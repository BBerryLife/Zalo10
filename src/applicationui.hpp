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

public slots:
    void invokeEmail(const QString &to, const QString &subject);
    void minimizeApp();
    Q_INVOKABLE void setDarkTheme(bool dark);
    Q_INVOKABLE bool getDarkTheme();
    // "Show Recalled Messages": when enabled, recalled/unsent messages keep showing
    // their original content (with a "(This message was recalled)" tag) instead of
    // being replaced by the generic "This message was recalled" placeholder bubble.
    Q_INVOKABLE void setShowRecalledMessages(bool show);
    Q_INVOKABLE bool getShowRecalledMessages();
    Q_INVOKABLE QString appVersion();
    // Copies the running session's debug log to the shared Documents folder
    // (/accounts/1000/shared/documents/zalo10/log/zalo10_log_YYYYMMDD_HHmmss.txt)
    // so each export gets its own timestamped file instead of overwriting the
    // last one. Returns the exported file path on success, or an empty string
    // on failure.
    Q_INVOKABLE QString exportLog();

    // ---- Update check / changelog (About screen) -----------------------------
    // Both read the same small JSON manifest hosted on BBerryLife.github.io
    // (see VERSION_MANIFEST_URL in applicationui.cpp) — releasing a new version
    // only means editing that one file on the website, no app rebuild needed.
    // checkForUpdate(): fetches the manifest and compares "latestVersion" against
    // appVersion(); result comes back via updateCheckResult().
    Q_INVOKABLE void checkForUpdate();
    // fetchChangelog(): fetches the manifest's "changelog" array and renders it
    // into ready-to-display HTML (Version header per entry + bullet list, with
    // **word** turned into bold); result comes back via changelogReady().
    Q_INVOKABLE void fetchChangelog();

signals:
    void openThreadRequested(const QString &threadId, bool isGroup);
    // Emitted from setShowRecalledMessages() so any already-open ChatView can
    // re-evaluate its bubbles immediately, without needing to leave/reopen the thread.
    void showRecalledMessagesChanged(bool show);
    // isLatest: true if appVersion() is already >= latestVersion.
    // error non-empty only on network/parse failure (isLatest/latestVersion/downloadUrl
    // are meaningless in that case).
    void updateCheckResult(bool isLatest, const QString &latestVersion, const QString &downloadUrl, const QString &error);
    // html: ready to hand to a WebView's loadHtml(). error non-empty on failure.
    void changelogReady(const QString &html, const QString &error);

private slots:
    void onSystemLanguageChanged();
    void onManualExit();
    // Đọc/xả pipe từ signal handler SIGTERM (xem main.cpp) rồi gọi onManualExit().
    // Tách riêng vì SIGTERM đến qua 1 fd (self-pipe trick), không qua Cascades signal.
    void onTermSignal(int fd);
    void onInvoked(const bb::system::InvokeRequest &request);
    void onUpdateCheckFetchDone();
    void onChangelogFetchDone();
    void onManifestSslErrors(const QList<QSslError> &errors);

private:
    QString buildChangelogHtml(const QVariantList &versions) const;

    QTranslator*                  m_pTranslator;
    bb::cascades::LocaleHandler*  m_pLocaleHandler;
    bb::system::InvokeManager*    m_pInvokeManager;
    ZaloService*                  m_zService;
    QNetworkAccessManager*        m_updateManager;
    bool                          m_exitHandled; // chống chạy onManualExit() 2 lần nếu cả manualExit/aboutToQuit/SIGTERM đều bắn
};

#endif
