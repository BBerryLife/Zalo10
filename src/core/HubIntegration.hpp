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

    // The card process (InvokeCard startup, see ApplicationUI) must never touch the Hub: init() re-creates the
    // account, which would delete every Hub item, the one that was just tapped included.
    void setDisabled(bool disabled) { m_disabled = disabled; }

    void upsertThreadItem(const QString &threadId, bool isGroup,
                           const QString &title, const QString &preview,
                           qint64 timestampMs);

    void markThreadRead(const QString &threadId);

    // Feeds: ONE item per account ("Zalo10 Feeds", id FEED_ITEM_ID) in its own category of the Zalo10 account, updated
    // in place; unread grows by addCount each time. Ids starting with "feed:" are routed to that
    // category by every method below (mark read/unread/remove), so no caller needs to care.
    static const char *FEED_ITEM_ID;
    static bool isFeedId(const QString &id);
    void upsertFeedItem(const QString &preview, qint64 timestampMs, int addCount);

    void removeThreadItem(const QString &threadId);

    // Message state icon of the item, the same as the chat list's (ChatsListItem.qml): yellow pellet = unread, grey
    // pellet = last message is theirs and read, otherwise the state of OUR last message (sending / sent / delivered /
    // read / failed, the numbers of message_states). Items are only ever updated here, never created.
    //  noteOutgoing(): our message is now the last of the thread (sent here, or echoed from another device); the
    //  item shows it as "sent" and counts as read.
    //  advanceOutgoingState(): a delivered (2) / read (3) receipt for it; ignored when the last message is theirs.
    void noteOutgoing(const QString &threadId, const QString &preview, qint64 timestampMs);
    void advanceOutgoingState(const QString &threadId, int state);

    // Looks up the isGroup saved by the last upsertThreadItem(): the Hub invoke payload only has
    // attributes.sourceId. Returns false (DM) for unknown threads.
    bool isGroupThread(const QString &threadId) const;
    QString titleFor(const QString &threadId) const;   // the title of the thread's Hub item

    // Action names Hub sends to the <invoke-target> (Mark read / Mark unread / Delete); must match
    // <action> in bar-descriptor.xml and the uds_register_item_context_action() calls in init().
    static const char *ACTION_OPEN;
    static const char *ACTION_MARK_READ;
    static const char *ACTION_REPLY;
    static const char *ACTION_MARK_UNREAD;
    static const char *ACTION_DELETE;

    // title/preview/timestamp/isGroup are only used when the thread has no Hub item yet (never notified, or
    // wiped when the account was re-registered): Mark as Unread then creates one.
    void markThreadUnread(const QString &threadId, const QString &title = QString(),
                          const QString &preview = QString(), qint64 timestampMs = 0, bool isGroup = false);
    static bool isItemAction(const QString &action);

    // Absolute path of the app's installed public asset directory, shared by uds_register_client()
    // and Notification::setIconUrl() so both use the same icon.
    static QString publicAssetPath();
    // file:// URI of ic_hub.png, as Notification::setIconUrl() requires (not asset:///).
    static QUrl hubIconUrl();

private:
    Q_DISABLE_COPY(HubIntegration)

    void upsertThreadItemN(const QString &threadId, bool isGroup,
                           const QString &title, const QString &preview,
                           qint64 timestampMs, int addUnread);
    bool initFeedsAccount();
    static long long accountIdFor(const QString &threadId);
    static long long categoryIdFor(const QString &threadId);
    static const char *iconFor(bool unread, int outState);
    bool m_feedsReady;

    // The Hub instant preview (banner) shows the ACCOUNT icon, not the item icon. The account therefore
    // switches to ic_hub_unread.png while something is unread and back to ic_hub.png when all is read.
    void setAccountUnread(bool unread);
    bool anyUnread();
    bool m_accountUnread;
    bool m_disabled;

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
        int     outState;   // -1: the last message is theirs; else the state of ours: 0 sending, 1 sent, 2 delivered, 3 read, 4 failed
        ThreadItemState() : timestampMs(0), isGroup(false), unread(0), total(0), outState(-1) {}
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
    static const long long ACCOUNT_ID = 424242007LL;

};

#endif
