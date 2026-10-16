#include "applicationui.hpp"
#include "ServiceHandoff.hpp"
#include "ZaloService.hpp"
#include "ZaloServiceUtils.hpp"
#include "ActiveFrameCover.hpp"

#include <bb/cascades/Application>
#include <bb/cascades/QmlDocument>
#include <bb/cascades/AbstractPane>
#include <bb/cascades/LocaleHandler>
#include <bb/cascades/ThemeSupport>
#include <bb/system/InvokeManager>
#include <bb/system/InvokeRequest>
#include <bb/system/InvokeTargetReply>
#include <bb/system/InvokeQueryTargetsRequest>
#include <bb/system/InvokeQueryTargetsReply>
#include <bb/system/InvokeAction>
#include <bb/system/InvokeTarget>
#include <bb/system/Clipboard>
#include <bb/ApplicationInfo>
#include <bb/pim/calendar/CalendarService>
#include <bb/pim/calendar/CalendarEvent>
#include <bb/pim/calendar/CalendarFolder>
#include <bb/pim/calendar/Result>

#include <QTranslator>
#include <QLocale>
#include <QCoreApplication>
#include <QSettings>
#include <QDebug>
#include <QFile>
#include <QDir>
#include <QDateTime>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QStringList>
#include <QRegExp>
#include <QVariant>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QSslError>
#include <QList>

#include <unistd.h>

using namespace bb::cascades;
using namespace bb::system;

static const char *VERSION_MANIFEST_URL =
    "https://raw.githubusercontent.com/BBerryLife/BBerryLife.github.io/main/Data/Zalo10-version.json";

static bool isVersionNewer(const QString &a, const QString &b)
{
    QStringList pa = a.split('.');
    QStringList pb = b.split('.');
    int n = qMax(pa.size(), pb.size());
    for (int i = 0; i < n; ++i) {
        int va = (i < pa.size()) ? pa.at(i).toInt() : 0;
        int vb = (i < pb.size()) ? pb.at(i).toInt() : 0;
        if (va != vb) return va > vb;
    }
    return false;
}

static QString mimeTypeForImagePath(const QString &path)
{
    QString lower = path.toLower();
    if (lower.endsWith(".png"))                     return "image/png";
    if (lower.endsWith(".gif"))                     return "image/gif";
    if (lower.endsWith(".webp"))                     return "image/webp";
    if (lower.endsWith(".jpg") || lower.endsWith(".jpeg")) return "image/jpeg";
    return "image/jpeg";
}

static QString mimeTypeForVideoPath(const QString &path)
{
    QString lower = path.toLower();
    if (lower.endsWith(".3gp"))  return "video/3gpp";
    if (lower.endsWith(".mov"))  return "video/quicktime";
    if (lower.endsWith(".mkv"))  return "video/x-matroska";
    if (lower.endsWith(".pdf"))  return "application/pdf";
    if (lower.endsWith(".docx")) return "application/vnd.openxmlformats-officedocument.wordprocessingml.document";
    if (lower.endsWith(".doc"))  return "application/msword";
    if (lower.endsWith(".pptx")) return "application/vnd.openxmlformats-officedocument.presentationml.presentation";
    if (lower.endsWith(".ppt"))  return "application/vnd.ms-powerpoint";
    if (lower.endsWith(".xlsx")) return "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet";
    if (lower.endsWith(".xls"))  return "application/vnd.ms-excel";
    if (lower.endsWith(".txt"))  return "text/plain";
    if (lower.endsWith(".epub")) return "application/epub+zip";
    if (lower.endsWith(".apk"))  return "application/vnd.android.package-archive";
    if (lower.endsWith(".cer"))  return "application/x-x509-ca-cert";
    if (lower.endsWith(".zip"))  return "application/zip";
    if (lower.endsWith(".rar"))  return "application/x-rar-compressed";
    if (lower.endsWith(".7z"))   return "application/x-7z-compressed";
    if (lower.endsWith(".vcf"))  return "text/x-vcard";
    if (lower.endsWith(".mp3"))  return "audio/mpeg";
    if (lower.endsWith(".flac")) return "audio/flac";
    if (lower.endsWith(".m4a"))  return "audio/mp4";
    if (lower.endsWith(".bar"))  return "application/octet-stream";
    return "video/mp4";
}

static QString toLocalFilePath(const QString &pathOrUri)
{
    if (pathOrUri.startsWith("file://"))
        return pathOrUri.mid(7);
    return pathOrUri;
}

// Qt4 has no toHtmlEscaped(); '&' must be replaced first to avoid double escaping.
static QString escapeHtmlQt4(const QString &in)
{
    QString out = in;
    out.replace('&',  "&amp;");
    out.replace('<',  "&lt;");
    out.replace('>',  "&gt;");
    out.replace('"',  "&quot;");
    return out;
}

ApplicationUI::ApplicationUI() : QObject(), m_zService(NULL), m_updateManager(NULL), m_exitHandled(false), m_pendingShareMimeType("text/plain")
{
    m_pInvokeManager = new InvokeManager(this);
    bool invokeConnectOk = QObject::connect(
        m_pInvokeManager, SIGNAL(invoked(const bb::system::InvokeRequest&)),
        this,             SLOT(onInvoked(const bb::system::InvokeRequest&)));
    m_pTranslator    = new QTranslator(this);
    m_pLocaleHandler = new LocaleHandler(this);

    bool res = QObject::connect(
        m_pLocaleHandler, SIGNAL(systemLanguageChanged()),
        this,             SLOT(onSystemLanguageChanged()));
    Q_ASSERT(res); Q_UNUSED(res);
    onSystemLanguageChanged();

    ZaloService *zService = new ZaloService(this);
    m_zService = zService;

    ServiceHandoff::markUiAlive();
    pingHandoffService(ServiceHandoff::ACTION_UI_OPENED);

    QObject::connect(Application::instance(), SIGNAL(manualExit()),
                     this, SLOT(onManualExit()));
    QObject::connect(Application::instance(), SIGNAL(fullscreen()),
                     this, SLOT(onAppFullscreen()));
    QObject::connect(Application::instance(), SIGNAL(thumbnail()),
                     this, SLOT(onAppNotFullscreen()));
    QObject::connect(Application::instance(), SIGNAL(invisible()),
                     this, SLOT(onAppNotFullscreen()));
    // manualExit() doesn't fire when the app is swiped closed, so also hook aboutToQuit().
    QObject::connect(QCoreApplication::instance(), SIGNAL(aboutToQuit()),
                     this, SLOT(onManualExit()));

    {
        QSettings s("BerryLife", "Zalo10");
        if (s.value("darkTheme", false).toBool())
            Application::instance()->themeSupport()->setVisualStyle(VisualStyle::Dark);
    }

    QmlDocument *qml = QmlDocument::create("asset:///main.qml").parent(this);
    qml->setContextProperty("app",      this);
    qml->setContextProperty("zService", zService);

    AbstractPane *root = qml->createRootObject<AbstractPane>();
    Application::instance()->setScene(root);

    ActiveFrameCover *cover = new ActiveFrameCover();
    Application::instance()->setCover(cover);
}

void ApplicationUI::invokeEmail(const QString &to, const QString &subject)
{
    InvokeRequest req;
    req.setTarget("sys.pim.uib.email.hybridcomposer");
    req.setAction("bb.action.COMPOSE");
    req.setMimeType("message/rfc822");
    req.setUri(QString("mailto:%1?subject=%2").arg(to).arg(subject));
    m_pInvokeManager->invoke(req);
}

void ApplicationUI::createTodayEvent(const QString &subject, const QString &body, int durationMinutes)
{
    using namespace bb::pim::calendar;

    CalendarService calSvc;
    Result::Type folderResult = Result::Success;
    QPair<AccountId, FolderId> defFolder = calSvc.defaultCalendarFolder(&folderResult);
    if (folderResult != Result::Success) {
        qWarning() << "[App] createTodayEvent: defaultCalendarFolder() failed, result=" << folderResult;
        emit eventCreated(false, "Could not retrieve the device's default calendar");
        return;
    }

    QDateTime start = QDateTime::currentDateTime();
    int minutes = (durationMinutes > 0) ? durationMinutes : 30;
    QDateTime end = start.addSecs(minutes * 60);

    CalendarEvent ev;
    ev.setSubject(subject);
    ev.setBody(body);
    ev.setStartTime(start);
    ev.setEndTime(end);
    ev.setAccountId(defFolder.first);
    ev.setFolderId(defFolder.second);

    Result::Type createResult = calSvc.createEvent(ev);
    bool ok = (createResult == Result::Success);
    emit eventCreated(ok, ok ? QString() : QString("Error creating event, error code: %1").arg((int)createResult));
}

void ApplicationUI::copyToClipboard(const QString &text)
{
    bb::system::Clipboard clipboard;
    clipboard.clear();
    clipboard.insert("text/plain", text.toUtf8());
}

bool ApplicationUI::copyImageToClipboard(const QString &localPath)
{
    QString path = toLocalFilePath(localPath);
    QFile file(path);
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        qWarning() << "[App] copyImageToClipboard: cannot open" << path;
        return false;
    }
    QByteArray bytes = file.readAll();
    file.close();
    if (bytes.isEmpty()) {
        return false;
    }

    QString mime = mimeTypeForImagePath(path);
    bb::system::Clipboard clipboard;
    clipboard.clear();
    clipboard.insert(mime, bytes);
    return true;
}

void ApplicationUI::queryShareTargets(const QString &text)
{
    Q_UNUSED(text);
    m_pendingShareMimeType = "text/plain";
    InvokeQueryTargetsRequest req;
    req.setAction("bb.action.SHARE");
    req.setMimeType("text/plain");
    InvokeQueryTargetsReply *reply = m_pInvokeManager->queryTargets(req);
    connect(reply, SIGNAL(finished()), this, SLOT(onQueryTargetsFinished()));
}

void ApplicationUI::queryShareTargetsForImage(const QString &localPath)
{
    m_pendingShareMimeType = mimeTypeForImagePath(toLocalFilePath(localPath));
    InvokeQueryTargetsRequest req;
    req.setAction("bb.action.SHARE");
    req.setMimeType(m_pendingShareMimeType);
    InvokeQueryTargetsReply *reply = m_pInvokeManager->queryTargets(req);
    connect(reply, SIGNAL(finished()), this, SLOT(onQueryTargetsFinished()));
}

void ApplicationUI::onQueryTargetsFinished()
{
    InvokeQueryTargetsReply *reply = qobject_cast<InvokeQueryTargetsReply*>(sender());
    if (!reply) return;

    QString tmpDir = QDir::homePath() + "/tmp/icons/";
    QDir().mkpath(tmpDir);

    QStringList nativePfx;
    nativePfx << "sys." << "com.rim.";

    QVariantList bbmMain, bbmGroup, bbmChannel;
    QVariantList textList, emailList, meetingList, connList, rememberList, otherNatList, thirdList;

    foreach (const InvokeAction &action, reply->actions()) {
        foreach (const InvokeTarget &tgt, action.targets()) {
            QVariantMap m;
            m["label"]  = tgt.label();
            m["target"] = tgt.name();
            m["action"] = action.name();

            QString src = tgt.icon().toLocalFile();
            if (!src.isEmpty() && QFile::exists(src)) {
                QString dst = tmpDir + tgt.name().replace("/", "_") + ".png";
                if (!QFile::exists(dst)) QFile::copy(src, dst);
                m["icon"] = "file://" + dst;
            } else {
                m["icon"] = "";
            }

            bool isNat = false;
            QString name = tgt.name().toLower();
            foreach (const QString &pfx, nativePfx) {
                if (name.startsWith(pfx)) { isNat = true; break; }
            }
            m["isNative"] = isNat;

            if (isNat) {
                if (name.contains("bbgroups") || (name.contains("bbm") && name.contains("group")))
                    bbmGroup.append(m);
                else if (name.contains("channel") || name.contains("channels"))
                    bbmChannel.append(m);
                else if (name.contains("bbm"))
                    bbmMain.append(m);
                else if (name.contains("text") || name.contains("sms") || name.contains("mms"))
                    textList.append(m);
                else if (name.contains("email"))
                    emailList.append(m);
                else if (name.contains("meeting") || name.contains("calendar"))
                    meetingList.append(m);
                else if (name.contains("bluetooth") || name.contains("nfc"))
                    connList.append(m);
                else if (name.contains("remember"))
                    rememberList.append(m);
                else
                    otherNatList.append(m);
            } else {
                thirdList.append(m);
            }
        }
    }
    reply->deleteLater();

    QVariantList result;
    QList<QVariantList*> ordered;
    ordered << &bbmMain << &bbmGroup << &bbmChannel << &textList << &emailList
            << &meetingList << &connList << &rememberList << &otherNatList << &thirdList;
    foreach (QVariantList *lst, ordered)
        foreach (const QVariant &v, *lst)
            result.append(v);

    emit shareTargetsReady(result);
}

void ApplicationUI::invokeShareTarget(const QString &target, const QString &action, const QString &text)
{
    InvokeRequest req;
    req.setTarget(target);
    req.setAction(action.isEmpty() ? "bb.action.SHARE" : action);
    req.setMimeType("text/plain");
    req.setData(text.toUtf8());
    m_pInvokeManager->invoke(req);
}

void ApplicationUI::invokeShareTargetForImage(const QString &target, const QString &action, const QString &localPath)
{
    QString path = toLocalFilePath(localPath);
    QFile file(path);
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        qWarning() << "[App] invokeShareTargetForImage: cannot open" << path;
        return;
    }
    QByteArray bytes = file.readAll();
    file.close();

    InvokeRequest req;
    req.setTarget(target);
    req.setAction(action.isEmpty() ? "bb.action.SHARE" : action);
    req.setMimeType(mimeTypeForImagePath(path));
    req.setData(bytes);
    m_pInvokeManager->invoke(req);
}

void ApplicationUI::openLocalFile(const QString &localPath)
{
    QString path = toLocalFilePath(localPath);
    if (!QFile::exists(path)) {
        return;
    }

    InvokeRequest req;
    req.setTarget("");
    req.setAction("bb.action.OPEN");
    req.setMimeType(mimeTypeForVideoPath(path));
    req.setUri("file://" + path);
    m_pInvokeManager->invoke(req);
}

void ApplicationUI::minimizeApp()
{
    Application::instance()->minimize();
}

void ApplicationUI::setDarkTheme(bool dark)
{
    QSettings settings("BerryLife", "Zalo10");
    settings.setValue("darkTheme", dark);
    settings.sync();
    Application::instance()->themeSupport()->setVisualStyle(
        dark ? VisualStyle::Dark : VisualStyle::Bright);
    // ChatView reads getDarkTheme() once (Q_INVOKABLE, no NOTIFY), so emit a signal to
    // refresh open pages when the theme changes.
    emit darkThemeChanged(dark);
}

bool ApplicationUI::getDarkTheme()
{
    QSettings settings("BerryLife", "Zalo10");
    return settings.value("darkTheme", false).toBool();
}

void ApplicationUI::setShowRecalledMessages(bool show)
{
    QSettings settings("BerryLife", "Zalo10");
    settings.setValue("showRecalledMessages", show);
    settings.sync();
    emit showRecalledMessagesChanged(show);
}

bool ApplicationUI::getShowRecalledMessages()
{
    QSettings settings("BerryLife", "Zalo10");
    return settings.value("showRecalledMessages", false).toBool();
}

void ApplicationUI::onSystemLanguageChanged()
{
    QCoreApplication::instance()->removeTranslator(m_pTranslator);
    QString locale = QLocale().name();
    if (m_pTranslator->load(QString("Zalo10_%1").arg(locale), "app/native/qm"))
        QCoreApplication::instance()->installTranslator(m_pTranslator);
}

void ApplicationUI::pingHandoffService(const char *action)
{
    bb::system::InvokeTargetReply *reply = ServiceHandoff::pingService(m_pInvokeManager, action);
    if (!reply) {
        return;
    }
    connect(reply, SIGNAL(finished()), this, SLOT(onServicePingFinished()));
}

void ApplicationUI::onServicePingFinished()
{
    bb::system::InvokeTargetReply *reply = qobject_cast<bb::system::InvokeTargetReply *>(sender());
    if (!reply) return;
    // error() == 0 means Navigator accepted the invoke (even if it had to start the service).
    reply->deleteLater();
}

void ApplicationUI::onManualExit()
{
    if (m_exitHandled) return;
    m_exitHandled = true;

    if (m_zService && m_zService->loggedIn()) {
        m_zService->saveSession();
        m_zService->closeWebSocketGracefully();
    }
    ServiceHandoff::markUiGone();
    pingHandoffService(ServiceHandoff::ACTION_UI_CLOSED);
    bb::cascades::Application::instance()->quit();
}

void ApplicationUI::onTermSignal(int fd)
{
    char buf[16];
    while (::read(fd, buf, sizeof(buf)) > 0) {}

    qDebug() << "[App] SIGTERM received, running exit cleanup";
    onManualExit();
}

void ApplicationUI::onAppFullscreen()
{
    if (m_zService) m_zService->setAppForeground(true);
}

void ApplicationUI::onAppNotFullscreen()
{
    if (m_zService) m_zService->setAppForeground(false);
}

void ApplicationUI::onInvoked(const bb::system::InvokeRequest &request)
{

    // Two invoke sources: (1) legacy self-built request, data() = "threadId|isGroup" (kept as
    // fallback); (2) Hub item tap: data() is JSON {"attributes": {"sourceId": <threadId>, ...}},
    // uri() is always empty.
    QString rawData = QString::fromUtf8(request.data());
    QUrl    rawUri  = request.uri();

    QString threadId;
    bool    isGroup = false;

    QString trimmed = rawData.trimmed();
    if (trimmed.startsWith("{")) {
        QVariantMap root  = jsonToMap(rawData.toUtf8());
        QVariantMap attrs = root.value("attributes").toMap();
        threadId = attrs.value("sourceId").toString();
        if (threadId.isEmpty())
            threadId = attrs.value("messageid").toString();
        // The Hub payload has no isGroup flag: look it up from the state saved in HubIntegration
        // when the item was created.
        isGroup = m_zService ? m_zService->isGroupHubThread(threadId) : false;
    } else if (!trimmed.isEmpty()) {
        QStringList parts = trimmed.split("|");
        threadId = parts.value(0);
        isGroup  = (parts.value(1) == "1");
    } else if (!rawUri.isEmpty()) {
        QString path = rawUri.path();
        if (path.isEmpty()) path = rawUri.toString();
        QStringList segments = path.split(QRegExp("[/:]"), QString::SkipEmptyParts);
        threadId = segments.isEmpty() ? QString() : segments.last();

        if (rawUri.hasQueryItem("isGroup")) {
            isGroup = (rawUri.queryItemValue("isGroup") == "1");
        } else if (rawUri.hasQueryItem("is_group")) {
            isGroup = (rawUri.queryItemValue("is_group") == "1");
        }

    }

    if (threadId.isEmpty() && !rawUri.isEmpty()) {
        QString u = rawUri.toString();
        threadId = u.mid(u.lastIndexOf(QLatin1Char(':')) + 1);
        isGroup  = m_zService ? m_zService->isGroupHubThread(threadId) : false;
    }

    if (threadId.isEmpty()) {
        qWarning() << "[App] onInvoked: no threadId in data() or uri()";
        return;
    }

    if (m_zService && m_zService->handleHubAction(request.action(), threadId)) {
        return;
    }

    emit openThreadRequested(threadId, isGroup);
}

QString ApplicationUI::appVersion()
{
    return bb::ApplicationInfo().version();
}

QString ApplicationUI::exportLog()
{
    QString srcPath  = QDir::homePath() + "/zalo10_runtime.log";
    QString destDir  = "/accounts/1000/shared/documents/zalo10/log";
    QString stamp    = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString destPath = destDir + "/zalo10_log_" + stamp + ".txt";

    QDir dir;
    if (!dir.exists(destDir) && !dir.mkpath(destDir)) {
        qWarning() << "[App] exportLog: failed to create" << destDir;
        return QString();
    }

    if (!QFile::exists(srcPath)) {
        return QString();
    }

    bool ok = QFile::copy(srcPath, destPath);
    qWarning() << "[App] exportLog:" << (ok ? "success ->" : "FAILED ->") << destPath;
    return ok ? destPath : QString();
}

// Bundled OpenSSL is too old for TLS 1.2+ with the GitHub CDN: use AnyProtocol so it
// negotiates the highest common version.
static QNetworkRequest buildManifestRequest()
{
    QNetworkRequest req((QUrl(QString::fromLatin1(VERSION_MANIFEST_URL))));
    QSslConfiguration sslConf = req.sslConfiguration();
    sslConf.setPeerVerifyMode(QSslSocket::VerifyNone);
    sslConf.setProtocol(QSsl::AnyProtocol);
    req.setSslConfiguration(sslConf);
    req.setRawHeader("Accept", "application/json, text/plain, */*");
    req.setRawHeader("User-Agent", "Zalo10-BB10");
    return req;
}

void ApplicationUI::checkForUpdate()
{
    if (!m_updateManager) m_updateManager = new QNetworkAccessManager(this);
    QNetworkReply *reply = m_updateManager->get(buildManifestRequest());
    connect(reply, SIGNAL(sslErrors(const QList<QSslError>&)), this, SLOT(onManifestSslErrors(const QList<QSslError>&)));
    connect(reply, SIGNAL(finished()), this, SLOT(onUpdateCheckFetchDone()));
}

void ApplicationUI::onManifestSslErrors(const QList<QSslError> &errors)
{
    for (int i = 0; i < errors.size(); ++i)
        qWarning() << "[App] manifest SSL error:" << errors.at(i).errorString();
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (reply) reply->ignoreSslErrors();
}

void ApplicationUI::onUpdateCheckFetchDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    QByteArray raw = reply->readAll();
    QNetworkReply::NetworkError err = reply->error();
    reply->deleteLater();

    if (err != QNetworkReply::NoError) {
        qWarning() << "[App] checkForUpdate: network error" << err << "-" << reply->errorString();
        emit updateCheckResult(false, "", "", "Could not check for updates. Check your internet connection.");
        return;
    }

    QVariantMap root = jsonToMap(raw);
    QString latest      = root.value("latestVersion").toString();
    QString downloadUrl = root.value("downloadUrl").toString();
    if (latest.isEmpty()) {
        qWarning() << "[App] checkForUpdate: manifest missing/invalid";
        emit updateCheckResult(false, "", "", "Update info unavailable right now. Try again later.");
        return;
    }

    QString current = appVersion();
    bool isLatest = !isVersionNewer(latest, current);
    emit updateCheckResult(isLatest, latest, downloadUrl, "");
}

void ApplicationUI::fetchChangelog()
{
    if (!m_updateManager) m_updateManager = new QNetworkAccessManager(this);
    QNetworkReply *reply = m_updateManager->get(buildManifestRequest());
    connect(reply, SIGNAL(sslErrors(const QList<QSslError>&)), this, SLOT(onManifestSslErrors(const QList<QSslError>&)));
    connect(reply, SIGNAL(finished()), this, SLOT(onChangelogFetchDone()));
}

void ApplicationUI::onChangelogFetchDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    QByteArray raw = reply->readAll();
    QNetworkReply::NetworkError err = reply->error();
    reply->deleteLater();

    if (err != QNetworkReply::NoError) {
        qWarning() << "[App] fetchChangelog: network error" << err << "-" << reply->errorString();
        emit changelogReady("", "Could not load the changelog. Check your internet connection.");
        return;
    }

    QVariantMap root = jsonToMap(raw);
    QVariantList versions = root.value("changelog").toList();
    if (versions.isEmpty()) {
        emit changelogReady("", "Changelog is empty right now.");
        return;
    }

    emit changelogReady(buildChangelogHtml(versions), "");
}

QString ApplicationUI::buildChangelogHtml(const QVariantList &versions) const
{
    static const char *tags[] = { "NEW:", "IMPROVE:", "FIX:", "REMOVED:", 0 };

    QString html;
    html += "<html><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
            "<style>"
            "body{font-family:'Slate Pro','Helvetica Neue',Helvetica,sans-serif;"
            "margin:0;padding:18px;color:#1a1a1a;background:#ffffff;font-size:16px;}"
            "h2{font-size:20px;font-weight:700;margin:0 0 14px 0;padding-bottom:8px;"
            "border-bottom:2px solid #2575fc;}"
            "h2.notfirst{margin-top:30px;}"
            "ul{margin:0;padding-left:22px;}"
            "li{margin-bottom:12px;line-height:1.45;}"
            "b{font-weight:700;}"
            "</style></head><body>";

    for (int i = 0; i < versions.size(); ++i) {
        QVariantMap v = versions.at(i).toMap();
        QString ver = v.value("version").toString();
        QVariantList items = v.value("items").toList();

        html += QString("<h2%1>Version %2.x</h2><ul>")
                    .arg(i == 0 ? "" : " class=\"notfirst\"")
                    .arg(escapeHtmlQt4(ver));

        for (int j = 0; j < items.size(); ++j) {
            QString line = escapeHtmlQt4(items.at(j).toString());

            for (int t = 0; tags[t] != 0; ++t) {
                QString tag = QString::fromLatin1(tags[t]);
                if (line.startsWith(tag)) {
                    line = "<b>" + tag + "</b>" + line.mid(tag.length());
                    break;
                }
            }

            QStringList parts = line.split("**");
            QString rendered;
            for (int p = 0; p < parts.size(); ++p)
                rendered += (p % 2 == 1) ? ("<b>" + parts.at(p) + "</b>") : parts.at(p);

            html += "<li>" + rendered + "</li>";
        }
        html += "</ul>";
    }

    html += "</body></html>";
    return html;
}
