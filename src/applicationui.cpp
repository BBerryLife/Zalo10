#include "applicationui.hpp"
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
#include <QVariant>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QSslError>
#include <QList>

#include <unistd.h> // ::read() trong onTermSignal()

using namespace bb::cascades;
using namespace bb::system;

// JSON manifest dùng chung cho checkForUpdate() và fetchChangelog(). Release
// bản mới chỉ cần sửa file này trên web, không cần rebuild app. Format:
// { "latestVersion": "1.2.0", "downloadUrl": "...", "changelog": [...] }
static const char *VERSION_MANIFEST_URL =
    "https://raw.githubusercontent.com/BBerryLife/BBerryLife.github.io/main/Data/Zalo10-version.json";

// Compares dot-separated version strings numerically (so "1.10.0" > "1.9.0").
// Returns true if "a" is strictly newer than "b".
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

// Cheap extension-based MIME sniff — good enough for the handful of formats
// Zalo actually sends (jpg/png/gif/webp), no need to pull in a magic-byte lib.
static QString mimeTypeForImagePath(const QString &path)
{
    QString lower = path.toLower();
    if (lower.endsWith(".png"))                     return "image/png";
    if (lower.endsWith(".gif"))                     return "image/gif";
    if (lower.endsWith(".webp"))                     return "image/webp";
    if (lower.endsWith(".jpg") || lower.endsWith(".jpeg")) return "image/jpeg";
    return "image/jpeg"; // sensible default — this is what onImageMsgDownloaded transcodes most photos to
}

// Strips a "file://" prefix if present, leaving a bare filesystem path.
static QString toLocalFilePath(const QString &pathOrUri)
{
    if (pathOrUri.startsWith("file://"))
        return pathOrUri.mid(7);
    return pathOrUri;
}

// Qt4 has no QString::toHtmlEscaped() (that's Qt5+) — do it by hand.
// Order matters: '&' must be replaced first or it'd double-escape the
// entities just inserted for the other characters.
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
    QObject::connect(m_pInvokeManager, SIGNAL(invoked(const bb::system::InvokeRequest&)),
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

    QObject::connect(Application::instance(), SIGNAL(manualExit()),
                     this, SLOT(onManualExit()));
    // Lưới an toàn thứ 2: manualExit() của Cascades không bắn khi vuốt đóng
    // app, nên bắt thêm aboutToQuit() (signal Qt core chuẩn) cho chắc.
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

    // Set Active Frame cover using C++ class (no QML needed).
    // ActiveFrameCover selects the correct image based on DisplayInfo internally.
    ActiveFrameCover *cover = new ActiveFrameCover();
    Application::instance()->setCover(cover);
    qDebug() << "[App] ActiveFrameCover set";
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

// Tạo event thật trong calendar mặc định của máy qua CalendarService:
// lấy defaultCalendarFolder() để không phải hardcode account/folder id,
// build CalendarEvent rồi gọi createEvent(). Luôn là HÔM NAY — start/end
// tự tính từ giờ hiện tại, không nhận qua tham số. durationMinutes mặc
// định 30 phút nếu không truyền hoặc <= 0.
void ApplicationUI::createTodayEvent(const QString &subject, const QString &body, int durationMinutes)
{
    using namespace bb::pim::calendar;

    CalendarService calSvc;
    Result::Type folderResult = Result::Success;
    QPair<AccountId, FolderId> defFolder = calSvc.defaultCalendarFolder(&folderResult);
    if (folderResult != Result::Success) {
        qDebug() << "[App] createTodayEvent: defaultCalendarFolder() failed, result=" << folderResult;
        emit eventCreated(false, "Khong lay duoc lich mac dinh cua thiet bi");
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
    qDebug() << "[App] createTodayEvent: subject=" << subject << "start=" << start
              << "accountId=" << defFolder.first << "folderId=" << defFolder.second
              << "result=" << createResult << "ok=" << ok;
    emit eventCreated(ok, ok ? QString() : QString("Loi tao su kien, ma loi: %1").arg((int)createResult));
}

void ApplicationUI::copyToClipboard(const QString &text)
{
    bb::system::Clipboard clipboard;
    clipboard.clear();
    clipboard.insert("text/plain", text.toUtf8());
}

// Fix cho bug "copy ảnh chỉ copy được link": đọc thẳng bytes ảnh từ file
// cache local đã tải sẵn, rồi bỏ vào clipboard dưới MIME type image/*
// thay vì copy JSON text như trước.
bool ApplicationUI::copyImageToClipboard(const QString &localPath)
{
    QString path = toLocalFilePath(localPath);
    QFile file(path);
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        qDebug() << "[App] copyImageToClipboard: cannot open" << path;
        return false;
    }
    QByteArray bytes = file.readAll();
    file.close();
    if (bytes.isEmpty()) {
        qDebug() << "[App] copyImageToClipboard: empty file" << path;
        return false;
    }

    QString mime = mimeTypeForImagePath(path);
    bb::system::Clipboard clipboard;
    clipboard.clear();
    clipboard.insert(mime, bytes);
    qDebug() << "[App] copyImageToClipboard: copied" << bytes.size() << "bytes as" << mime << "from" << path;
    return true;
}

// Query app đăng ký bb.action.SHARE, sắp theo thứ tự ưu tiên: BBM contact
// -> BBM group -> BBM channel -> Text -> Email -> Meeting -> Bluetooth/NFC
// -> Remember -> app native khác -> app bên thứ ba.
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

// Giống queryShareTargets() nhưng query theo image/* thay vì text/plain,
// để các app chỉ đăng ký nhận ảnh cũng hiện ra trong picker.
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

// Giống invokeShareTarget() nhưng gửi bytes ảnh thật (đọc từ cache local)
// dưới MIME type image/*, thay vì text JSON như trước.
void ApplicationUI::invokeShareTargetForImage(const QString &target, const QString &action, const QString &localPath)
{
    QString path = toLocalFilePath(localPath);
    QFile file(path);
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        qDebug() << "[App] invokeShareTargetForImage: cannot open" << path;
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

void ApplicationUI::onManualExit()
{
    if (m_exitHandled) return; // manualExit() / aboutToQuit() / SIGTERM có thể trùng nhau
    m_exitHandled = true;

    if (m_zService && m_zService->loggedIn()) {
        qDebug() << "[App] manualExit: saving session";
        m_zService->saveSession();
        m_zService->closeWebSocketGracefully();
    }
    bb::cascades::Application::instance()->quit();
}

void ApplicationUI::onTermSignal(int fd)
{
    // Xả hết byte trong pipe (self-pipe trick từ signal handler SIGTERM, xem
    // main.cpp) — không bắt buộc vì process sắp thoát, nhưng dọn cho sạch.
    char buf[16];
    while (::read(fd, buf, sizeof(buf)) > 0) {}

    qDebug() << "[App] SIGTERM received, running exit cleanup";
    onManualExit();
}

void ApplicationUI::onInvoked(const bb::system::InvokeRequest &request)
{
    // Called when app is opened from Hub notification.
    // data format sent by ZaloService: "threadId|isGroup" (isGroup: 1=group, 0=DM)
    QString raw = QString::fromUtf8(request.data());
    qDebug() << "[App] onInvoked data:" << raw;
    if (raw.isEmpty()) return;

    QStringList parts = raw.split("|");
    QString threadId  = parts.value(0);
    bool    isGroup   = (parts.value(1) == "1");

    if (threadId.isEmpty()) return;

    emit openThreadRequested(threadId, isGroup);
}

QString ApplicationUI::appVersion()
{
    // Đọc version trực tiếp từ bar-descriptor.xml lúc runtime, nên update
    // version chỉ cần sửa file đó, không cần đổi code.
    return bb::ApplicationInfo().version();
}

QString ApplicationUI::exportLog()
{
    QString srcPath  = QDir::homePath() + "/zalo10_runtime.log";
    // Tách riêng zalo10/log/ với thư mục export data để tránh gộp nhầm
    // log debug và lịch sử tin nhắn khi gửi hỗ trợ.
    QString destDir  = "/accounts/1000/shared/documents/zalo10/log";
    QString stamp    = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString destPath = destDir + "/zalo10_log_" + stamp + ".txt";

    QDir dir;
    if (!dir.exists(destDir) && !dir.mkpath(destDir)) {
        qDebug() << "[App] exportLog: failed to create" << destDir;
        return QString();
    }

    if (!QFile::exists(srcPath)) {
        qDebug() << "[App] exportLog: no log file yet at" << srcPath;
        return QString();
    }

    bool ok = QFile::copy(srcPath, destPath);
    qDebug() << "[App] exportLog:" << (ok ? "success ->" : "FAILED ->") << destPath;
    return ok ? destPath : QString();
}

// OpenSSL bundle của BB10 quá cũ để handshake TLS1.2+ với GitHub CDN, nên
// phải nới protocol về AnyProtocol để OpenSSL tự thương lượng bản cao nhất
// cả 2 bên hỗ trợ, thay vì bị khóa cứng ở default cũ của Qt.
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

// sslErrors() chỉ bắn khi lỗi xảy ra sau khi handshake xong (vd cert chain
// lỗi), nên phần lớn không bắt được SslHandshakeFailedError, nhưng cứ giữ
// lại phòng hờ.
void ApplicationUI::onManifestSslErrors(const QList<QSslError> &errors)
{
    for (int i = 0; i < errors.size(); ++i)
        qDebug() << "[App] manifest SSL error:" << errors.at(i).errorString();
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
        qDebug() << "[App] checkForUpdate: network error" << err << "-" << reply->errorString();
        emit updateCheckResult(false, "", "", "Could not check for updates. Check your internet connection.");
        return;
    }

    QVariantMap root = jsonToMap(raw);
    QString latest      = root.value("latestVersion").toString();
    QString downloadUrl = root.value("downloadUrl").toString();
    if (latest.isEmpty()) {
        qDebug() << "[App] checkForUpdate: manifest missing/invalid";
        emit updateCheckResult(false, "", "", "Update info unavailable right now. Try again later.");
        return;
    }

    QString current = appVersion();
    bool isLatest = !isVersionNewer(latest, current);
    qDebug() << "[App] checkForUpdate: current=" << current << "latest=" << latest << "isLatest=" << isLatest;
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
        qDebug() << "[App] fetchChangelog: network error" << err << "-" << reply->errorString();
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

// Render mảng "changelog" trong manifest thành HTML: header "Version X.x"
// + bullet list mỗi entry. Tự bold tag đầu dòng (NEW:/IMPROVE:/FIX:/REMOVED:)
// và hỗ trợ **word** kiểu markdown, để viết changelog trên web không cần HTML.
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

            // "**word**" -> "<b>word</b>" — split on the literal marker and
            // alternate plain/bold, same trick used for simple markdown.
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
