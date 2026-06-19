#ifndef ApplicationUI_HPP_
#define ApplicationUI_HPP_

#include <QObject>
#include <QString>
#include <QSettings>
#include <bb/system/InvokeManager>
#include <bb/cascades/ThemeSupport>

namespace bb { namespace cascades { class LocaleHandler; class AbstractPane; } }
class QTranslator;
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

signals:
    void openThreadRequested(const QString &threadId, bool isGroup);
    // Emitted from setShowRecalledMessages() so any already-open ChatView can
    // re-evaluate its bubbles immediately, without needing to leave/reopen the thread.
    void showRecalledMessagesChanged(bool show);

private slots:
    void onSystemLanguageChanged();
    void onManualExit();
    void onInvoked(const bb::system::InvokeRequest &request);

private:
    QTranslator*                  m_pTranslator;
    bb::cascades::LocaleHandler*  m_pLocaleHandler;
    bb::system::InvokeManager*    m_pInvokeManager;
    ZaloService*                  m_zService;
};

#endif
