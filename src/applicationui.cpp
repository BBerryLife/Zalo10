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
#include <bb/ApplicationInfo>

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

// Single small JSON file on BBerryLife.github.io that both checkForUpdate() and
// fetchChangelog() read. Releasing a new version = editing this one file on the
// website (latestVersion / downloadUrl / changelog array) — the app never needs
// a hardcoded download link or a rebuild just to know about a newer release.
// Expected shape:
// {
//   "latestVersion": "1.2.0",
//   "downloadUrl": "https://.../Downloads/Zalo10-1_2_0_0.bar",
//   "changelog": [ { "version": "1.2.0", "items": ["NEW: ...", "IMPROVE: ..."] }, ... ]
// }
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

ApplicationUI::ApplicationUI() : QObject(), m_zService(NULL), m_updateManager(NULL), m_exitHandled(false)
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
    // Lưới an toàn thứ 2: manualExit() (Cascades) có vẻ KHÔNG bắn khi user vuốt
    // card đóng app trong màn đa nhiệm (đã xác nhận qua log thực tế — không có
    // dòng "manualExit: saving session" nào xuất hiện trước khi process chết).
    // aboutToQuit() là signal Qt core chuẩn, nhiều khả năng phổ quát hơn.
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
    // Reads version directly from bar-descriptor.xml at runtime (same pattern as bbtube).
    // To update the version, only bar-descriptor.xml needs to be changed.
    return bb::ApplicationInfo().version();
}

QString ApplicationUI::exportLog()
{
    QString srcPath  = QDir::homePath() + "/zalo10_runtime.log";
    // Logs go in zalo10/log/ — a sibling of, but separate from, the data
    // exports written to zalo10/ by ZaloService::exportData(). Keeping them
    // apart means attaching a debug log to a support email never accidentally
    // bundles message history, and vice versa.
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

// BB10's bundled OpenSSL is old enough that Qt's default protocol pin
// (effectively SSLv3/TLS1.0-only on this NDK) can't complete a handshake with
// GitHub's CDN (raw.githubusercontent.com / Fastly), which requires TLS1.2+ —
// that's QNetworkReply::SslHandshakeFailedError (error code 6), and it happens
// *before* certificate checking, so setPeerVerifyMode(VerifyNone) alone isn't
// enough; the protocol negotiation itself has to be loosened too.
// AnyProtocol tells the underlying OpenSSL to negotiate the highest version
// both sides support instead of being locked to Qt's older default.
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

// sslErrors() only fires for problems found *after* the handshake completes
// (bad/unknown cert chain etc). A bare SslHandshakeFailedError (error code 6)
// usually means the handshake itself never got that far — so this mostly won't
// fire for that case, but it's cheap insurance and gives us the real OpenSSL
// reason on the rare case it does.
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

// Renders the manifest's "changelog" array into the same look as the
// Reference/Change List screen used elsewhere (bold "Version X.x" header
// per entry + bullet list). Each item is HTML-escaped first, then:
//   - a leading "NEW:"/"IMPROVE:"/"FIX:"/"REMOVED:" tag is bolded automatically
//   - any **word** markdown pair is turned into <b>word</b>
// so Jim can write plain changelog text on the website without touching HTML.
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
