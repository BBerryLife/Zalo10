#ifndef ApplicationUI_HPP_
#define ApplicationUI_HPP_

#include <QObject>
#include <QString>
#include <QSettings>
#include <QNetworkAccessManager>
#include <bb/system/InvokeManager>
#include <bb/system/CardDoneMessage>
#include <bb/cascades/ThemeSupport>

namespace bb { namespace cascades { class LocaleHandler; class AbstractPane; } }
class QTranslator;
class QNetworkReply;
class QSslError;
class ZaloService;
class VoiceCallBridge;

class ApplicationUI : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool squareScreen READ squareScreen CONSTANT)
    Q_PROPERTY(int screenWidthPx READ screenWidthPx CONSTANT)
    Q_PROPERTY(int screenHeightPx READ screenHeightPx CONSTANT)

public:
    // Square displays (Q5/Q10/Q20/Passport) vs portrait (Z3/Z10/Z20/Z30). Physical pixels.
    bool squareScreen() const;
    int  screenWidthPx() const;   // the shorter side, i.e. the width in portrait
    int  screenHeightPx() const;  // the longer side
    explicit ApplicationUI();
    virtual ~ApplicationUI() {}

    void pingHandoffService(const char *action);

public slots:
    void onServicePingFinished();
    void invokeEmail(const QString &to, const QString &subject);
    void minimizeApp();
    Q_INVOKABLE void setDarkTheme(bool dark);
    Q_INVOKABLE bool getDarkTheme();
    Q_INVOKABLE void setShowRecalledMessages(bool show);
    Q_INVOKABLE bool getShowRecalledMessages();
    Q_INVOKABLE bool getTosAccepted();
    Q_INVOKABLE void setTosAccepted(bool accepted);
    Q_INVOKABLE void quitApp();
    Q_INVOKABLE QString appVersion();
    Q_INVOKABLE QString exportLog();

    Q_INVOKABLE void checkForUpdate();
    Q_INVOKABLE void fetchChangelog();

    Q_INVOKABLE void copyToClipboard(const QString &text);
    Q_INVOKABLE bool copyImageToClipboard(const QString &localPath);
    Q_INVOKABLE void queryShareTargets(const QString &text);
    Q_INVOKABLE void invokeShareTarget(const QString &target, const QString &action, const QString &text);
    Q_INVOKABLE void queryShareTargetsForImage(const QString &localPath);
    Q_INVOKABLE void invokeShareTargetForImage(const QString &target, const QString &action, const QString &localPath);
    Q_INVOKABLE void openLocalFile(const QString &localPath);
    // Camera card (sys.camera.card): one photo; the file comes back through cameraPhotoCaptured().
    Q_INVOKABLE void captureCameraPhoto();

    // Hub reply card (InvokeCard startup, see the constructor). closeCard() ends the card; openThreadFromCard() hands
    // the conversation to the main app (a normal bb.action.OPEN invoke) and then closes the card.
    Q_INVOKABLE bool isCardMode() const { return m_cardMode; }
    Q_INVOKABLE void closeCard();
    Q_INVOKABLE void openThreadFromCard(const QString &threadId, bool isGroup);

    Q_INVOKABLE void createTodayEvent(const QString &subject, const QString &body, int durationMinutes);

signals:
    void openThreadRequested(const QString &threadId, bool isGroup);
    void openFeedsRequested();
    void replyCardRequested(const QString &threadId, bool isGroup);
    void showRecalledMessagesChanged(bool show);
    void darkThemeChanged(bool dark);
    void updateCheckResult(bool isLatest, const QString &latestVersion, const QString &downloadUrl, const QString &error);
    void changelogReady(const QString &html, const QString &error);
    void shareTargetsReady(const QVariantList &targets);
    void eventCreated(bool success, const QString &error);
    void cameraPhotoCaptured(const QString &path);

private slots:
    void onSystemLanguageChanged();
    void onManualExit();
    void onTermSignal(int fd);
    void onInvoked(const bb::system::InvokeRequest &request);
    // Foreground/background tracking: avoids duplicate banner dialogs on top of the Hub sound.
    void onAppFullscreen();
    void onAppNotFullscreen();
    void onUpdateCheckFetchDone();
    void onChangelogFetchDone();
    void onManifestSslErrors(const QList<QSslError> &errors);
    void onQueryTargetsFinished();
    void onChildCardDone(const bb::system::CardDoneMessage &message);
    void onCardAboutToQuit();
    void closeCardLater();

private:
    QString buildChangelogHtml(const QVariantList &versions) const;

    QTranslator*                  m_pTranslator;
    bb::cascades::LocaleHandler*  m_pLocaleHandler;
    bb::system::InvokeManager*    m_pInvokeManager;
    ZaloService*                  m_zService;
    QNetworkAccessManager*        m_updateManager;
    bool                          m_exitHandled;
    QString                       m_pendingShareMimeType;
    bool                          m_waitingCameraPhoto;
    bool                          m_cardMode;
    // A Hub tap was handed to the reply card from this (main) process and the card is still open.
    bool                          m_hubTapCard;
    VoiceCallBridge*              m_voice;
};

#endif
