#include "core/contacts/ContactsCommon.hpp"
#include "core/HubIntegration.hpp"

// Feeds: Zalo has no timeline call (zca-js neither), so the feed is built locally. Every contact
// keeps a snapshot row (avatar, cover, bio, birthday); a slow background scan reads profiles in
// small batches and compares them with the snapshot. A difference becomes a feed event and one
// "Zalo10 Feeds" Hub notification. Rate limits only slow the scan down.

namespace {

const int    FEED_TICK_MS_DEFAULT = 15000;
const int    FEED_TICK_MS_MAX     = 120000;
const int    FEED_BATCH_DEFAULT   = 15;
const int    FEED_BATCH_MIN       = 3;
// Minimum time between two passes. One pass of ~100 contacts already takes about as long, so in
// practice the scan runs continuously at the paced rate (one batch per tick) and a change is
// seen within about two minutes; a big list simply takes longer per pass.
const qint64 FEED_CYCLE_MS        = 60LL * 1000;
const qint64 FEED_START_DELAY_MS  = 45000;
const qint64 FEED_RECHECK_MS      = 5LL * 60 * 1000;
const int    FEED_MAX_EVENTS      = 300;
const int    FEED_PRIORITY_CHATS  = 150;

struct FeedEvent {
    QString uid, name, kind, oldValue, newValue;
};

QString stripUid0(const QString &s)
{
    return s.endsWith("_0") ? s.left(s.size() - 2) : s;
}

bool allDigits(const QString &s)
{
    if (s.isEmpty()) return false;
    for (int i = 0; i < s.size(); ++i) if (!s[i].isDigit()) return false;
    return true;
}

bool looksLikeProfile(const QVariantMap &m)
{
    return m.contains("userId") || m.contains("displayName") || m.contains("zaloName")
        || m.contains("username") || m.contains("avatar");
}

// Same search as findProfileMap() in Contacts_Profile.cpp, but collects every profile of a batch
// reply (keyed "<uid>_0", possibly inside JSON strings) instead of looking for one uid.
void collectProfiles(const QVariant &v, QMap<QString, QVariantMap> &out, int depth)
{
    if (depth > 8) return;
    if (v.type() == QVariant::Map) {
        QVariantMap m = v.toMap();
        if (m.contains("userId") && looksLikeProfile(m)) {
            QString uid = stripUid0(m.value("userId").toString());
            if (!uid.isEmpty() && !out.contains(uid)) out.insert(uid, m);
            return;
        }
        for (QVariantMap::const_iterator it = m.constBegin(); it != m.constEnd(); ++it) {
            if (it.value().type() == QVariant::Map) {
                QVariantMap c = it.value().toMap();
                QString k = stripUid0(it.key());
                if (!c.contains("userId") && allDigits(k) && looksLikeProfile(c)) {
                    if (!out.contains(k)) out.insert(k, c);
                    continue;
                }
            }
            collectProfiles(it.value(), out, depth + 1);
        }
    } else if (v.type() == QVariant::String) {
        QString str = v.toString().trimmed();
        if (str.startsWith("{") && str.size() > 2) {
            QVariantMap inner = jsonToMap(str.toUtf8());
            if (!inner.isEmpty()) collectProfiles(QVariant(inner), out, depth + 1);
        }
    } else if (v.type() == QVariant::List) {
        QVariantList l = v.toList();
        for (int i = 0; i < l.size(); ++i) collectProfiles(l[i], out, depth + 1);
    }
}

void bindText(sqlite3_stmt *st, int i, const QString &s)
{
    QByteArray b = s.toUtf8();
    sqlite3_bind_text(st, i, b.constData(), b.size(), SQLITE_TRANSIENT);
}

// known=false stores NULL: "never seen this field", so a field that was missing from an earlier
// reply can never produce a fake change later.
void bindMaybe(sqlite3_stmt *st, int i, bool known, const QString &s)
{
    if (known) bindText(st, i, s); else sqlite3_bind_null(st, i);
}

QString colText(sqlite3_stmt *st, int i)
{
    const unsigned char *t = sqlite3_column_text(st, i);
    return t ? QString::fromUtf8(reinterpret_cast<const char*>(t)) : QString();
}

bool colKnown(sqlite3_stmt *st, int i)
{
    return sqlite3_column_type(st, i) != SQLITE_NULL;
}

// Compare avatars/covers by path only: host, scheme and query can change without the image changing.
QString urlKey(const QString &url)
{
    QString u = url;
    int q = u.indexOf('?');
    if (q >= 0) u = u.left(q);
    int s = u.indexOf("://");
    if (s >= 0) {
        u = u.mid(s + 3);
        int slash = u.indexOf('/');
        u = (slash >= 0) ? u.mid(slash) : QString();
    }
    return u;
}

QString kindPhrase(const QString &kind)
{
    if (kind == "avatar") return "changed their profile picture";
    if (kind == "cover")  return "changed their cover photo";
    if (kind == "bio")    return "updated their bio";
    if (kind == "dob")    return "updated their birthday";
    return "updated their profile";
}

} // namespace

void ZaloService::feedsEnsureTables()
{
    if (!m_db) return;
    if (!m_feedTablesReady) {
        sqlite3_exec(m_db,
            "CREATE TABLE IF NOT EXISTS contact_snapshot ("
            "  uid        TEXT PRIMARY KEY,"
            "  name       TEXT,"
            "  avatarUrl  TEXT,"
            "  avatarKey  TEXT,"
            "  coverUrl   TEXT,"
            "  coverKey   TEXT,"
            "  bio        TEXT,"
            "  dob        TEXT,"
            "  checkedAt  INTEGER DEFAULT 0,"
            "  hasProfile INTEGER DEFAULT 0"
            ");", 0, 0, 0);
        sqlite3_exec(m_db,
            "CREATE TABLE IF NOT EXISTS feed_events ("
            "  id       INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  uid      TEXT NOT NULL,"
            "  name     TEXT,"
            "  kind     TEXT NOT NULL,"
            "  oldValue TEXT,"
            "  newValue TEXT,"
            "  ts       INTEGER DEFAULT 0,"
            "  seen     INTEGER DEFAULT 0"
            ");", 0, 0, 0);
        sqlite3_exec(m_db, "CREATE INDEX IF NOT EXISTS idx_feed_seen ON feed_events(seen);", 0, 0, 0);
        // Added later: Zalo's own "profile last updated" time. Fails harmlessly when it already exists.
        sqlite3_exec(m_db, "ALTER TABLE contact_snapshot ADD COLUMN lastUpdate INTEGER DEFAULT 0;", 0, 0, 0);
        sqlite3_exec(m_db,
            "CREATE TABLE IF NOT EXISTS feeds_meta (k TEXT PRIMARY KEY, v TEXT);", 0, 0, 0);
        sqlite3_exec(m_db,
            "CREATE TABLE IF NOT EXISTS group_cache ("
            "  groupId TEXT PRIMARY KEY,"
            "  name    TEXT,"
            "  avatar  TEXT"
            ");", 0, 0, 0);
        m_feedTablesReady = true;
    }

    // Stored data belongs to one account: drop it when a different account logs in.
    if (m_uid.isEmpty() || property("feedsUidChecked").toString() == m_uid) return;
    QString stored;
    bool have = false;
    sqlite3_stmt *st = 0;
    if (sqlite3_prepare_v2(m_db, "SELECT v FROM feeds_meta WHERE k='uid'", -1, &st, 0) == SQLITE_OK) {
        if (sqlite3_step(st) == SQLITE_ROW) { stored = colText(st, 0); have = true; }
        sqlite3_finalize(st);
    }
    if (have && stored != m_uid) {
        sqlite3_exec(m_db, "DELETE FROM contact_snapshot;", 0, 0, 0);
        sqlite3_exec(m_db, "DELETE FROM feed_events;", 0, 0, 0);
        sqlite3_exec(m_db, "DELETE FROM group_cache;", 0, 0, 0);
    }
    if (!have || stored != m_uid) {
        sqlite3_stmt *w = 0;
        if (sqlite3_prepare_v2(m_db,
                "INSERT OR REPLACE INTO feeds_meta (k, v) VALUES ('uid', ?)", -1, &w, 0) == SQLITE_OK) {
            bindText(w, 1, m_uid);
            sqlite3_step(w);
            sqlite3_finalize(w);
        }
    }
    setProperty("feedsUidChecked", m_uid);
}

// Called with the full friend list after every getfriends. Only keeps the roster up to date
// (new friends get an empty snapshot, name/avatar refreshed for display); changes are detected by
// the profile scan, always from the same source, so two sources can't disagree.
void ZaloService::feedsOnFriends(const QVariantList &threads, bool complete)
{
    if (!m_db || threads.isEmpty()) return;
    feedsEnsureTables();

    sqlite3_stmt *ins = 0, *upd = 0;
    if (sqlite3_prepare_v2(m_db,
            "INSERT OR IGNORE INTO contact_snapshot (uid, name, avatarUrl) VALUES (?,?,?)",
            -1, &ins, 0) != SQLITE_OK) return;
    if (sqlite3_prepare_v2(m_db,
            "UPDATE contact_snapshot SET name=?, "
            "avatarUrl=CASE WHEN hasProfile=1 THEN avatarUrl ELSE ? END WHERE uid=?", -1, &upd, 0) != SQLITE_OK) {
        sqlite3_finalize(ins);
        return;
    }

    QSet<QString> current;
    sqlite3_exec(m_db, "BEGIN", 0, 0, 0);
    for (int i = 0; i < threads.size(); ++i) {
        QVariantMap t = threads[i].toMap();
        QString uid = t.value("threadId").toString();
        if (uid.isEmpty()) continue;
        QString name = t.value("name").toString();
        QString avatar = t.value("avatar").toString();
        current.insert(uid);

        bindText(ins, 1, uid); bindText(ins, 2, name); bindText(ins, 3, avatar);
        sqlite3_step(ins);
        sqlite3_reset(ins);

        bindText(upd, 1, name); bindText(upd, 2, avatar); bindText(upd, 3, uid);
        sqlite3_step(upd);
        sqlite3_reset(upd);
    }
    sqlite3_finalize(ins);
    sqlite3_finalize(upd);

    if (complete) {
        QStringList gone;
        sqlite3_stmt *all = 0;
        if (sqlite3_prepare_v2(m_db, "SELECT uid FROM contact_snapshot", -1, &all, 0) == SQLITE_OK) {
            while (sqlite3_step(all) == SQLITE_ROW) {
                QString uid = colText(all, 0);
                if (!current.contains(uid)) gone << uid;
            }
            sqlite3_finalize(all);
        }
        sqlite3_stmt *del = 0;
        if (!gone.isEmpty() && sqlite3_prepare_v2(m_db,
                "DELETE FROM contact_snapshot WHERE uid=?", -1, &del, 0) == SQLITE_OK) {
            for (int i = 0; i < gone.size(); ++i) {
                bindText(del, 1, gone[i]);
                sqlite3_step(del);
                sqlite3_reset(del);
            }
            sqlite3_finalize(del);
        }
    }
    sqlite3_exec(m_db, "COMMIT", 0, 0, 0);
}

void ZaloService::startFeedsWatch()
{
    if (m_feedWatching) return;
    m_feedWatching = true;
    m_feedStartAt = QDateTime::currentMSecsSinceEpoch() + FEED_START_DELAY_MS;
    m_feedTimer->start(m_feedTickMs);
    qDebug() << "[Feeds] watching contacts, batch" << m_feedBatch << "every" << m_feedTickMs << "ms";
}

// Queue order: people you chat with most recently first, then everybody else, least recently
// checked first (never checked = 0 sorts first, which is how the first baseline is built).
void ZaloService::feedsBuildQueue()
{
    m_feedQueue.clear();
    if (!m_db) return;

    QSet<QString> known;
    QStringList byAge;
    sqlite3_stmt *st = 0;
    if (sqlite3_prepare_v2(m_db, "SELECT uid FROM contact_snapshot ORDER BY checkedAt ASC", -1, &st, 0) == SQLITE_OK) {
        while (sqlite3_step(st) == SQLITE_ROW) {
            QString uid = colText(st, 0);
            known.insert(uid);
            byAge << uid;
        }
        sqlite3_finalize(st);
    }

    QSet<QString> used;
    if (sqlite3_prepare_v2(m_db,
            "SELECT threadId FROM messages WHERE isGroup=0 GROUP BY threadId "
            "ORDER BY MAX(CAST(ts AS INTEGER)) DESC LIMIT ?", -1, &st, 0) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, FEED_PRIORITY_CHATS);
        while (sqlite3_step(st) == SQLITE_ROW) {
            QString uid = colText(st, 0);
            if (known.contains(uid) && !used.contains(uid)) {
                m_feedQueue << uid;
                used.insert(uid);
            }
        }
        sqlite3_finalize(st);
    }
    for (int i = 0; i < byAge.size(); ++i)
        if (!used.contains(byAge[i])) m_feedQueue << byAge[i];
}

void ZaloService::feedsBackoff(bool rateLimited)
{
    m_feedOkStreak = 0;
    if (m_feedBackoffStep < 5) m_feedBackoffStep++;
    qint64 ms = 60000LL << m_feedBackoffStep;          // 2, 4, 8, 16, 32 minutes
    if (ms > 3600000LL) ms = 3600000LL;
    m_feedBackoffUntil = QDateTime::currentMSecsSinceEpoch() + ms;
    if (rateLimited) {
        // "Limit it down": smaller batches, slower ticks.
        if (m_feedBatch > FEED_BATCH_MIN) m_feedBatch = qMax(FEED_BATCH_MIN, m_feedBatch / 2);
        m_feedTickMs = qMin(FEED_TICK_MS_MAX, m_feedTickMs * 2);
        m_feedTimer->setInterval(m_feedTickMs);
    }
    qWarning() << "[Feeds] backing off for" << ms / 1000 << "s, batch" << m_feedBatch
               << "tick" << m_feedTickMs << "ms, rateLimited=" << rateLimited;
}

void ZaloService::onFeedScanTick()
{
    if (!m_loggedIn || !m_db || m_feedBusy || m_secretKey.isEmpty()) return;
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now < m_feedStartAt || now < m_feedBackoffUntil) return;

    feedsEnsureTables();
    if (m_feedQueue.isEmpty()) {
        if (m_feedCycleStart > 0 && now - m_feedCycleStart < FEED_CYCLE_MS) return;
        feedsBuildQueue();
        m_feedCycleStart = now;
        if (m_feedQueue.isEmpty()) return;
    }

    QStringList batch;
    while (batch.size() < m_feedBatch && !m_feedQueue.isEmpty())
        batch << m_feedQueue.takeFirst();

    QVariantList verMap;
    for (int i = 0; i < batch.size(); ++i) verMap.append(batch[i] + "_0");
    QVariantMap p;
    p["phonebook_version"]   = 0;
    p["friend_pversion_map"] = verMap;
    p["avatar_size"]         = 120;
    p["language"]            = "vi";
    p["show_online_status"]  = 1;

    QNetworkReply *reply = sendProfileApi("/api/social/friend/getprofiles/v2", true, p);
    reply->setProperty("feedUids", batch);
    m_feedReply = reply;
    m_feedBusy = true;
    connect(reply, SIGNAL(finished()), this, SLOT(onFeedProfilesDone()));
    m_feedWatchdog->start(30000);
}

void ZaloService::onFeedRequestTimeout()
{
    if (m_feedReply && m_feedReply->isRunning()) m_feedReply->abort();
}

void ZaloService::onFeedProfilesDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    m_feedWatchdog->stop();
    m_feedReply = 0;
    m_feedBusy = false;
    QStringList uids = reply->property("feedUids").toStringList();
    QByteArray raw = reply->readAll();
    reply->deleteLater();

    // 429 comes back as an HTML page. Network failures are put back in the queue and retried later.
    if (raw.isEmpty() || raw.contains("429 Too Many Requests") || raw.contains("<html")) {
        feedsBackoff(raw.contains("429"));
        for (int i = uids.size() - 1; i >= 0; --i) m_feedQueue.prepend(uids[i]);
        return;
    }

    QVariantMap root = jsonToMap(raw);
    int ec = root["error_code"].toInt();
    QVariantMap outer;
    if (ec == 0) {
        outer = jsonToMap(aesDecryptBase64(m_secretKey, root["data"].toString()).toUtf8());
        if (outer.contains("error_code")) ec = outer["error_code"].toInt();
    }
    if (ec != 0) {
        // Not retried: a bad uid would otherwise block the queue. Only mark them as tried.
        qWarning() << "[Feeds] getprofiles error_code" << ec;
        feedsApplyProfiles(uids, QMap<QString, QVariantMap>(), true);
        feedsBackoff(false);
        return;
    }

    QMap<QString, QVariantMap> profs;
    collectProfiles(QVariant(outer), profs, 0);

    if (m_feedBatchProbe) {
        m_feedBatchProbe = false;
        QString fields;
        if (!profs.isEmpty()) {
            const QVariantMap &f = profs.constBegin().value();
            for (QVariantMap::const_iterator it = f.constBegin(); it != f.constEnd(); ++it)
                fields += it.key() + " ";
        }
        qDebug() << "[Feeds] first batch: asked" << uids.size() << "got" << profs.size()
                 << "profile fields:" << fields;
        if (uids.size() > 1 && profs.size() < (uids.size() + 1) / 2) {
            m_feedBatch = 1;
            setProperty("feedBatchFixed", true);
            qWarning() << "[Feeds] batch lookup returned too few profiles, going one by one";
        }
    }

    int events = feedsApplyProfiles(uids, profs, true);
    if (events > 0) qDebug() << "[Feeds] new events:" << events;

    setProperty("feedPassScanned", property("feedPassScanned").toInt() + uids.size());
    setProperty("feedPassEvents",  property("feedPassEvents").toInt() + events);
    if (m_feedQueue.isEmpty()) {
        qDebug() << "[Feeds] pass complete: checked" << property("feedPassScanned").toInt()
                 << "contacts," << property("feedPassEvents").toInt() << "changes";
        setProperty("feedPassScanned", 0);
        setProperty("feedPassEvents", 0);
    }

    m_feedBackoffStep = 0;
    if (++m_feedOkStreak >= 10) {
        m_feedOkStreak = 0;
        if (m_feedTickMs > FEED_TICK_MS_DEFAULT) {
            m_feedTickMs = qMax(FEED_TICK_MS_DEFAULT, m_feedTickMs / 2);
            m_feedTimer->setInterval(m_feedTickMs);
        }
        if (m_feedBatch < FEED_BATCH_DEFAULT && !property("feedBatchFixed").toBool())
            m_feedBatch = qMin(FEED_BATCH_DEFAULT, m_feedBatch + 2);
    }
}

// Compares each profile with its snapshot row and stores the new values. Returns how many events
// were added. A missing profile only refreshes checkedAt.
int ZaloService::feedsApplyProfiles(const QStringList &uids, const QMap<QString, QVariantMap> &profiles, bool force)
{
    if (!m_db || uids.isEmpty()) return 0;
    feedsEnsureTables();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    sqlite3_stmt *sel = 0, *upd = 0, *touch = 0, *ins = 0, *dup = 0;
    bool ok =
        sqlite3_prepare_v2(m_db,
            "SELECT name, avatarKey, coverKey, bio, dob, checkedAt, hasProfile, avatarUrl, coverUrl, lastUpdate "
            "FROM contact_snapshot WHERE uid=?", -1, &sel, 0) == SQLITE_OK
        && sqlite3_prepare_v2(m_db,
            "UPDATE contact_snapshot SET name=?, avatarUrl=?, avatarKey=?, coverUrl=?, coverKey=?, "
            "bio=?, dob=?, checkedAt=?, lastUpdate=?, hasProfile=1 WHERE uid=?", -1, &upd, 0) == SQLITE_OK
        && sqlite3_prepare_v2(m_db,
            "UPDATE contact_snapshot SET checkedAt=? WHERE uid=?", -1, &touch, 0) == SQLITE_OK
        && sqlite3_prepare_v2(m_db,
            "INSERT INTO feed_events (uid, name, kind, oldValue, newValue, ts, seen) "
            "VALUES (?,?,?,?,?,?,0)", -1, &ins, 0) == SQLITE_OK
        && sqlite3_prepare_v2(m_db,
            "SELECT 1 FROM feed_events WHERE uid=? AND kind=? AND newValue=? AND ts>? LIMIT 1",
            -1, &dup, 0) == SQLITE_OK;
    if (!ok) {
        if (sel)   sqlite3_finalize(sel);
        if (upd)   sqlite3_finalize(upd);
        if (touch) sqlite3_finalize(touch);
        if (ins)   sqlite3_finalize(ins);
        if (dup)   sqlite3_finalize(dup);
        return 0;
    }

    QList<FeedEvent> events;
    sqlite3_exec(m_db, "BEGIN", 0, 0, 0);

    for (int i = 0; i < uids.size(); ++i) {
        const QString uid = uids[i];

        bindText(sel, 1, uid);
        if (sqlite3_step(sel) != SQLITE_ROW) { sqlite3_reset(sel); continue; }   // not a contact (any more)
        QString name0       = colText(sel, 0);
        bool    avatarKnown = colKnown(sel, 1);
        QString avatarKey0  = colText(sel, 1);
        bool    coverKnown  = colKnown(sel, 2);
        QString coverKey0   = colText(sel, 2);
        bool    bioKnown    = colKnown(sel, 3);
        QString bio0        = colText(sel, 3);
        bool    dobKnown    = colKnown(sel, 4);
        QString dob0        = colText(sel, 4);
        qint64  checkedAt   = sqlite3_column_int64(sel, 5);
        bool    hasProfile  = sqlite3_column_int(sel, 6) != 0;
        QString avatarUrl0  = colText(sel, 7);
        QString coverUrl0   = colText(sel, 8);
        qint64  lastUpdate0 = sqlite3_column_int64(sel, 9);
        sqlite3_reset(sel);

        if (!force && checkedAt > 0 && now - checkedAt < FEED_RECHECK_MS) continue;

        QMap<QString, QVariantMap>::const_iterator pit = profiles.constFind(uid);
        if (pit == profiles.constEnd()) {
            sqlite3_bind_int64(touch, 1, now);
            bindText(touch, 2, uid);
            sqlite3_step(touch);
            sqlite3_reset(touch);
            continue;
        }
        const QVariantMap &p = pit.value();

        QString name = p.value("displayName").toString();
        if (name.isEmpty()) name = p.value("zaloName").toString();
        if (name.isEmpty()) name = name0;

        // Each field is only taken from the reply when it is there; otherwise the old value stays.
        bool    nAvatarKnown = avatarKnown;
        QString nAvatarUrl   = avatarUrl0;
        QString nAvatarKey   = avatarKey0;
        if (p.contains("avatar") && !p.value("avatar").toString().isEmpty()) {
            nAvatarUrl   = p.value("avatar").toString();
            nAvatarKey   = urlKey(nAvatarUrl);
            nAvatarKnown = true;
        }
        bool    nCoverKnown = coverKnown;
        QString nCoverUrl   = coverUrl0;
        QString nCoverKey   = coverKey0;
        if (p.contains("cover") || p.contains("bgavatar")) {
            QString c = p.value("cover").toString();
            if (c.isEmpty()) c = p.value("bgavatar").toString();
            // A cover that disappears is not reported; only a new/changed one is.
            if (!c.isEmpty() || !coverKnown) {
                nCoverUrl   = c;
                nCoverKey   = urlKey(c);
                nCoverKnown = true;
            }
        }
        bool    nBioKnown = bioKnown;
        QString nBio      = bio0;
        if (p.contains("status")) {
            nBio = p.value("status").toString().trimmed();
            if (nBio.size() > 500) nBio = nBio.left(500);
            nBioKnown = true;
        }
        bool    nDobKnown = dobKnown;
        QString nDob      = dob0;
        if (p.contains("sdob")) {
            QString d = p.value("sdob").toString().trimmed();
            // A birthday that gets hidden is not reported.
            if (!d.isEmpty() || !dobKnown) { nDob = d; nDobKnown = true; }
        }

        qint64 nLastUpdate = p.value("lastUpdateTime").toLongLong();
        if (nLastUpdate <= 0) nLastUpdate = lastUpdate0;

        if (hasProfile) {
            const bool bumped = (lastUpdate0 > 0 && nLastUpdate > lastUpdate0);
            const bool avatarPath = avatarKnown && nAvatarKnown && !avatarKey0.isEmpty() && nAvatarKey != avatarKey0;
            const bool avatarFull = nAvatarKnown && !avatarUrl0.isEmpty() && nAvatarUrl != avatarUrl0;
            const bool coverPath  = coverKnown && nCoverKnown && !nCoverKey.isEmpty() && nCoverKey != coverKey0;
            const bool bioDiff    = bioKnown && nBioKnown && nBio != bio0;
            const bool dobDiff    = dobKnown && nDobKnown && !nDob.isEmpty() && nDob != dob0;

            // The picture's link may keep its path and change only a trailing part, so the path
            // compare can miss it. When Zalo says the profile was updated (lastUpdateTime went up)
            // and nothing else explains it, a different full link counts as a new picture.
            const bool avatarChanged = avatarPath
                || (bumped && avatarFull && !coverPath && !bioDiff && !dobDiff);

            if (bumped)
                qDebug() << "[Feeds] profile updated by Zalo:" << "avatarPath" << avatarPath
                         << "avatarFullLink" << avatarFull << "cover" << coverPath
                         << "bio" << bioDiff << "dob" << dobDiff;

            FeedEvent e;
            e.uid = uid; e.name = name;
            if (avatarChanged) {
                e.kind = "avatar"; e.oldValue = avatarUrl0; e.newValue = nAvatarUrl;
                events.append(e);
            }
            if (coverPath) {
                e.kind = "cover"; e.oldValue = coverUrl0; e.newValue = nCoverUrl;
                events.append(e);
            }
            if (bioDiff) {
                e.kind = "bio"; e.oldValue = bio0; e.newValue = nBio;
                events.append(e);
            }
            if (dobDiff) {
                e.kind = "dob"; e.oldValue = dob0; e.newValue = nDob;
                events.append(e);
            }
        }

        bindText(upd, 1, name);
        bindText(upd, 2, nAvatarUrl);
        bindMaybe(upd, 3, nAvatarKnown, nAvatarKey);
        bindText(upd, 4, nCoverUrl);
        bindMaybe(upd, 5, nCoverKnown, nCoverKey);
        bindMaybe(upd, 6, nBioKnown, nBio);
        bindMaybe(upd, 7, nDobKnown, nDob);
        sqlite3_bind_int64(upd, 8, now);
        sqlite3_bind_int64(upd, 9, nLastUpdate);
        bindText(upd, 10, uid);
        int urc = sqlite3_step(upd);
        if (urc != SQLITE_DONE)
            qWarning() << "[Feeds] snapshot write failed:" << urc << sqlite3_errmsg(m_db);
        sqlite3_reset(upd);
    }

    // If most of a batch "changed avatar" at once, the CDN link format changed, not the pictures.
    int avatarEvents = 0;
    for (int i = 0; i < events.size(); ++i) if (events[i].kind == "avatar") ++avatarEvents;
    if (avatarEvents >= 8 && avatarEvents * 10 >= uids.size() * 6) {
        qWarning() << "[Feeds] ignoring" << avatarEvents << "avatar changes in one batch (link format change?)";
        QList<FeedEvent> kept;
        for (int i = 0; i < events.size(); ++i) if (events[i].kind != "avatar") kept.append(events[i]);
        events = kept;
    }

    int added = 0;
    QList<FeedEvent> addedEvents;
    for (int i = 0; i < events.size(); ++i) {
        const FeedEvent &e = events[i];
        bindText(dup, 1, e.uid); bindText(dup, 2, e.kind); bindText(dup, 3, e.newValue);
        sqlite3_bind_int64(dup, 4, now - 10 * 60 * 1000);   // same change seen twice within minutes
        bool exists = (sqlite3_step(dup) == SQLITE_ROW);
        sqlite3_reset(dup);
        if (exists) continue;
        bindText(ins, 1, e.uid); bindText(ins, 2, e.name); bindText(ins, 3, e.kind);
        bindText(ins, 4, e.oldValue); bindText(ins, 5, e.newValue);
        sqlite3_bind_int64(ins, 6, now);
        sqlite3_step(ins);
        sqlite3_reset(ins);
        addedEvents.append(e);
        ++added;
    }
    if (added > 0) {
        char sql[160];
        qsnprintf(sql, sizeof(sql),
                  "DELETE FROM feed_events WHERE id NOT IN (SELECT id FROM feed_events ORDER BY id DESC LIMIT %d);",
                  FEED_MAX_EVENTS);
        sqlite3_exec(m_db, sql, 0, 0, 0);
    }
    sqlite3_exec(m_db, "COMMIT", 0, 0, 0);

    sqlite3_finalize(sel);
    sqlite3_finalize(upd);
    sqlite3_finalize(touch);
    sqlite3_finalize(ins);
    sqlite3_finalize(dup);

    if (added == 0) return 0;

    // The feed shows the new picture, so the Chats and Contacts lists must show it too: re-download
    // it now. avatarReady(uid, newPath) then updates every row of that person.
    for (int i = 0; i < addedEvents.size(); ++i) {
        if (addedEvents[i].kind == "avatar" && !addedEvents[i].newValue.isEmpty())
            refreshAvatar(addedEvents[i].uid, addedEvents[i].newValue);
    }

    if (m_feedsViewing) {
        markFeedsSeen();
        return added;
    }

    QStringList distinct;
    for (int i = 0; i < addedEvents.size(); ++i)
        if (!distinct.contains(addedEvents[i].uid)) distinct << addedEvents[i].uid;
    const FeedEvent &first = addedEvents[0];
    QString who = first.name.isEmpty() ? QString("A contact") : first.name;
    QString preview;
    if (distinct.size() == 1) {
        preview = (addedEvents.size() == 1)
            ? who + " " + kindPhrase(first.kind)
            : who + " updated their profile";
    } else {
        int others = distinct.size() - 1;
        preview = who + " and " + QString::number(others)
                + (others == 1 ? " other updated their profiles" : " others updated their profiles");
    }

    emit feedEventsChanged(unseenFeedCount());
    if (m_hub && feedsNotifyEnabled()) m_hub->upsertFeedItem(preview, now, added);
    return added;
}

QVariantList ZaloService::getFeedEvents(int limit)
{
    QVariantList out;
    if (!m_db) return out;
    feedsEnsureTables();
    if (limit <= 0 || limit > FEED_MAX_EVENTS) limit = FEED_MAX_EVENTS;

    sqlite3_stmt *st = 0;
    if (sqlite3_prepare_v2(m_db,
            "SELECT e.id, e.uid, e.name, e.kind, e.oldValue, e.newValue, e.ts, e.seen, s.avatarUrl "
            "FROM feed_events e LEFT JOIN contact_snapshot s ON s.uid=e.uid "
            "ORDER BY e.id DESC LIMIT ?", -1, &st, 0) != SQLITE_OK) return out;
    sqlite3_bind_int(st, 1, limit);
    while (sqlite3_step(st) == SQLITE_ROW) {
        QVariantMap m;
        m["id"]       = QString::number(sqlite3_column_int64(st, 0));
        m["uid"]      = colText(st, 1);
        m["name"]     = colText(st, 2);
        m["kind"]     = colText(st, 3);
        m["oldValue"] = colText(st, 4);
        m["newValue"] = colText(st, 5);
        m["ts"]       = QString::number(sqlite3_column_int64(st, 6));
        m["seen"]     = sqlite3_column_int(st, 7) != 0;
        m["avatar"]   = colText(st, 8);
        // A profile-picture event carries the picture of that moment; the stored link may be newer.
        if (m["kind"].toString() == "avatar") m["avatar"] = m["newValue"];
        out.append(m);
    }
    sqlite3_finalize(st);
    return out;
}

int ZaloService::unseenFeedCount()
{
    if (!m_db) return 0;
    feedsEnsureTables();
    int n = 0;
    sqlite3_stmt *st = 0;
    if (sqlite3_prepare_v2(m_db, "SELECT COUNT(*) FROM feed_events WHERE seen=0", -1, &st, 0) == SQLITE_OK) {
        if (sqlite3_step(st) == SQLITE_ROW) n = sqlite3_column_int(st, 0);
        sqlite3_finalize(st);
    }
    return n;
}

void ZaloService::markFeedsSeen()
{
    if (m_db) {
        feedsEnsureTables();
        sqlite3_exec(m_db, "UPDATE feed_events SET seen=1 WHERE seen=0;", 0, 0, 0);
    }
    if (m_hub) m_hub->markThreadRead(QString::fromLatin1(HubIntegration::FEED_ITEM_ID));
    emit feedEventsChanged(0);
}

void ZaloService::clearFeedEvents()
{
    if (m_db) {
        feedsEnsureTables();
        sqlite3_exec(m_db, "DELETE FROM feed_events;", 0, 0, 0);
    }
    if (m_hub) m_hub->removeThreadItem(QString::fromLatin1(HubIntegration::FEED_ITEM_ID));
    emit feedEventsChanged(0);
}

bool ZaloService::hubSheetEnabled() const
{
    QSettings s("BerryLife", "Zalo10");
    return s.value("hubChatSheet", true).toBool();
}

void ZaloService::setHubSheetEnabled(bool enabled)
{
    QSettings s("BerryLife", "Zalo10");
    s.setValue("hubChatSheet", enabled);
}

bool ZaloService::feedsNotifyEnabled() const
{
    QSettings s("BerryLife", "Zalo10");
    return s.value("feedsNotify", true).toBool();
}

void ZaloService::setFeedsNotifyEnabled(bool enabled)
{
    QSettings s("BerryLife", "Zalo10");
    s.setValue("feedsNotify", enabled);
    if (!enabled && m_hub) m_hub->removeThreadItem(QString::fromLatin1(HubIntegration::FEED_ITEM_ID));
}

void ZaloService::setFeedsViewing(bool viewing)
{
    m_feedsViewing = viewing;
    if (viewing) markFeedsSeen();
}

// ---------------------------------------------------------------------------------------------
// Group member names. Names come from (1) each group's currentMems, which only lists some members,
// (2) the friend list, (3) this lookup: members that nobody has named yet are queued when a label
// asks for them, then read in batches with the same getprofiles call the feed scan uses (it works
// for any user id, not just friends). Everything is stored in member_names, so a restart does not
// bring the bare numbers back.
// ---------------------------------------------------------------------------------------------

void ZaloService::memberEnsureTable()
{
    if (!m_db) return;
    sqlite3_exec(m_db,
        "CREATE TABLE IF NOT EXISTS member_names (uid TEXT PRIMARY KEY, name TEXT NOT NULL, ts INTEGER)",
        0, 0, 0);
}

void ZaloService::loadMemberNamesFromDb()
{
    if (!m_db) return;
    m_memberDbLoaded = true;
    memberEnsureTable();
    sqlite3_stmt *st = 0;
    if (sqlite3_prepare_v2(m_db, "SELECT uid, name FROM member_names", -1, &st, 0) != SQLITE_OK) return;
    while (sqlite3_step(st) == SQLITE_ROW) {
        QString uid  = colText(st, 0);
        QString name = colText(st, 1);
        if (!uid.isEmpty() && !name.isEmpty() && !m_memberNames.contains(uid))
            m_memberNames.insert(uid, name);
    }
    sqlite3_finalize(st);
}

void ZaloService::rememberMemberName(const QString &uid, const QString &nameIn, bool overwrite)
{
    QString name = nameIn.trimmed();
    if (uid.isEmpty() || name.isEmpty() || name == uid) return;   // the id itself is not a name
    QMap<QString, QString>::const_iterator it = m_memberNames.constFind(uid);
    if (it != m_memberNames.constEnd() && (!overwrite || it.value() == name)) return;
    m_memberNames.insert(uid, name);
    if (!m_db) return;
    memberEnsureTable();
    sqlite3_stmt *st = 0;
    if (sqlite3_prepare_v2(m_db, "INSERT OR REPLACE INTO member_names (uid, name, ts) VALUES (?,?,?)",
                           -1, &st, 0) == SQLITE_OK) {
        bindText(st, 1, uid);
        bindText(st, 2, name);
        sqlite3_bind_int64(st, 3, QDateTime::currentMSecsSinceEpoch());
        sqlite3_step(st);
        sqlite3_finalize(st);
    }
}

void ZaloService::rememberMemberNames(const QMap<QString, QString> &names)
{
    if (names.isEmpty()) return;
    if (!m_memberDbLoaded) loadMemberNamesFromDb();
    const bool haveDb = (m_db != 0);
    if (haveDb) { memberEnsureTable(); sqlite3_exec(m_db, "BEGIN", 0, 0, 0); }
    for (QMap<QString, QString>::const_iterator it = names.constBegin(); it != names.constEnd(); ++it)
        rememberMemberName(it.key(), it.value(), false);
    if (haveDb) sqlite3_exec(m_db, "COMMIT", 0, 0, 0);
    emit memberNamesResolved();
}

QString ZaloService::memberDisplayName(const QString &uid)
{
    if (uid.isEmpty()) return QString();
    if (!m_memberDbLoaded && m_db) loadMemberNamesFromDb();
    QMap<QString, QString>::const_iterator it = m_memberNames.constFind(uid);
    if (it != m_memberNames.constEnd() && !it.value().isEmpty()) return it.value();

    // Unknown: ask once per session. Our own id and non-numeric ids are never looked up.
    if (m_loggedIn && uid != m_uid && allDigits(uid) && !m_memberAsked.contains(uid)) {
        m_memberAsked.insert(uid);
        m_memberQueue << uid;
        if (!m_memberTimer->isActive()) m_memberTimer->start(700);
    }
    return QString();
}

void ZaloService::onMemberLookupTick()
{
    if (m_memberQueue.isEmpty() || m_memberBusy) return;
    if (!m_loggedIn || m_secretKey.isEmpty()) { m_memberTimer->start(5000); return; }

    QStringList batch;
    while (batch.size() < 20 && !m_memberQueue.isEmpty()) batch << m_memberQueue.takeFirst();

    QVariantList verMap;
    for (int i = 0; i < batch.size(); ++i) verMap.append(batch[i] + "_0");
    QVariantMap p;
    p["phonebook_version"]   = 0;
    p["friend_pversion_map"] = verMap;
    p["avatar_size"]         = 120;
    p["language"]            = "vi";
    p["show_online_status"]  = 0;

    QNetworkReply *reply = sendProfileApi("/api/social/friend/getprofiles/v2", true, p);
    reply->setProperty("memberUids", batch);
    m_memberBusy = true;
    connect(reply, SIGNAL(finished()), this, SLOT(onMemberLookupDone()));
}

void ZaloService::onMemberLookupDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    m_memberBusy = false;
    QStringList uids = reply->property("memberUids").toStringList();
    QByteArray raw = reply->readAll();
    reply->deleteLater();

    // Rate limit / network trouble: put the ids back and try again later, slowly.
    if (raw.isEmpty() || raw.contains("429 Too Many Requests") || raw.contains("<html")) {
        for (int i = uids.size() - 1; i >= 0; --i) m_memberQueue.prepend(uids[i]);
        m_memberTimer->start(raw.contains("429") ? 120000 : 30000);
        return;
    }

    QVariantMap root = jsonToMap(raw);
    int ec = root["error_code"].toInt();
    QVariantMap outer;
    if (ec == 0) {
        outer = jsonToMap(aesDecryptBase64(m_secretKey, root["data"].toString()).toUtf8());
        if (outer.contains("error_code")) ec = outer["error_code"].toInt();
    }
    if (ec != 0) {
        qWarning() << "[Members] getprofiles error_code" << ec;   // not retried: bad ids must not loop
    } else {
        QMap<QString, QVariantMap> profs;
        collectProfiles(QVariant(outer), profs, 0);
        int got = 0;
        for (QMap<QString, QVariantMap>::const_iterator it = profs.constBegin(); it != profs.constEnd(); ++it) {
            QString name = it.value().value("displayName").toString();
            if (name.isEmpty()) name = it.value().value("zaloName").toString();
            if (name.isEmpty()) name = it.value().value("username").toString();
            if (!name.isEmpty() && m_memberNames.value(it.key()) != name) { rememberMemberName(it.key(), name, true); ++got; }
        }
        qDebug() << "[Members] asked" << uids.size() << "named" << got;
        if (got > 0) emit memberNamesResolved();
    }
    if (!m_memberQueue.isEmpty()) m_memberTimer->start(1500);
}
