#include "app/ApplicationUICommon.hpp"
#include "core/HubIntegration.hpp"
#include "core/stickers/StickerManager.hpp"
#include "core/voice/VoiceCallBridge.hpp"
#include <bb/device/DisplayInfo>

namespace {
QSize screenPixels()
{
    bb::device::DisplayInfo display;
    QSize px = display.pixelSize();
    if (px.width() > px.height()) px.transpose();   // portrait orientation: width is the short side
    return px;
}
}

bool ApplicationUI::squareScreen() const
{
    QSize px = screenPixels();
    if (px.isEmpty()) return true;
    return qAbs(px.width() - px.height()) <= qMax(px.width(), px.height()) / 20;
}

int ApplicationUI::screenWidthPx() const
{
    return screenPixels().width();
}

int ApplicationUI::screenHeightPx() const
{
    return screenPixels().height();
}

ApplicationUI::ApplicationUI() : QObject(), m_zService(NULL), m_updateManager(NULL), m_exitHandled(false), m_pendingShareMimeType("text/plain"), m_waitingCameraPhoto(false), m_cardMode(false), m_hubTapCard(false), m_voice(NULL)
{
    m_pInvokeManager = new InvokeManager(this);
    bool invokeConnectOk = QObject::connect(
        m_pInvokeManager, SIGNAL(invoked(const bb::system::InvokeRequest&)),
        this,             SLOT(onInvoked(const bb::system::InvokeRequest&)));
    QObject::connect(
        m_pInvokeManager, SIGNAL(childCardDone(const bb::system::CardDoneMessage&)),
        this,             SLOT(onChildCardDone(const bb::system::CardDoneMessage&)));
    m_pTranslator    = new QTranslator(this);
    m_pLocaleHandler = new LocaleHandler(this);

    bool res = QObject::connect(
        m_pLocaleHandler, SIGNAL(systemLanguageChanged()),
        this,             SLOT(onSystemLanguageChanged()));
    Q_ASSERT(res); Q_UNUSED(res);
    onSystemLanguageChanged();

    // The Hub "Reply" action starts this app a second time as a card over the Hub (card.composer target in
    // bar-descriptor.xml). That process must stay small: HTTP only, no WebSocket, no Hub account, and it shows
    // HubReplyCard.qml instead of main.qml.
    m_cardMode = (m_pInvokeManager->startupMode() == bb::system::ApplicationStartupMode::InvokeCard);
    qDebug() << "[App] startup, cardMode=" << m_cardMode;

    ZaloService *zService = new ZaloService(this, m_cardMode);
    m_zService = zService;

    if (m_cardMode) {
        QObject::connect(QCoreApplication::instance(), SIGNAL(aboutToQuit()), this, SLOT(onCardAboutToQuit()));
        {
            QSettings s("BerryLife", "Zalo10");
            if (s.value("darkTheme", false).toBool())
                Application::instance()->themeSupport()->setVisualStyle(VisualStyle::Dark);
        }
        qDebug() << "[Card] reply card starting";
        QmlDocument *cardQml = QmlDocument::create("asset:///HubReplyCard.qml").parent(this);
        cardQml->setContextProperty("app",      this);
        cardQml->setContextProperty("zService", zService);
        AbstractPane *cardRoot = cardQml->createRootObject<AbstractPane>();
        if (!cardRoot) {
            qWarning() << "[Card] HubReplyCard.qml failed to load";
            closeCard();
            return;
        }
        Application::instance()->setScene(cardRoot);
        qDebug() << "[Card] scene set";
        return;
    }

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
    qml->setContextProperty("stickerManager", new StickerManager(this));
    m_voice = new VoiceCallBridge(zService, this);
    qml->setContextProperty("voice", m_voice);
    connect(m_voice, SIGNAL(bringToFrontRequested(int)), this, SLOT(onBringToFront(int)));

    AbstractPane *root = qml->createRootObject<AbstractPane>();
    Application::instance()->setScene(root);

    ActiveFrameCover *cover = new ActiveFrameCover();
    Application::instance()->setCover(cover);
}

void ApplicationUI::onCardAboutToQuit()
{
    qDebug() << "[Card] aboutToQuit";
}

void ApplicationUI::closeCard()
{
    bb::system::CardDoneMessage msg;
    msg.setReason("done");
    m_pInvokeManager->sendCardDone(msg);
}

void ApplicationUI::closeCardLater()
{
    closeCard();
}

// "Open" on the card: a plain OPEN invoke of the main app, whose onInvoked() routes it like a Hub tap.
void ApplicationUI::openThreadFromCard(const QString &threadId, bool isGroup)
{
    if (threadId.isEmpty()) { closeCard(); return; }
    bb::system::InvokeRequest req;
    req.setTarget("com.BerryLife.Zalo10.invoke");
    req.setAction("bb.action.OPEN");
    req.setMimeType("application/vnd.zalo10.hub.chat");
    req.setUri(QUrl("pim:application/vnd.zalo10.hub.chat"));
    req.setData(QString("%1|%2").arg(threadId).arg(isGroup ? "1" : "0").toUtf8());
    bb::system::InvokeTargetReply *reply = m_pInvokeManager->invoke(req);
    if (reply) reply->setParent(this);
    // Give Navigator a moment to take the invoke before the card goes away.
    QTimer::singleShot(400, this, SLOT(closeCardLater()));
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

QString ApplicationUI::getLanguage()
{
    QSettings settings("BerryLife", "Zalo10");
    return settings.value("language", "system").toString();
}

void ApplicationUI::setLanguage(const QString &lang)
{
    QSettings settings("BerryLife", "Zalo10");
    settings.setValue("language", lang);
    settings.sync();
    onSystemLanguageChanged();
}

// Also runs when the user picks a language in Settings > General. The source strings are English, so
// "en" just removes the translator; "system" follows the device language (Zalo10_vi.qm for vi_VN, etc.).
void ApplicationUI::onSystemLanguageChanged()
{
    QCoreApplication::instance()->removeTranslator(m_pTranslator);
    QString lang = getLanguage();
    if (lang == "en") return;
    QString locale = (lang.isEmpty() || lang == "system") ? QLocale().name() : lang;
    const QString name = QString("Zalo10_%1").arg(locale);
    bool loaded = m_pTranslator->load(name, "app/native/qm");
    if (!loaded) loaded = m_pTranslator->load(name, QCoreApplication::applicationDirPath() + "/qm");
    qDebug() << "[I18n] language" << lang << "locale" << locale << (loaded ? "loaded" : "NOT loaded (no .qm?)");
    if (loaded)
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

// Incoming call while the app is not in front: the shapes below are tried one per call until the app comes up.
// Each attempt logs the Navigator's error code (0 = accepted, 2 = BadRequest).
void ApplicationUI::onBringToFront(int shape)
{
    qDebug() << "[Voice] bring to front, shape" << shape;
    bb::system::InvokeRequest req;
    switch (shape) {
    case 0:   // exactly the UI_OPENED ping, other action
        req.setTarget("com.BerryLife.Zalo10.service");
        req.setAction("com.BerryLife.Zalo10.service.OPEN_UI");
        break;
    case 1:
        req.setTarget("com.BerryLife.Zalo10.service");
        req.setAction("com.BerryLife.Zalo10.service.OPEN_UI");
        req.setUri(QUrl("headless:"));
        break;
    case 2:   // service, OPEN, Hub-like
        req.setTarget("com.BerryLife.Zalo10.service");
        req.setAction("bb.action.OPEN");
        req.setMimeType("application/vnd.zalo10.hub.chat");
        break;
    case 3:   // the app itself
        req.setTarget("com.BerryLife.Zalo10.invoke");
        req.setAction("bb.action.OPEN");
        req.setMimeType("application/vnd.zalo10.hub.chat");
        req.setData(QByteArray("zalo10:call"));
        break;
    default:
        req.setTarget("com.BerryLife.Zalo10.invoke");
        req.setAction("bb.action.OPEN");
        req.setMimeType("application/vnd.zalo10.hub.chat");
        req.setUri(QUrl("pim:application/vnd.zalo10.hub.chat"));
        req.setData(QByteArray("zalo10:call"));
        break;
    }
    bb::system::InvokeTargetReply *reply = m_pInvokeManager->invoke(req);
    if (!reply) { qWarning() << "[Voice] bring to front: no reply, shape" << shape; return; }
    reply->setProperty("shape", shape);
    connect(reply, SIGNAL(finished()), this, SLOT(onBringReplyFinished()));
}

void ApplicationUI::onBringReplyFinished()
{
    bb::system::InvokeTargetReply *reply = qobject_cast<bb::system::InvokeTargetReply *>(sender());
    if (!reply) return;
    qDebug() << "[Voice] bring-to-front reply error=" << (int)reply->error() << "shape" << reply->property("shape").toInt();
    reply->deleteLater();
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

    // Incoming-call launch: the engine (app minimized) or the headless service (app closed) opens the app on
    // the call screen. "zalo10:call" alone only brings the app forward; "zalo10:call|<json>" also carries the
    // call "request" frame the service received while the app was closed.
    static const QString kCallMarker = QString::fromLatin1("zalo10:call");
    if (rawData.startsWith(kCallMarker)) {
        const QString payload = rawData.mid(kCallMarker.size() + 1);
        qDebug() << "[App] call launch, frame=" << !payload.isEmpty();
        if (m_voice && !payload.isEmpty())
            m_voice->control(jsonToMap(payload.toUtf8()));
        return;
    }

    QString threadId;
    bool    isGroup = false;

    // The Hub "Zalo10 Feeds" item (id "feed:updates") must open the Feeds tab. Decide that first,
    // from the raw payload: the uri branch below splits on ':' and would keep only "updates", which
    // then opened an empty chat view.
    const QString feedId = QString::fromLatin1(HubIntegration::FEED_ITEM_ID);
    if (rawData.contains(feedId) || rawUri.toString().contains(feedId)) {
        if (m_cardMode) {
            // The Hub tap reached the card (it is the VIEW target): the Feeds item belongs to the app.
            bb::system::InvokeRequest fwd;
            fwd.setTarget("com.BerryLife.Zalo10.invoke");
            fwd.setAction("bb.action.OPEN");
            fwd.setMimeType(request.mimeType());
            fwd.setUri(request.uri());
            fwd.setData(request.data());
            bb::system::InvokeTargetReply *fr = m_pInvokeManager->invoke(fwd);
            if (fr) fr->setParent(this);
            QTimer::singleShot(400, this, SLOT(closeCardLater()));
            return;
        }
        if (!(m_zService && m_zService->handleHubAction(request.action(), feedId)))
            emit openFeedsRequested();
        return;
    }

    QString trimmed = rawData.trimmed();

    // Which way did the Hub tap arrive? (the service forwards it, or the Hub reached the app directly)
    qDebug() << "[App] invoked action=" << request.action() << "target=" << request.target()
             << "hubPayload=" << trimmed.startsWith("{") << "card=" << m_cardMode;

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

    if (m_cardMode) {
        // Hub "Reply": HubReplyCard.qml shows this conversation.
        qDebug() << "[Card] invoked action=" << request.action();
        emit replyCardRequested(threadId, isGroup);
        return;
    }

    if (m_zService && m_zService->handleHubAction(request.action(), threadId)) {
        return;
    }

    // Going into the app (a tap with the card off, or the card's Open button): the card must not send the app
    // back to the background when it closes.
    m_hubTapCard = false;

    if (HubIntegration::isFeedId(threadId)) {
        emit openFeedsRequested();
        return;
    }

    qDebug() << "[App] opening chat" << threadId << "isGroup=" << isGroup;
    emit openThreadRequested(threadId, isGroup);
}
