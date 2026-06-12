#include "applicationui.hpp"
#include "ZaloService.hpp"

#include <bb/cascades/Application>
#include <bb/cascades/QmlDocument>
#include <bb/cascades/AbstractPane>
#include <bb/cascades/AbstractCover>
#include <bb/cascades/SceneCover>
#include <bb/cascades/LocaleHandler>
#include <bb/cascades/ThemeSupport>
#include <bb/system/InvokeManager>
#include <bb/system/InvokeRequest>

#include <QTranslator>
#include <QLocale>
#include <QSettings>
#include <QDebug>
#include <QFile>
#include <QXmlStreamReader>
#include <QFile>
#include <QTextStream>
#include <bb/device/DisplayInfo>

using namespace bb::cascades;
using namespace bb::system;

ApplicationUI::ApplicationUI() : QObject(), m_zService(NULL)
{
    m_pInvokeManager = new InvokeManager(this);
    m_pTranslator    = new QTranslator(this);
    m_pLocaleHandler = new LocaleHandler(this);

    bool res = QObject::connect(
        m_pLocaleHandler, SIGNAL(systemLanguageChanged()),
        this,             SLOT(onSystemLanguageChanged()));
    Q_ASSERT(res); Q_UNUSED(res);
    onSystemLanguageChanged();

    ZaloService *zService = new ZaloService(this);
    m_zService = zService;

    // Hook manualExit để save session trước khi app bị đóng hoàn toàn
    // Giúp timestamp được cập nhật đúng → loadSession biết session còn mới hay không
    QObject::connect(Application::instance(), SIGNAL(manualExit()),
                     this, SLOT(onManualExit()));

    // Áp dụng dark theme đã lưu TRƯỚC khi tạo QML scene
    {
        QSettings s("BerryLife", "Zalo10");
        bool dark = s.value("darkTheme", false).toBool();
        if (dark) {
            Application::instance()->themeSupport()->setVisualStyle(VisualStyle::Dark);
            qDebug() << "[App] Startup: applying saved Dark theme";
        }
    }

    QmlDocument *qml = QmlDocument::create("asset:///main.qml").parent(this);
    qml->setContextProperty("app",      this);
    qml->setContextProperty("zService", zService);


    AbstractPane *root = qml->createRootObject<AbstractPane>();
    Application::instance()->setScene(root);

    // Active Frame: load cover.qml as a separate QmlDocument (SmartList10 pattern)
    QmlDocument *coverQml = QmlDocument::create("asset:///cover.qml").parent(this);
    coverQml->setContextProperty("app", this);
    SceneCover *cover = coverQml->createRootObject<SceneCover>();
    if (cover) {
        Application::instance()->setCover(cover);
        qDebug() << "[App] Active Frame (SceneCover) set successfully";
    } else {
        qDebug() << "[App] cover.qml failed to create SceneCover";
    }
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
    // Dùng cùng một QSettings group/key nhất quán
    QSettings settings("BerryLife", "Zalo10");
    settings.setValue("darkTheme", dark);
    settings.sync();
    // Áp dụng ngay cho runtime (controls sẽ update)
    Application::instance()->themeSupport()->setVisualStyle(
        dark ? VisualStyle::Dark : VisualStyle::Bright);
    qDebug() << "[App] setDarkTheme:" << dark;
}

bool ApplicationUI::getDarkTheme()
{
    QSettings settings("BerryLife", "Zalo10");
    return settings.value("darkTheme", false).toBool();
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
    // User đóng app (swipe-close) — save session để timestamp được cập nhật
    if (m_zService && m_zService->loggedIn()) {
        qDebug() << "[App] manualExit: saving session before close";
        m_zService->saveSession();
    }
    bb::cascades::Application::instance()->quit();
}

QString ApplicationUI::appVersion()
{
    // Version built from integer DEFINES (APP_VER_MAJOR/MINOR/PATCH/BUILD) set in Zalo10.pro.
    // Integer defines have no escaping issues on Windows qmake + qcc.
    // Keep in sync with bar-descriptor.xml <versionNumber> and <buildId>.
#if defined(APP_VER_MAJOR) && defined(APP_VER_MINOR) && defined(APP_VER_PATCH) && defined(APP_VER_BUILD)
    return QString("%1.%2.%3.%4")
           .arg(APP_VER_MAJOR)
           .arg(APP_VER_MINOR)
           .arg(APP_VER_PATCH)
           .arg(APP_VER_BUILD);
#else
    return "1.1.0.1";
#endif
}

// Helper: detect screen type using BB10's official APIs.
// Returns: 0 = portrait/tall, 1 = square, 2 = landscape
//
// Strategy (in order of reliability):
//   1. bb::device::DisplayInfo::pixelSize() — official BB10 API for screen dimensions
//   2. PPS /pps/services/deviceproperties/physical — model name heuristic (Q-series/Passport)
//   3. PPS /pps/services/display/display0 — resolution key fallback
static int detectScreenType()
{
    // ── Method 1: bb::device::DisplayInfo ──────────────────────────────────
    // Try display IDs 0..3. On most BB10 devices the primary display is 0,
    // but some firmware builds assign a different ID and reject 0.
    {
        for (int dispId = 0; dispId <= 3; ++dispId) {
            bb::device::DisplayInfo di(dispId);
            QSize sz = di.pixelSize();
            int w = sz.width();
            int h = sz.height();
            if (w > 0 && h > 0) {
                qDebug() << "[App] DisplayInfo id=" << dispId << "pixelSize=" << w << "x" << h;
                if (w == h) return 1;  // square (Q10/Q20/Passport)
                if (w > h)  return 2;  // landscape
                return 0;              // portrait (Z10/Z30/Z3/Leap)
            }
        }
        qDebug() << "[App] DisplayInfo: no valid display found";
    }

    // ── Method 2: Device model via PPS — Q-series/Passport = square screen ─
    // /pps/services/deviceproperties/physical identifies the hardware model.
    // Q5=SQR100, Q10=SQN100, Q20=SQC100, Passport=SQW100/STR100 — all square.
    {
        QFile f("/pps/services/deviceproperties/physical");
        if (f.open(QIODevice::ReadOnly)) {
            QString data = QString::fromUtf8(f.readAll());
            f.close();
            qDebug() << "[App] deviceproperties:" << data.left(200);
            // Look for "hardware_id::SQxxx" or "model_name::SQxxx"
            foreach (const QString &line, data.split('\n')) {
                QString t = line.trimmed().toUpper();
                // Square-screen model prefixes
                if (t.contains("SQN") || t.contains("SQR") ||
                    t.contains("SQC") || t.contains("SQW") ||
                    t.contains("STR1")) {
                    qDebug() << "[App] PPS model: Q-series/Passport → square";
                    return 1;
                }
            }
        }
    }

    // ── Method 3: PPS file fallback ─────────────────────────────────────────
    static const char *paths[] = {
        "/pps/services/display/display0",
        "/pps/services/display/display0/display",
        "/pps/services/graphics/display/display_0"
    };
    for (int pi = 0; pi < 3; ++pi) {
        QFile f(paths[pi]);
        if (!f.open(QIODevice::ReadOnly)) continue;
        QString data = QString::fromUtf8(f.readAll());
        f.close();
        foreach (const QString &line, data.split('\n')) {
            QString t = line.trimmed();
            int sep = -1;
            if (t.startsWith("resolution::"))    sep = 12;
            else if (t.startsWith("size::"))      sep = 6;
            else if (t.startsWith("display_size::")) sep = 14;
            if (sep < 0) continue;
            QString res = t.mid(sep).trimmed();
            int ci = res.indexOf(':');
            if (ci != -1) res = res.left(ci).trimmed();
            QStringList parts = res.split('x');
            if (parts.size() == 2) {
                int w = parts[0].trimmed().toInt();
                int h = parts[1].trimmed().toInt();
                if (w > 0 && h > 0) {
                    qDebug() << "[App] PPS screen:" << w << "x" << h;
                    if (w == h) return 1;
                    if (w > h)  return 2;
                    return 0;
                }
            }
        }
    }

    qDebug() << "[App] detectScreenType: could not determine resolution, defaulting to portrait";
    return 0;
}

QString ApplicationUI::splashImage()
{
    int type = detectScreenType();
    switch (type) {
        case 1:  return "asset:///images/splash720.png"; // square: Q5/Q10/Q20/Passport
        case 2:  return "asset:///images/splashLS.png";  // landscape
        default: return "asset:///images/splash.png";    // portrait: Z10/Z30/Z3/Leap
    }
}

QString ApplicationUI::coverImage()
{
    int type = detectScreenType();
    if (type == 1)
        return "asset:///images/cover.png";  // square devices: cover.png fits perfectly
    return "asset:///images/splash.png";     // all other devices: reuse splash
}
