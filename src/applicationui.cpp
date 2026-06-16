#include "applicationui.hpp"
#include "ZaloService.hpp"
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
#include <QSettings>
#include <QDebug>

using namespace bb::cascades;
using namespace bb::system;

ApplicationUI::ApplicationUI() : QObject(), m_zService(NULL)
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
