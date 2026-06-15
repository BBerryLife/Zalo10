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
#include <bb/device/DisplayInfo>

#include <QTranslator>
#include <QLocale>
#include <QSettings>
#include <QDebug>
#include <QFile>
#include <QTextStream>

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

    // Save session on manual exit so timestamps stay up to date
    QObject::connect(Application::instance(), SIGNAL(manualExit()),
                     this, SLOT(onManualExit()));

    // Apply saved theme before creating QML scene
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

    QmlDocument *coverQml = QmlDocument::create("asset:///cover.qml").parent(this);
    coverQml->setContextProperty("app", this);
    SceneCover *cover = coverQml->createRootObject<SceneCover>();
    if (cover) {
        Application::instance()->setCover(cover);
        qDebug() << "[App] Active Frame set";
    } else {
        qDebug() << "[App] cover.qml failed";
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
    QSettings settings("BerryLife", "Zalo10");
    settings.setValue("darkTheme", dark);
    settings.sync();
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
    if (m_zService && m_zService->loggedIn()) {
        qDebug() << "[App] manualExit: saving session";
        m_zService->saveSession();
    }
    bb::cascades::Application::instance()->quit();
}

QString ApplicationUI::appVersion()
{
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

// Detect screen orientation/shape using BB10 APIs.
// Returns: 0 = portrait, 1 = square, 2 = landscape
static int detectScreenType()
{
    // Method 1: bb::device::DisplayInfo
    for (int dispId = 0; dispId <= 3; ++dispId) {
        bb::device::DisplayInfo di(dispId);
        QSize sz = di.pixelSize();
        int w = sz.width(), h = sz.height();
        if (w > 0 && h > 0) {
            qDebug() << "[App] DisplayInfo id=" << dispId << "size=" << w << "x" << h;
            if (w == h) return 1;
            if (w > h)  return 2;
            return 0;
        }
    }

    // Method 2: PPS device model — Q/Passport series = square
    {
        QFile f("/pps/services/deviceproperties/physical");
        if (f.open(QIODevice::ReadOnly)) {
            QString data = QString::fromUtf8(f.readAll());
            f.close();
            foreach (const QString &line, data.split('\n')) {
                QString t = line.trimmed().toUpper();
                if (t.contains("SQN") || t.contains("SQR") ||
                    t.contains("SQC") || t.contains("SQW") ||
                    t.contains("STR1")) {
                    qDebug() << "[App] PPS model: square";
                    return 1;
                }
            }
        }
    }

    // Method 3: PPS display resolution fallback
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

    qDebug() << "[App] detectScreenType: defaulting to portrait";
    return 0;
}

QString ApplicationUI::coverImage()
{
    QFile f("/pps/services/deviceproperties/physical");
    if (f.open(QIODevice::ReadOnly)) {
        QString data = QString::fromUtf8(f.readAll());
        f.close();
        foreach (const QString &line, data.split('\n')) {
            QString t = line.trimmed().toUpper();
            if (t.contains("SQW") || t.contains("STR1")) {
                qDebug() << "[App] coverImage: Passport";
                return "asset:///images/splash.png";
            }
            if (t.contains("SQN") || t.contains("SQR") || t.contains("SQC")) {
                qDebug() << "[App] coverImage: Q-series";
                return "asset:///images/cover.png";
            }
        }
    }
    int type = detectScreenType();
    return (type == 1) ? "asset:///images/cover.png" : "asset:///images/splash.png";
}
