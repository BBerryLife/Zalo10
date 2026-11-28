#include "core/messages/MessagesCommon.hpp"
#include "utils/json/ZaloMessageJson.hpp"

// Zalo's own stickers (the ones the phone app shows), as opposed to StickerManager's image packs.
// Endpoints as in zca-js (src/apis/getStickerCategories, getStickerCategoryInfo,
// getStickerCategoryDetail, searchSticker, sendSticker), all on zpwServiceMap.sticker except sending:
//   GET  <sticker>/api/message/sticker/category/list/v2            {}           the account's categories
//   GET  <sticker>/api/message/sticker/category/detail             {cid}        name / count of one
//   GET  <sticker>/api/message/sticker/category/sticker_detail     {cid}        its stickers
//   GET  <sticker>/api/message/sticker/search                      {keyword, limit, srcType, imei}
//   POST <chat>/api/message/sticker | <group>/api/group/sticker    {stickerId, cateId, type, ...}
// The picture of any sticker comes from downloadSticker(id) (the same one used for received stickers).
// The exact shape of some answers is not documented, so parsing is lenient and the first answer of
// each kind logs its field names (never values) to help adjust it from a device log.

namespace {

const int kMaxCategories = 60;

// First list found in v: v itself, or a list-valued field of a map (data / stickers / items ...).
QVariantList firstList(const QVariant &v)
{
    if (v.type() == QVariant::List) return v.toList();
    if (v.type() == QVariant::Map) {
        QVariantMap m = v.toMap();
        static const char *keys[] = { "data", "stickers", "sticker", "items", "list", "cates", "categories" };
        for (unsigned i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i)
            if (m.value(keys[i]).type() == QVariant::List) return m.value(keys[i]).toList();
        for (QVariantMap::const_iterator it = m.constBegin(); it != m.constEnd(); ++it)
            if (it.value().type() == QVariant::List) return it.value().toList();
    }
    return QVariantList();
}

qint64 numField(const QVariantMap &m, const char *a, const char *b = 0, const char *c = 0)
{
    const char *keys[] = { a, b, c };
    for (int i = 0; i < 3; ++i) {
        if (!keys[i]) continue;
        QVariant v = m.value(keys[i]);
        if (!v.isValid()) continue;
        qint64 n = v.toLongLong();
        if (n == 0) n = (qint64)v.toDouble();
        if (n == 0) n = v.toString().toLongLong();
        if (n != 0) return n;
    }
    return 0;
}

QString shape(const QVariant &v)
{
    if (v.type() == QVariant::Map) return "map{" + QStringList(v.toMap().keys()).join(",") + "}";
    if (v.type() == QVariant::List) {
        QVariantList l = v.toList();
        return QString("list[%1]%2").arg(l.size()).arg(l.isEmpty() ? QString() : (" of " + shape(l.first())));
    }
    return QString::fromLatin1(v.typeName() ? v.typeName() : "null");
}

QVariantMap stickerItem(const QVariantMap &m, qint64 cateFallback)
{
    QVariantMap out;
    qint64 id   = numField(m, "sticker_id", "id", "stickerId");
    qint64 cate = numField(m, "cate_id", "cateId", "catId");
    qint64 type = numField(m, "type");
    if (id <= 0) return out;
    out["id"]     = (int)id;
    out["cateId"] = (int)(cate > 0 ? cate : cateFallback);
    out["type"]   = (int)(type > 0 ? type : 7);     // 7 is what Zalo Web sends for normal stickers
    return out;
}

} // namespace

QVariantList ZaloService::zaloStickerCategories() const
{
    if (!m_zaloStickerCats.isEmpty()) return m_zaloStickerCats;
    QSettings s("BerryLife", "Zalo10");
    return s.value("zstickers/cats_" + m_uid).toList();
}

void ZaloService::zaloStickerGet(const QString &path, const QString &paramsJson, const QString &kind, const QString &key)
{
    if (m_stickerServiceUrl.isEmpty()) {
        qWarning() << "[ZSticker] no zpwServiceMap.sticker url (re-login to refresh it)";
        if (kind == "cats" || kind == "catinfo") {
            if (kind == "catinfo") { if (--m_zsCatPending == 0) zaloStickerCategoriesDone(); return; }
            emit zaloStickerCategoriesReady(zaloStickerCategories(), "Sticker service unavailable - please log in again");
        } else {
            emit zaloStickerListReady(key, QVariantList(), "Sticker service unavailable - please log in again");
        }
        return;
    }
    QString enc = aesEncryptBase64(m_secretKey, paramsJson);
    QString url = m_stickerServiceUrl + path
                + "?zpw_ver=" + QString::number(API_VERSION)
                + "&zpw_type=" + QString::number(API_TYPE)
                + "&params=" + QString::fromLatin1(QUrl::toPercentEncoding(enc));
    QNetworkRequest req = buildRequest(url, "https://chat.zalo.me/");
    QNetworkReply *reply = m_manager->get(req);
    reply->setProperty("zsKind", kind);
    reply->setProperty("zsKey", key);
    connect(reply, SIGNAL(finished()), this, SLOT(onZaloStickerReply()));
}

void ZaloService::fetchZaloStickerCategories(bool force)
{
    if (!m_loggedIn) return;
    if (!force && !m_zaloStickerCats.isEmpty()) {
        emit zaloStickerCategoriesReady(m_zaloStickerCats, QString());
        return;
    }
    if (m_zsCatPending > 0) return;                     // already collecting
    zaloStickerGet("/api/message/sticker/category/list/v2", "{}", "cats", QString());
}

void ZaloService::fetchZaloStickerCategory(int cateId)
{
    if (!m_loggedIn || cateId <= 0) return;
    zaloStickerGet("/api/message/sticker/category/sticker_detail",
                   QString("{\"cid\":%1}").arg(cateId), "list", "c" + QString::number(cateId));
}

void ZaloService::searchZaloStickers(const QString &keyword)
{
    const QString kw = keyword.trimmed();
    if (!m_loggedIn || kw.isEmpty()) return;
    zaloStickerGet("/api/message/sticker/search",
                   QString("{\"keyword\":%1,\"limit\":50,\"srcType\":0,\"imei\":%2}")
                       .arg(jsonQuote(kw)).arg(jsonQuote(m_imei)),
                   "search", "s:" + kw);
}

void ZaloService::onZaloStickerReply()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    const QString kind = reply->property("zsKind").toString();
    const QString key  = reply->property("zsKey").toString();
    const bool netErr  = (reply->error() != QNetworkReply::NoError);
    const int  http    = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QByteArray raw = reply->readAll();
    reply->deleteLater();

    QString error;
    QVariant data;
    if (netErr) {
        error = "Network error";
        qWarning() << "[ZSticker]" << kind << "network error" << reply->errorString() << "HTTP" << http;
    } else {
        QVariantMap outer = jsonToMap(raw);
        int ec = outer.value("error_code").toInt();
        if (ec != 0) {
            error = outer.value("error_message").toString();
            qWarning() << "[ZSticker]" << kind << "error_code" << ec << error;
        } else {
            QVariant d = outer.value("data");
            if (d.type() == QVariant::String) {
                QVariantMap inner = jsonToMap(aesDecryptBase64(m_secretKey, d.toString()).toUtf8());
                int iec = inner.value("error_code").toInt();
                if (iec != 0) {
                    error = inner.value("error_message").toString();
                    qWarning() << "[ZSticker]" << kind << "inner error_code" << iec << error;
                }
                data = inner.contains("data") ? inner.value("data") : QVariant(inner);
            } else {
                data = d;
            }
        }
    }
    static QSet<QString> logged;
    if (error.isEmpty() && !logged.contains(kind)) {
        logged.insert(kind);
        qDebug() << "[ZSticker]" << kind << "answer shape:" << shape(data).left(400);
    }

    if (kind == "cats") {
        if (!error.isEmpty()) { emit zaloStickerCategoriesReady(zaloStickerCategories(), error); return; }
        // list/v2: {dataUrl, cs, exCateIds, data:[...]}. Which of them carries the account's packs is not
        // documented (and `data` can come back empty), so: `data` first, then `exCateIds`, then the list
        // file behind `dataUrl` (see zaloStickerCatsFromEntries). The log gets the field types and sizes.
        QVariantList entries = firstList(data);
        QString dataUrl;
        if (data.type() == QVariant::Map) {
            QVariantMap dm = data.toMap();
            dataUrl = dm.value("dataUrl").toString();
            QStringList parts;
            for (QVariantMap::const_iterator it = dm.constBegin(); it != dm.constEnd(); ++it) {
                QString t = QString::fromLatin1(it.value().typeName() ? it.value().typeName() : "null");
                if (it.value().type() == QVariant::List)        t += QString("[%1]").arg(it.value().toList().size());
                else if (it.value().type() == QVariant::String) t += QString("(%1)").arg(it.value().toString().length());
                parts << it.key() + ":" + t;
            }
            qDebug() << "[ZSticker] list/v2 fields:" << parts.join(" ");
            if (entries.isEmpty()) entries = dm.value("exCateIds").toList();
        }
        zaloStickerCatsFromEntries(entries, dataUrl);
    } else if (kind == "catinfo") {
        int id = key.toInt();
        if (error.isEmpty() && data.type() == QVariant::Map && m_zsCatInfo.contains(id)) {
            QVariantMap m = data.toMap();
            m_zsCatInfo[id]["name"]  = m.value("name").toString();
            m_zsCatInfo[id]["count"] = (int)numField(m, "totalImage", "total", "count");
        }
        if (--m_zsCatPending <= 0) { m_zsCatPending = 0; zaloStickerCategoriesDone(); }
    } else if (kind == "catsurl") {
        // The pack list file behind list/v2's dataUrl: plain JSON, not the encrypted envelope.
        QVariantList entries;
        if (error.isEmpty()) {
            entries = jsonToList(raw);
            if (entries.isEmpty()) entries = firstList(jsonToMap(raw));
            qDebug() << "[ZSticker] dataUrl answer:" << raw.size() << "bytes," << entries.size() << "entries"
                     << (entries.isEmpty() ? QString() : shape(entries.first()).left(200));
        }
        zaloStickerCatsFromEntries(entries, QString());
    } else {
        // "list" (category stickers) or "search".
        QVariantList out;
        if (error.isEmpty()) {
            qint64 cateFallback = key.startsWith("c") ? key.mid(1).toLongLong() : 0;
            QVariantList entries = firstList(data);
            QSet<int> seen;
            for (int i = 0; i < entries.size(); ++i) {
                if (entries.at(i).type() != QVariant::Map) continue;
                QVariantMap item = stickerItem(entries.at(i).toMap(), cateFallback);
                if (item.isEmpty()) continue;
                int id = item.value("id").toInt();
                if (seen.contains(id)) continue;
                seen.insert(id);
                out.append(item);
            }
            if (out.isEmpty()) error = (kind == "search") ? "No stickers found" : "This pack has no stickers";
        }
        emit zaloStickerListReady(key, out, error);
    }
}

// Turns the entries of a category list (ids or objects) into the pack list: names come with the entries
// or from one category/detail request each. With no entries and a dataUrl, that file is fetched once and
// parsed the same way (dataUrl is only passed on the first pass).
void ZaloService::zaloStickerCatsFromEntries(const QVariantList &entries, const QString &dataUrl)
{
    QList<int> ids;
    m_zsCatInfo.clear();
    for (int i = 0; i < entries.size() && ids.size() < kMaxCategories; ++i) {
        const QVariant &e = entries.at(i);
        int id = 0;
        if (e.type() == QVariant::Map) {
            QVariantMap m = e.toMap();
            id = (int)numField(m, "cid", "id", "cateId");
            if (id > 0 && !m.value("name").toString().isEmpty()) {
                QVariantMap info;
                info["id"] = id;
                info["name"] = m.value("name").toString();
                info["count"] = (int)numField(m, "totalImage", "total", "count");
                m_zsCatInfo[id] = info;
            }
        } else {
            id = e.toInt();
            if (id == 0) id = (int)e.toDouble();
        }
        if (id > 0 && !ids.contains(id)) ids << id;
    }
    qDebug() << "[ZSticker] categories:" << ids.size();
    if (ids.isEmpty()) {
        if (!dataUrl.isEmpty() && dataUrl.startsWith("https://")) {
            QNetworkReply *r = m_manager->get(QNetworkRequest(QUrl(dataUrl)));
            r->setProperty("zsKind", "catsurl");
            r->setProperty("zsKey", QString());
            connect(r, SIGNAL(finished()), this, SLOT(onZaloStickerReply()));
            return;
        }
        emit zaloStickerCategoriesReady(QVariantList(), "No sticker packs found in your Zalo account");
        return;
    }
    // Names: one category/detail request per category we don't have a name for.
    QList<int> need;
    for (int i = 0; i < ids.size(); ++i) {
        if (!m_zsCatInfo.contains(ids[i])) {
            QVariantMap info; info["id"] = ids[i]; info["name"] = QString(); info["count"] = 0;
            m_zsCatInfo[ids[i]] = info;
            need << ids[i];
        }
        m_zsCatInfo[ids[i]]["order"] = i;
    }
    if (need.isEmpty()) { zaloStickerCategoriesDone(); return; }
    m_zsCatPending = need.size();
    for (int i = 0; i < need.size(); ++i)
        zaloStickerGet("/api/message/sticker/category/detail", QString("{\"cid\":%1}").arg(need[i]),
                       "catinfo", QString::number(need[i]));
}

void ZaloService::zaloStickerCategoriesDone()
{
    QList<QVariantMap> rows = m_zsCatInfo.values();
    QVariantList cats;
    for (int pos = 0; pos < rows.size() + 1 && cats.size() < rows.size(); ++pos)
        for (int i = 0; i < rows.size(); ++i)
            if (rows[i].value("order").toInt() == pos) {
                QVariantMap c = rows[i];
                c.remove("order");
                if (c.value("name").toString().isEmpty()) c["name"] = "Pack " + c.value("id").toString();
                cats.append(c);
            }
    m_zsCatInfo.clear();
    m_zaloStickerCats = cats;
    QSettings s("BerryLife", "Zalo10");
    s.setValue("zstickers/cats_" + m_uid, cats);
    emit zaloStickerCategoriesReady(cats, cats.isEmpty() ? QString("No sticker packs found") : QString());
}

// ---------------------------------------------------------------- sending

void ZaloService::sendZaloSticker(const QString &threadId, int stickerId, int cateId, int type, bool isGroup)
{
    if (!m_loggedIn || threadId.isEmpty() || stickerId <= 0) return;
    const QString clientId = QString::number(QDateTime::currentMSecsSinceEpoch());
    // Hand-built: ids must stay plain numbers (mapToJson goes through double / strings).
    QString params = QString("{\"stickerId\":%1,\"cateId\":%2,\"type\":%3,\"clientId\":%4,\"imei\":%5,\"zsource\":101,")
        .arg(stickerId).arg(cateId).arg(type > 0 ? type : 7).arg(clientId).arg(jsonQuote(m_imei));
    params += isGroup ? QString("\"grid\":%1}").arg(jsonQuote(threadId))
                      : QString("\"toid\":%1}").arg(jsonQuote(threadId));

    QString enc = aesEncryptBase64(m_secretKey, params);
    QByteArray body = "params=" + QUrl::toPercentEncoding(enc);
    QString base = isGroup ? m_groupServiceUrl + "/api/group/sticker"
                           : m_chatServiceUrl  + "/api/message/sticker";
    QString url = base + "?zpw_ver=" + QString::number(API_VERSION)
                       + "&zpw_type=" + QString::number(API_TYPE) + "&nretry=0";
    QNetworkRequest req = buildRequest(url, "https://chat.zalo.me/");
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    QNetworkReply *reply = m_manager->post(req, body);
    reply->setProperty("threadId", threadId);
    reply->setProperty("isGroup", isGroup);
    reply->setProperty("stickerId", stickerId);
    reply->setProperty("cliMsgId", clientId);
    connect(reply, SIGNAL(finished()), this, SLOT(onZaloStickerSent()));
    downloadSticker(QString::number(stickerId));        // warm the picture for our own bubble
}

void ZaloService::onZaloStickerSent()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    bool hasError    = (reply->error() != QNetworkReply::NoError);
    QString tid      = reply->property("threadId").toString();
    bool isGroup     = reply->property("isGroup").toBool();
    int stickerId    = reply->property("stickerId").toInt();
    QString cliMsgId = reply->property("cliMsgId").toString();
    QByteArray raw   = reply->readAll();
    reply->deleteLater();

    if (!hasError) {
        QVariantMap outer = jsonToMap(raw);
        if (outer.value("error_code").toInt() == 0) {
            QVariantMap decOuter = jsonToMap(aesDecryptBase64(m_secretKey, outer.value("data").toString()).toUtf8());
            if (decOuter.value("error_code").toInt() != 0) {
                hasError = true;
                qWarning() << "[ZSticker] send inner error_code" << decOuter.value("error_code").toInt()
                           << decOuter.value("error_message").toString();
            } else {
                QVariantMap data = decOuter.value("data").toMap();
                qint64 msgIdInt = data.value("msgId").toLongLong();
                if (msgIdInt == 0) msgIdInt = (qint64)data.value("msgId").toDouble();
                QString msgId = msgIdInt != 0 ? QString::number(msgIdInt)
                                              : QString::number(QDateTime::currentMSecsSinceEpoch());
                if (!m_seenMsgIds.contains(msgId)) {
                    QVariantMap out;
                    out["msgId"]    = msgId;
                    out["cliMsgId"] = cliMsgId;
                    out["content"]  = QString("{\"stickerId\":%1}").arg(stickerId);
                    out["msgType"]  = 5;
                    out["isMine"]   = true;
                    out["isGroup"]  = isGroup;
                    out["senderId"] = m_uid;
                    out["dName"]    = m_displayName;
                    out["ts"]       = QString::number(QDateTime::currentMSecsSinceEpoch());
                    m_seenMsgIds.insert(msgId);
                    dbSaveMessage(out, tid);
                    emit newMessage(tid, out);
                    hubNoteOutgoing(tid, 5, out["content"].toString());
                }
            }
        } else {
            hasError = true;
            qWarning() << "[ZSticker] send error_code" << outer.value("error_code").toInt()
                       << outer.value("error_message").toString();
        }
    } else {
        qWarning() << "[ZSticker] send network error" << reply->errorString();
    }
    emit messageSent(!hasError, tid);
}
