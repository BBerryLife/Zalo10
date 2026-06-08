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
