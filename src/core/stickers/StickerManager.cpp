#include "core/stickers/StickerManager.hpp"

#include <QScriptEngine>
#include <QScriptValue>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QSettings>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUrl>
#include <QDateTime>
#include <QDebug>

#include "utils/json/ZaloMessageJson.hpp"
#include "utils/ZaloNetworkManager.hpp"

// GitHub raw first, over the app's OpenSSL path (ZaloNetworkAccessManager; Qt's own TLS fails the
// handshake with it). jsDelivr is only the fallback: it answers 403 for files it has not cached yet
// because the whole repo is over its size limit.
static const char *PRIMARY_BASE  = "https://raw.githubusercontent.com/BBerryLife/BBerryLife.github.io/main/Data/";
static const char *FALLBACK_BASE = "https://cdn.jsdelivr.net/gh/BBerryLife/BBerryLife.github.io@main/Data/";
static const char *CATALOG_REL  = "Zalo10-stickers.json";
static const char *COMPLETE_MARK = ".complete";

static QString enc(const QString &s)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(s));
}

// Same TLS setup as the update check (ApplicationUI_Update.cpp): the bundled OpenSSL is too old
// for GitHub's CDN with the default protocol, so negotiate whatever both sides support.
static QNetworkRequest makeRequest(const QString &encodedUrl)
{
    QNetworkRequest req(QUrl::fromEncoded(encodedUrl.toLatin1()));
    QSslConfiguration ssl = req.sslConfiguration();
    ssl.setPeerVerifyMode(QSslSocket::VerifyNone);
    ssl.setProtocol(QSsl::AnyProtocol);
    req.setSslConfiguration(ssl);
    req.setRawHeader("User-Agent", "Zalo10-BB10");
    req.setRawHeader("Accept", "*/*");
    return req;
}

static bool writeFileAtomic(const QString &path, const QByteArray &data)
{
    QString tmp = path + ".part";
    QFile f(tmp);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    if (f.write(data) != data.size()) { f.close(); QFile::remove(tmp); return false; }
    f.close();
    if (QFile::exists(path)) QFile::remove(path);
    return QFile::rename(tmp, path);
}

static QString extOf(const QString &name)
{
    int dot = name.lastIndexOf('.');
    return dot < 0 ? QString() : name.mid(dot + 1).toLower();
}

static bool isImageName(const QString &name)
{
    QString e = extOf(name);
    return e == "png" || e == "gif" || e == "jpg" || e == "jpeg" || e == "webp";
}

StickerManager::StickerManager(QObject *parent)
    : QObject(parent), m_net(new ZaloNetworkAccessManager(this)), m_reply(0), m_busy(false),
      m_catalogLoaded(false)
{
    m_current.kind = JobCatalog;
    m_current.mirror = false;
    loadCatalogFromCache();
}

// ---------------------------------------------------------------- paths

QString StickerManager::rootDir()    { return QDir::homePath() + "/stickers"; }
QString StickerManager::previewDir() { return rootDir() + "/_preview"; }
QString StickerManager::packDir(const QString &packId) { return rootDir() + "/" + packId; }
QString StickerManager::browseDir(const QString &packId) { return previewDir() + "/pk_" + packId; }

// Names come from the downloaded manifest: never let them leave the sticker folder.
bool StickerManager::isSafeName(const QString &name)
{
    if (name.isEmpty() || name.length() > 120) return false;
    if (name.contains('/') || name.contains('\\') || name.contains(':')) return false;
    if (name.startsWith('.') || name.startsWith('_')) return false;
    return true;
}

bool StickerManager::isGifName(const QString &name) { return extOf(name) == "gif"; }

QString StickerManager::urlFor(const QString &relPath, bool mirror)
{
    return QString::fromLatin1(mirror ? FALLBACK_BASE : PRIMARY_BASE) + relPath;
}

void StickerManager::removeDirRecursive(const QString &path)
{
    QDir d(path);
    if (!d.exists()) return;
    QFileInfoList entries = d.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden);
    for (int i = 0; i < entries.size(); ++i) {
        if (entries.at(i).isDir()) removeDirRecursive(entries.at(i).absoluteFilePath());
        else QFile::remove(entries.at(i).absoluteFilePath());
    }
    QDir().rmdir(path);
}

// ---------------------------------------------------------------- installed packs

QStringList StickerManager::installedIds() const
{
    QStringList out;
    QDir root(rootDir());
    QStringList dirs = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (int i = 0; i < dirs.size(); ++i) {
        if (!isSafeName(dirs.at(i))) continue;   // also skips "_preview"
        if (QFile::exists(packDir(dirs.at(i)) + "/" + COMPLETE_MARK)) out.append(dirs.at(i));
    }
    return out;
}

QStringList StickerManager::orderedInstalledIds() const
{
    QStringList installed = installedIds();
    QSettings s("BerryLife", "Zalo10");
    QStringList saved = s.value("stickers/order").toStringList();

    QStringList out;
    for (int i = 0; i < saved.size(); ++i) {
        if (installed.contains(saved.at(i)) && !out.contains(saved.at(i))) out.append(saved.at(i));
    }
    // Packs installed after the order was last saved go at the end, A-Z.
    QMap<QString, QString> rest;
    for (int i = 0; i < installed.size(); ++i) {
        if (!out.contains(installed.at(i))) rest.insert(installed.at(i).toLower(), installed.at(i));
    }
    for (QMap<QString, QString>::const_iterator it = rest.constBegin(); it != rest.constEnd(); ++it)
        out.append(it.value());
    return out;
}

bool StickerManager::isInstalled(const QString &packId) const
{
    return isSafeName(packId) && QFile::exists(packDir(packId) + "/" + COMPLETE_MARK);
}

bool StickerManager::isInstalling(const QString &packId) const
{
    return m_installing.contains(packId);
}

QVariantList StickerManager::stickersOf(const QString &packId) const
{
    QVariantList out;
    if (!isSafeName(packId)) return out;
    QString dir = packDir(packId);

    QStringList names;
    if (m_files.contains(packId)) {
        QStringList listed = m_files.value(packId);
        for (int i = 0; i < listed.size(); ++i) {
            if (QFile::exists(dir + "/" + listed.at(i))) names.append(listed.at(i));
        }
    } else {
        QStringList all = QDir(dir).entryList(QDir::Files, QDir::Name);
        for (int i = 0; i < all.size(); ++i) {
            if (isImageName(all.at(i))) names.append(all.at(i));
        }
    }
    for (int i = 0; i < names.size(); ++i) {
        QVariantMap m;
        QString path = dir + "/" + names.at(i);
        m["path"]  = path;
        m["url"]   = "file://" + path;
        m["file"]  = names.at(i);
        m["isGif"] = isGifName(names.at(i));
        out.append(m);
    }
    return out;
}

QVariantList StickerManager::packStickers(const QString &packId) const
{
    if (!isSafeName(packId)) return QVariantList();
    if (isInstalled(packId)) return stickersOf(packId);

    QVariantList out;
    QStringList names = m_files.value(packId);
    QString dir = browseDir(packId);
    for (int i = 0; i < names.size(); ++i) {
        QString path = dir + "/" + names.at(i);
        QFileInfo fi(path);
        bool have = fi.exists() && fi.size() > 0;
        QVariantMap m;
        m["path"]  = have ? path : QString();
        m["url"]   = have ? QString("file://" + path) : QString();
        m["file"]  = names.at(i);
        m["isGif"] = isGifName(names.at(i));
        out.append(m);
    }
    return out;
}

void StickerManager::browsePack(const QString &packId)
{
    if (!isSafeName(packId) || isInstalled(packId) || !m_files.contains(packId)) return;
    cancelBrowse(packId);   // never queue a file twice

    QString dir = browseDir(packId);
    QDir().mkpath(dir);
    QStringList names = m_files.value(packId);
    QList<Job> jobs;
    for (int i = 0; i < names.size(); ++i) {
        QString dest = dir + "/" + names.at(i);
        QFileInfo fi(dest);
        if (fi.exists() && fi.size() > 0) continue;
        if (m_busy && m_current.kind == JobBrowse && m_current.packId == packId && m_current.file == names.at(i)) continue;
        Job j;
        j.kind   = JobBrowse;
        j.packId = packId;
        j.file   = names.at(i);
        j.dest   = dest;
        j.mirror = false;
        jobs.append(j);
    }
    if (jobs.isEmpty()) return;
    enqueueFront(jobs);
    startNext();
}

void StickerManager::cancelBrowse(const QString &packId)
{
    QList<Job> keep;
    for (int i = 0; i < m_queue.size(); ++i) {
        if (!(m_queue.at(i).kind == JobBrowse && m_queue.at(i).packId == packId)) keep.append(m_queue.at(i));
    }
    m_queue = keep;
}

QString StickerManager::firstStickerPath(const QString &packId) const
{
    QVariantList l = stickersOf(packId);
    if (l.isEmpty()) return QString();
    return l.at(0).toMap().value("url").toString();
}

QVariantList StickerManager::installedPacks() const
{
    QVariantList out;
    QSettings s("BerryLife", "Zalo10");
    QStringList disabled = s.value("stickers/disabled").toStringList();
    QStringList ids = orderedInstalledIds();
    for (int i = 0; i < ids.size(); ++i) {
        QVariantMap m;
        m["id"]      = ids.at(i);
        m["name"]    = ids.at(i);
        m["enabled"] = !disabled.contains(ids.at(i));
        m["thumb"]   = firstStickerPath(ids.at(i));
        m["count"]   = stickersOf(ids.at(i)).size();
        out.append(m);
    }
    return out;
}

QVariantList StickerManager::enabledPacks() const
{
    QVariantList all = installedPacks();
    QVariantList out;
    for (int i = 0; i < all.size(); ++i) {
        if (all.at(i).toMap().value("enabled").toBool()) out.append(all.at(i));
    }
    return out;
}

void StickerManager::setPackEnabled(const QString &packId, bool enabled)
{
    if (!isInstalled(packId)) return;
    QSettings s("BerryLife", "Zalo10");
    QStringList disabled = s.value("stickers/disabled").toStringList();
    if (enabled) disabled.removeAll(packId);
    else if (!disabled.contains(packId)) disabled.append(packId);
    s.setValue("stickers/disabled", disabled);
    s.sync();
    emit installedChanged();
}

void StickerManager::savePackOrder(const QVariantList &orderedIds)
{
    QStringList installed = installedIds();
    QStringList order;
    for (int i = 0; i < orderedIds.size(); ++i) {
        QString id = orderedIds.at(i).toString();
        if (installed.contains(id) && !order.contains(id)) order.append(id);
    }
    QSettings s("BerryLife", "Zalo10");
    s.setValue("stickers/order", order);
    s.sync();
    emit installedChanged();
}

void StickerManager::removePack(const QString &packId)
{
    if (!isSafeName(packId) || m_installing.contains(packId)) return;
    removeDirRecursive(packDir(packId));

    QSettings s("BerryLife", "Zalo10");
    QStringList order = s.value("stickers/order").toStringList();
    QStringList disabled = s.value("stickers/disabled").toStringList();
    order.removeAll(packId);
    disabled.removeAll(packId);
    s.setValue("stickers/order", order);
    s.setValue("stickers/disabled", disabled);
    s.sync();

    emit packRemoved(packId);
    emit installedChanged();
    queuePreviews();   // the Shop card needs its preview again
}

// ---------------------------------------------------------------- catalog

QString StickerManager::previewPathFor(const QString &packId) const
{
    if (isInstalled(packId)) {
        QString p = firstStickerPath(packId);
        if (!p.isEmpty()) return p;
    }
    const char *exts[] = { "png", "gif", "jpg", "jpeg", "webp" };
    for (int i = 0; i < 5; ++i) {
        QString p = previewDir() + "/" + packId + "." + exts[i];
        if (QFileInfo(p).exists() && QFileInfo(p).size() > 0) return "file://" + p;
    }
    return QString();
}

bool StickerManager::catalogLoaded() const { return m_catalogLoaded; }

QVariantList StickerManager::catalog() const
{
    QVariantList out;
    for (int i = 0; i < m_catalog.size(); ++i) {
        QVariantMap m = m_catalog.at(i).toMap();
        QString id = m.value("id").toString();
        m["installed"]  = isInstalled(id);
        m["installing"] = m_installing.contains(id);
        m["preview"]    = previewPathFor(id);
        out.append(m);
    }
    return out;
}

bool StickerManager::applyCatalogJson(const QByteArray &raw)
{
    QVariantMap root = jsonToMap(raw);
    QVariantList packs = root.value("packs").toList();
    if (packs.isEmpty()) return false;

    QVariantList cat;
    QMap<QString, QStringList> files;
    for (int i = 0; i < packs.size(); ++i) {
        QVariantMap p = packs.at(i).toMap();
        QString id = p.value("id").toString();
        if (!isSafeName(id)) continue;

        QStringList fl;
        QVariantList fv = p.value("files").toList();
        for (int j = 0; j < fv.size(); ++j) {
            QString name = fv.at(j).toString();
            if (isSafeName(name) && isImageName(name)) fl.append(name);
        }
        if (fl.isEmpty()) continue;

        QVariantMap c;
        c["id"]       = id;
        c["name"]     = p.value("name", id).toString();
        c["count"]    = fl.size();
        c["size"]     = p.value("size").toLongLong();
        c["animated"] = p.value("animated").toBool();
        cat.append(c);
        files.insert(id, fl);
    }
    if (cat.isEmpty()) return false;

    m_catalog = cat;
    m_files = files;
    m_catalogLoaded = true;
    return true;
}

void StickerManager::loadCatalogFromCache()
{
    QFile f(rootDir() + "/catalog.json");
    if (!f.open(QIODevice::ReadOnly)) return;
    QByteArray raw = f.readAll();
    f.close();
    applyCatalogJson(raw);
}

void StickerManager::fetchCatalog()
{
    for (int i = 0; i < m_queue.size(); ++i) {
        if (m_queue.at(i).kind == JobCatalog) return;   // already queued
    }
    if (m_busy && m_current.kind == JobCatalog) return;

    Job j;
    j.kind = JobCatalog;
    j.mirror = false;
    QList<Job> one;
    one.append(j);
    enqueueFront(one);
    startNext();
}

void StickerManager::queuePreviews()
{
    QDir().mkpath(previewDir());
    for (int i = 0; i < m_catalog.size(); ++i) {
        QString id = m_catalog.at(i).toMap().value("id").toString();
        if (isInstalled(id) || !previewPathFor(id).isEmpty()) continue;
        QStringList fl = m_files.value(id);
        if (fl.isEmpty()) continue;

        bool queued = false;
        for (int q = 0; q < m_queue.size(); ++q) {
            if (m_queue.at(q).kind == JobPreview && m_queue.at(q).packId == id) { queued = true; break; }
        }
        if (queued || (m_busy && m_current.kind == JobPreview && m_current.packId == id)) continue;

        Job j;
        j.kind   = JobPreview;
        j.packId = id;
        j.file   = fl.at(0);
        j.dest   = previewDir() + "/" + id + "." + extOf(fl.at(0));
        j.mirror = false;
        m_queue.append(j);
    }
    startNext();
}

// ---------------------------------------------------------------- install

void StickerManager::installPack(const QString &packId)
{
    if (!isSafeName(packId) || isInstalled(packId) || m_installing.contains(packId)) return;
    if (!m_files.contains(packId)) {
        emit packFailed(packId, "Sticker list is not loaded yet. Reopen the Shop and try again.");
        return;
    }

    QString dir = packDir(packId);
    QDir().mkpath(dir);

    QStringList files = m_files.value(packId);
    QList<Job> jobs;
    int already = 0;
    for (int i = 0; i < files.size(); ++i) {
        QString dest = dir + "/" + files.at(i);
        QFileInfo fi(dest);
        if (fi.exists() && fi.size() > 0) { ++already; continue; }   // left over from an interrupted install
        Job j;
        j.kind   = JobInstall;
        j.packId = packId;
        j.file   = files.at(i);
        j.dest   = dest;
        j.mirror = false;
        jobs.append(j);
    }

    m_installing.insert(packId);
    m_installTotal[packId] = files.size();
    m_installDone[packId]  = already;
    emit packProgress(packId, already, files.size());

    if (jobs.isEmpty()) {
        QFile mark(dir + "/" + COMPLETE_MARK);
        if (mark.open(QIODevice::WriteOnly)) mark.close();
        m_installing.remove(packId);
        emit packInstalled(packId);
        emit installedChanged();
        return;
    }
    enqueueFront(jobs);
    startNext();
}

void StickerManager::failPack(const QString &packId, const QString &error)
{
    QList<Job> keep;
    for (int i = 0; i < m_queue.size(); ++i) {
        if (!(m_queue.at(i).kind == JobInstall && m_queue.at(i).packId == packId)) keep.append(m_queue.at(i));
    }
    m_queue = keep;
    m_installing.remove(packId);
    emit packFailed(packId, error);
}

// ---------------------------------------------------------------- queue

void StickerManager::enqueueFront(const QList<Job> &jobs)
{
    m_queue = jobs + m_queue;
}

void StickerManager::startNext()
{
    if (m_busy || m_queue.isEmpty()) return;
    Job j = m_queue.takeFirst();
    startJob(j);
}

void StickerManager::startJob(const Job &job)
{
    m_current = job;
    m_busy = true;

    QString rel;
    if (job.kind == JobCatalog) {
        rel = QString::fromLatin1(CATALOG_REL) + "?t=" + QString::number(QDateTime::currentMSecsSinceEpoch());
    } else {
        rel = "Zalo10/" + enc(job.packId) + "/" + enc(job.file);
    }
    m_reply = m_net->get(makeRequest(urlFor(rel, job.mirror)));
    connect(m_reply, SIGNAL(sslErrors(QList<QSslError>)), this, SLOT(onSslErrors(QList<QSslError>)));
    connect(m_reply, SIGNAL(finished()), this, SLOT(onJobFinished()));
}

void StickerManager::onSslErrors(const QList<QSslError> &errors)
{
    Q_UNUSED(errors);
    QNetworkReply *r = qobject_cast<QNetworkReply*>(sender());
    if (r) r->ignoreSslErrors();
}

void StickerManager::onJobFinished()
{
    QNetworkReply *r = qobject_cast<QNetworkReply*>(sender());
    if (!r) return;
    if (r != m_reply) { r->deleteLater(); return; }
    m_reply = 0;

    int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    bool ok = (r->error() == QNetworkReply::NoError) && (status == 0 || status == 200);
    QByteArray data = r->readAll();
    QString err = r->errorString();
    r->deleteLater();
    if (ok && data.isEmpty()) { ok = false; err = "Empty response"; }

    if (!ok && !m_current.mirror) {
        qWarning() << "[Stickers] download failed on GitHub raw, retrying via jsDelivr:" << m_current.file << err;
        Job retry = m_current;
        retry.mirror = true;
        startJob(retry);
        return;
    }
    finishCurrent(ok, data, err);
}

void StickerManager::finishCurrent(bool ok, const QByteArray &data, const QString &error)
{
    Job job = m_current;
    m_busy = false;

    if (job.kind == JobCatalog) {
        if (ok && applyCatalogJson(data)) {
            QDir().mkpath(rootDir());
            writeFileAtomic(rootDir() + "/catalog.json", data);
            emit catalogReady();
            queuePreviews();
        } else {
            emit catalogFailed(ok ? QString("The sticker list is invalid.") : error);
        }
    } else if (job.kind == JobPreview) {
        if (ok && writeFileAtomic(job.dest, data)) emit previewReady(job.packId, "file://" + job.dest);
    } else if (job.kind == JobBrowse) {
        if (ok && writeFileAtomic(job.dest, data)) emit browseReady(job.packId, job.file, "file://" + job.dest);
    } else if (m_installing.contains(job.packId)) {
        if (!ok) {
            failPack(job.packId, error);
        } else if (!writeFileAtomic(job.dest, data)) {
            failPack(job.packId, "Could not save the sticker to storage.");
        } else {
            int done = m_installDone.value(job.packId) + 1;
            int total = m_installTotal.value(job.packId);
            m_installDone[job.packId] = done;
            emit packProgress(job.packId, done, total);
            if (done >= total) {
                QFile mark(packDir(job.packId) + "/" + COMPLETE_MARK);
                if (mark.open(QIODevice::WriteOnly)) mark.close();
                m_installing.remove(job.packId);
                removeDirRecursive(browseDir(job.packId));
                emit packInstalled(job.packId);
                emit installedChanged();
            }
        }
    }
    startNext();
}
