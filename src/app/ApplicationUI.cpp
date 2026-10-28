#include "app/ApplicationUICommon.hpp"

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

bool ApplicationUI::getTosAccepted()
{
    QSettings settings("BerryLife", "Zalo10");
    return settings.value("tosAccepted", false).toBool();
}

void ApplicationUI::setTosAccepted(bool accepted)
{
    QSettings settings("BerryLife", "Zalo10");
    settings.setValue("tosAccepted", accepted);
    settings.sync();
}

void ApplicationUI::quitApp()
{
    onManualExit();
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
