#ifndef ApplicationUI_HPP_
#define ApplicationUI_HPP_

#include <QObject>
#include <QString>
#include <QSettings>
#include <QNetworkAccessManager>
#include <bb/system/InvokeManager>
#include <bb/cascades/ThemeSupport>

namespace bb { namespace cascades { class LocaleHandler; class AbstractPane; } }
class QTranslator;
class QNetworkReply;
class QSslError;
class ZaloService;

class ApplicationUI : public QObject
{
    Q_OBJECT

public:
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

    Q_INVOKABLE void createTodayEvent(const QString &subject, const QString &body, int durationMinutes);

signals:
    void openThreadRequested(const QString &threadId, bool isGroup);
    void showRecalledMessagesChanged(bool show);
    void darkThemeChanged(bool dark);
    void updateCheckResult(bool isLatest, const QString &latestVersion, const QString &downloadUrl, const QString &error);
    void changelogReady(const QString &html, const QString &error);
    void shareTargetsReady(const QVariantList &targets);
    void eventCreated(bool success, const QString &error);

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

private:
    QString buildChangelogHtml(const QVariantList &versions) const;

    QTranslator*                  m_pTranslator;
    bb::cascades::LocaleHandler*  m_pLocaleHandler;
    bb::system::InvokeManager*    m_pInvokeManager;
    ZaloService*                  m_zService;
    QNetworkAccessManager*        m_updateManager;
    bool                          m_exitHandled;
    QString                       m_pendingShareMimeType;
};

#endif
