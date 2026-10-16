#ifndef HUBINTEGRATION_HPP
#define HUBINTEGRATION_HPP

#include <QObject>
#include <QString>
#include <QMap>
#include <QSet>
#include <QUrl>

// Forward-declared as void*: unified_data_source.h is a plain C API and would leak into every
// file that includes this header.

// Wraps BB10's UDS (Unified Data Source) library so Zalo10 gets its own account/tab in
// BlackBerry Hub (instant previews are only enabled for apps with a Hub account). Each thread
// (DM or group) is one inbox item with source_id = threadId, updated in place on new messages.
// UDS calls are synchronous but cheap IPC, so running them on the main thread is acceptable.
class HubIntegration : public QObject
{
    Q_OBJECT
public:
    explicit HubIntegration(QObject *parent = 0);
    virtual ~HubIntegration();

    // Opens the UDS connection and registers the account; safe to call repeatedly. Returns false if
    // UDS is unavailable (e.g. Simulator). Every other method no-ops when not ready, so a Hub
    // problem never blocks notifications.
    bool init();

    void upsertThreadItem(const QString &threadId, bool isGroup,
                           const QString &title, const QString &preview,
                           qint64 timestampMs);

    void markThreadRead(const QString &threadId);

    void removeThreadItem(const QString &threadId);

    // Looks up the isGroup saved by the last upsertThreadItem(): the Hub invoke payload only has
    // attributes.sourceId. Returns false (DM) for unknown threads.
    bool isGroupThread(const QString &threadId) const;

    // Action names Hub sends to the <invoke-target> (Mark read / Mark unread / Delete); must match
    // <action> in bar-descriptor.xml and the uds_register_item_context_action() calls in init().
    static const char *ACTION_OPEN;
    static const char *ACTION_MARK_READ;
    static const char *ACTION_MARK_UNREAD;
    static const char *ACTION_DELETE;

    void markThreadUnread(const QString &threadId);
    static bool isItemAction(const QString &action);

    // Absolute path of the app's installed public asset directory, shared by uds_register_client()
    // and Notification::setIconUrl() so both use the same icon.
    static QString publicAssetPath();
    // file:// URI of ic_hub.png, as Notification::setIconUrl() requires (not asset:///).
    static QUrl hubIconUrl();

private:
    Q_DISABLE_COPY(HubIntegration)

    // uds_item_updated() REPLACES the whole record: unset fields reset to empty/0 (markThreadRead()
    // once produced an empty name and a 1970 timestamp). Every update must supply all current
    // fields, so this struct stores what is needed to rebuild them.
    struct ThreadItemState {
        QString title;
        QString preview;
        qint64  timestampMs;
        bool    isGroup;
        int     unread;
        int     total;
        ThreadItemState() : timestampMs(0), isGroup(false), unread(0), total(0) {}
    };
    QMap<QString, ThreadItemState> m_threadItemState;

    // Item state is persisted in QSettings("BerryLife","Zalo10") group "hubItems": the UI and the
    // headless service each have their own HubIntegration, and a Hub action can reach the process
    // that did not create the item. Returns false if no state exists.
    bool ensureState(const QString &threadId);
    void saveState(const QString &threadId);
    void forgetState(const QString &threadId);
    bool publishState(const QString &threadId, bool notify, bool allowAdd);
    static unsigned int contextStateFor(bool unread);

    void *m_udsHandle;
    bool  m_ready;
    bool  m_initAttempted;

    // threadId -> unread_count shown on the item, so upsertThreadItem() accumulates instead of
    // resetting to 1.
    QMap<QString, int> m_unreadCounts;
    QSet<QString> m_knownThreadIds;

    // Changing the ID forces Hub to create a clean record. It was bumped repeatedly (424242001 ->
    // 006) while debugging: stale state in Hub (survives reinstall), account type experiments
    // (IM/TEXT_MESSAGE), a newly registered item action, and the theory that the ID is global while
    // register_client returns a NEW service on every build (taps invoked nothing). If taps still
    // fail with a fresh ID, that theory is ruled out.
    static const long long ACCOUNT_ID = 424242006LL;
};

#endif
