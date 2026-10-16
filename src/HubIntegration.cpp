#include "HubIntegration.hpp"

#include <bb/pim/unified/unified_data_source.h>

#include <QDebug>
#include <QByteArray>
#include <QLatin1String>
#include <QDir>
#include <QStringList>
#include <QSettings>

// Account icon (Hub tab). Must live in the asset dir passed to uds_register_client()
// (see publicAssetPath()) and be declared public="true" in bar-descriptor.xml.
static const char *HUB_ICON_FILE = "ic_hub.png";
// Per-item read/unread icons; also need public="true" in bar-descriptor.xml, otherwise Hub
// silently shows an empty icon.
static const char *HUB_ICON_UNREAD_FILE = "ic_hub_unread.png";
static const char *HUB_ICON_READ_FILE   = "ic_hub_read.png";
static const char *HUB_ICON_MARK_OPENED_FILE   = "ic_mark_opened.png";
static const char *HUB_ICON_MARK_UNOPENED_FILE = "ic_mark_unopened.png";
static const char *HUB_SERVICE_TARGET = "com.BerryLife.Zalo10.service";
static const char *HUB_SERVICE_URL = "com.BerryLife.Zalo10.hub";

// Short-tap on Hub items needs Hub to match the item to the <invoke-target> (a generic mime type
// and a filter without "uris" broke this). Setup used by BBCord/Beeper10: (1) app-specific mime
// type shared by items, item context actions and the <filter> in bar-descriptor.xml; (2) <filter>
// with <property var="uris" value="pim:<mime type>"/>; (3) item action UDS_PLACEMENT_DEFAULT and
// account target_name = the invoke-target id (not the app id).
static const char *HUB_INVOKE_TARGET = "com.BerryLife.Zalo10.invoke";
// Must match bar-descriptor.xml (<mime-type> and pim: uris).
static const char *HUB_MIME_TYPE_MESSAGE = "application/vnd.zalo10.hub.chat";

// Context state bit; must match the context_mask of the "Open in Zalo10" action. Without
// uds_inbox_item_data_set_context_state() Hub finds no action for the item (suspected cause of
// taps doing nothing).
static const unsigned int HUB_CONTEXT_STATE_READ   = 0x01;
static const unsigned int HUB_CONTEXT_STATE_UNREAD = 0x02;

// Action names Hub sends to the invoke-target; must match <action> in bar-descriptor.xml.
const char *HubIntegration::ACTION_OPEN        = "bb.action.OPEN";
const char *HubIntegration::ACTION_MARK_READ   = "bb.action.MARKREAD";
const char *HubIntegration::ACTION_MARK_UNREAD = "bb.action.MARKUNREAD";
const char *HubIntegration::ACTION_DELETE      = "bb.action.DELETE";

bool HubIntegration::isItemAction(const QString &action)
{
    return action == QLatin1String(ACTION_MARK_READ)
        || action == QLatin1String(ACTION_MARK_UNREAD)
        || action == QLatin1String(ACTION_DELETE);
}

unsigned int HubIntegration::contextStateFor(bool unread)
{
    return unread ? HUB_CONTEXT_STATE_UNREAD : HUB_CONTEXT_STATE_READ;
}

static int registerItemAction(void *udsHandle, long long accountId, const char *action,
                              const char *target, const char *icon,
                              const char *title, unsigned int contextMask,
                              uds_placement_type_t placement)
{
    uds_item_action_data_t *a = uds_item_action_data_create();
    uds_item_action_data_set_action(a, action);
    uds_item_action_data_set_target(a, target);
    uds_item_action_data_set_type(a, "service");
    uds_item_action_data_set_title(a, title);
    uds_item_action_data_set_image_source(a, icon);
    uds_item_action_data_set_mime_type(a, HUB_MIME_TYPE_MESSAGE);
    uds_item_action_data_set_placement(a, placement);
    uds_item_action_data_set_context_mask(a, contextMask);
    int rc = uds_register_item_context_action(static_cast<uds_context_t>(udsHandle), accountId, a);
    uds_item_action_data_destroy(a);
    return rc;
}

// category_id = 1 (a single category for all items). It was the only field missing compared to
// the official sample; a guess, NOT a confirmed cause of the short-tap bug.
static const long long HUB_CATEGORY_ID = 1;

HubIntegration::HubIntegration(QObject *parent)
    : QObject(parent), m_udsHandle(0), m_ready(false), m_initAttempted(false)
{
}

HubIntegration::~HubIntegration()
{
    if (m_udsHandle) {
        uds_context_t h = static_cast<uds_context_t>(m_udsHandle);
        uds_close(&h);
        m_udsHandle = 0;
    }
}

// Why not __progname: it is the entry point name ("Zalo10.so" in .bar builds), not the real app
// id, so the asset path pointed to a non-existent /apps/Zalo10.so/public/res_hub/ and the icon
// stayed blank (not a Hub cache problem).
extern char *__progname;

// The app id comes from QDir::homePath() ("/accounts/1000/appdata/<app-id>/data"), which matches
// the real id in both Debug and Release builds.
static QString appIdFromHomePath()
{
    QStringList parts = QDir::homePath().split(QLatin1Char('/'), QString::SkipEmptyParts);
    if (parts.size() >= 2 && parts.last() == QLatin1String("data")) {
        QString appId = parts.at(parts.size() - 2);
        return appId;
    }
    // Fallback to __progname: a wrong path is better than crashing (Hub is an optional feature).
    return QString::fromLatin1(__progname);
}

QString HubIntegration::publicAssetPath()
{
    // "res_hub" must match the dest in bar-descriptor.xml. The name must not start with "assets":
    // Momentics (NDK 10.3.1) blocks names prefixed like the "assets" rule (even a top-level
    // "assets-public").
    return QString("/apps/%1/public/res_hub/").arg(appIdFromHomePath());
}

QUrl HubIntegration::hubIconUrl()
{
    return QUrl::fromLocalFile(publicAssetPath() + QLatin1String(HUB_ICON_FILE));
}

// Always remove-before-add on startup. Tried and dropped: (a) caching assetPath in a file under
// homePath to detect changes (useless: each build variant, testDev/testRel, has its own sandbox);
// (b) update-first without removing (did not fix single-tap and the blank icon came back).
// The theory that remove-before-add breaks single-tap has no evidence: single-tap fails either
// way, so it is probably an independent bug.

bool HubIntegration::init()
{
    if (m_ready) return true;
    if (m_initAttempted) return false;
    m_initAttempted = true;

    uds_context_t handle = 0;
    int rc = uds_init(&handle, false );
    if (rc != UDS_SUCCESS || !handle) {
        qWarning() << "[Hub] init failed, rc=" << rc << "(using plain notifications)";
        return false;
    }
    m_udsHandle = handle;

    QString assetPath = publicAssetPath();

    rc = uds_register_client(m_udsHandle, HUB_SERVICE_URL, "" ,
                              assetPath.toUtf8().constData());
    if (rc != UDS_SUCCESS) {
        qWarning() << "[Hub] register client failed, rc=" << rc;
        uds_context_t h = static_cast<uds_context_t>(m_udsHandle);
        uds_close(&h);
        m_udsHandle = 0;
        return false;
    }

    int regStatus = uds_get_service_status(m_udsHandle);
    int serviceId = uds_get_service_id(m_udsHandle);
    // Compare serviceId/status across runs: if register_client always returns NEW with a changing
    // serviceId, Hub treats every build as a new service (see ACCOUNT_ID in the header).

    // Keep the 72x72 icons: the 81x81 recommended in the UDS header looked off-size on device.

    int removeRc = uds_account_removed(m_udsHandle, ACCOUNT_ID);

    uds_account_data_t *account = uds_account_data_create();
    uds_account_data_set_id(account, ACCOUNT_ID);
    uds_account_data_set_name(account, "Zalo10");
    uds_account_data_set_description(account, "Zalo10 messages");
    uds_account_data_set_icon(account, HUB_ICON_FILE);
    uds_account_data_set_target_name(account, HUB_SERVICE_TARGET);
    uds_account_data_set_supports_compose(account, false);
    // Tried UDS_ACCOUNT_TYPE_TEXT_MESSAGE: it did not fix tapping and changed the Hub sort order,
    // so IM is kept.
    uds_account_data_set_type(account, UDS_ACCOUNT_TYPE_IM);

    rc = uds_account_added(m_udsHandle, account);
    uds_account_data_destroy(account);

    if (rc != UDS_SUCCESS) {
        qWarning() << "[Hub] account add failed, rc=" << rc;
        return false;
    }

    qDebug() << "[Hub] account registered";
    m_ready = true;

    // uds_category_added() must run after account_added() and before any uds_item_added() that
    // uses this category_id.
    {
        uds_category_data_t *category = uds_category_data_create();
        uds_category_data_set_id(category, HUB_CATEGORY_ID);
        uds_category_data_set_account_id(category, ACCOUNT_ID);
        uds_category_data_set_name(category, "Zalo10");
        int categoryRc = uds_category_added(m_udsHandle, category);
        if (categoryRc != UDS_SUCCESS) categoryRc = uds_category_updated(m_udsHandle, category);
        uds_category_data_destroy(category);
    }

    // "Open in Zalo10" item context action (long-press), registered once for the account.
    // Mime type/placement/target must follow the setup described at the top of this file.
    uds_item_action_data_t *openAction = uds_item_action_data_create();
    uds_item_action_data_set_action(openAction, "bb.action.OPEN");
    uds_item_action_data_set_target(openAction, HUB_INVOKE_TARGET);
    uds_item_action_data_set_type(openAction, "service");
    uds_item_action_data_set_title(openAction, "Open in Zalo10");
    uds_item_action_data_set_image_source(openAction, HUB_ICON_FILE);
    uds_item_action_data_set_mime_type(openAction, HUB_MIME_TYPE_MESSAGE);
    uds_item_action_data_set_placement(openAction, UDS_PLACEMENT_DEFAULT);
    uds_item_action_data_set_context_mask(openAction, HUB_CONTEXT_STATE_READ | HUB_CONTEXT_STATE_UNREAD);

    int actionRc = uds_register_item_context_action(m_udsHandle, ACCOUNT_ID, openAction);
    uds_item_action_data_destroy(openAction);
    if (actionRc != UDS_SUCCESS) {
        qWarning() << "[Hub] item action registration failed, rc=" << actionRc;
        // Not fatal: the account is registered, only the "Open" action is missing.
    } else {
    }

    registerItemAction(m_udsHandle, ACCOUNT_ID, ACTION_MARK_READ, HUB_SERVICE_TARGET,
                       HUB_ICON_MARK_OPENED_FILE, "Mark Read",
                       HUB_CONTEXT_STATE_UNREAD, UDS_PLACEMENT_DEFAULT);
    registerItemAction(m_udsHandle, ACCOUNT_ID, ACTION_MARK_UNREAD, HUB_SERVICE_TARGET,
                       HUB_ICON_MARK_UNOPENED_FILE, "Mark Unread",
                       HUB_CONTEXT_STATE_READ, UDS_PLACEMENT_DEFAULT);

    return true;
}

void HubIntegration::upsertThreadItem(const QString &threadId, bool isGroup,
                                       const QString &title, const QString &preview,
                                       qint64 timestampMs)
{
    if (threadId.isEmpty()) return;
    if (!init()) return;

    m_threadItemState.remove(threadId);
    m_unreadCounts.remove(threadId);
    ensureState(threadId);
    int unread = m_unreadCounts.value(threadId, 0) + 1;
    m_unreadCounts[threadId] = unread;

    QByteArray threadIdUtf8 = threadId.toUtf8();
    QByteArray titleUtf8    = title.toUtf8();
    QByteArray previewUtf8  = preview.toUtf8();

    uds_inbox_item_data_t *item = uds_inbox_item_data_create();
    uds_inbox_item_data_set_account_id(item, ACCOUNT_ID);
    uds_inbox_item_data_set_source_id(item, const_cast<char*>(threadIdUtf8.constData()));
    uds_inbox_item_data_set_name(item, titleUtf8.constData());
    uds_inbox_item_data_set_description(item, previewUtf8.constData());
    uds_inbox_item_data_set_icon(item, HUB_ICON_UNREAD_FILE);
    uds_inbox_item_data_set_mime_type(item, HUB_MIME_TYPE_MESSAGE);
    uds_inbox_item_data_set_category_id(item, HUB_CATEGORY_ID);
    uds_inbox_item_data_set_timestamp(item, timestampMs);
    uds_inbox_item_data_set_unread_count(item, unread);
    uds_inbox_item_data_set_total_count(item, unread);
    uds_inbox_item_data_set_context_state(item, contextStateFor(true));
    // true: this item is the ONLY source of the Hub row and of the alert (banner/sound/instant
    // preview). A parallel Notification::notify() duplicated rows, and notification_state doesn't
    // control row visibility, only the alert.
    uds_inbox_item_data_set_notification_state(item, true);

    // Try update first, then add: m_knownThreadIds only lives for this session and Hub has no query
    // API to check whether the item exists from an earlier one.
    int rc = uds_item_updated(m_udsHandle, item);
    if (rc != UDS_SUCCESS) {
        rc = uds_item_added(m_udsHandle, item);
    }
    if (rc == UDS_SUCCESS) {
        m_knownThreadIds.insert(threadId);
        // Keep the full state sent: uds_item_updated() replaces the whole record, so markThreadRead()
        // must rebuild every field (see ThreadItemState).
        ThreadItemState st;
        st.title = title;
        st.preview = preview;
        st.timestampMs = timestampMs;
        st.isGroup = isGroup;
        st.unread = unread;
        st.total  = unread;
        m_threadItemState[threadId] = st;
        saveState(threadId);
    }
    uds_inbox_item_data_destroy(item);

    if (rc != UDS_SUCCESS) {
        qWarning() << "[Hub] upsertThreadItem failed for thread" << threadId
                  << "isGroup=" << isGroup << "rc=" << rc;
    }
}

void HubIntegration::markThreadRead(const QString &threadId)
{
    if (threadId.isEmpty() || !m_ready) return;
    if (!ensureState(threadId)) return;
    if (m_threadItemState[threadId].unread == 0 && m_unreadCounts.value(threadId, 0) == 0)
        return;

    m_unreadCounts[threadId] = 0;
    m_threadItemState[threadId].unread = 0;
    saveState(threadId);

    if (!publishState(threadId, false , false )) {
    }
}

void HubIntegration::markThreadUnread(const QString &threadId)
{
    if (threadId.isEmpty()) return;
    if (!ensureState(threadId)) {
        return;
    }
    if (m_threadItemState[threadId].unread < 1) m_threadItemState[threadId].unread = 1;
    if (m_threadItemState[threadId].total  < 1) m_threadItemState[threadId].total  = 1;
    m_unreadCounts[threadId] = m_threadItemState[threadId].unread;
    saveState(threadId);
    publishState(threadId, false , true);
}

bool HubIntegration::ensureState(const QString &threadId)
{
    if (m_threadItemState.contains(threadId)) return true;
    QSettings s("BerryLife", "Zalo10");
    s.sync();
    s.beginGroup("hubItems");
    s.beginGroup(threadId);
    if (!s.contains("title")) return false;
    ThreadItemState st;
    st.title       = s.value("title").toString();
    st.preview     = s.value("preview").toString();
    st.timestampMs = s.value("ts").toLongLong();
    st.isGroup     = s.value("group", false).toBool();
    st.unread      = s.value("unread", 0).toInt();
    st.total       = s.value("total", qMax(st.unread, 1)).toInt();
    m_threadItemState[threadId] = st;
    if (!m_unreadCounts.contains(threadId)) m_unreadCounts[threadId] = st.unread;
    return true;
}

void HubIntegration::saveState(const QString &threadId)
{
    if (!m_threadItemState.contains(threadId)) return;
    const ThreadItemState &st = m_threadItemState[threadId];
    QSettings s("BerryLife", "Zalo10");
    s.beginGroup("hubItems");
    s.beginGroup(threadId);
    s.setValue("title", st.title);
    s.setValue("preview", st.preview);
    s.setValue("ts", st.timestampMs);
    s.setValue("group", st.isGroup);
    s.setValue("unread", st.unread);
    s.setValue("total", st.total);
}

void HubIntegration::forgetState(const QString &threadId)
{
    QSettings s("BerryLife", "Zalo10");
    s.beginGroup("hubItems");
    s.remove(threadId);
    m_knownThreadIds.remove(threadId);
    m_unreadCounts.remove(threadId);
    m_threadItemState.remove(threadId);
}

bool HubIntegration::publishState(const QString &threadId, bool notify, bool allowAdd)
{
    if (!init()) return false;
    if (!ensureState(threadId)) return false;
    const ThreadItemState st = m_threadItemState[threadId];

    QByteArray threadIdUtf8 = threadId.toUtf8();
    QByteArray titleUtf8    = st.title.toUtf8();
    QByteArray previewUtf8  = st.preview.toUtf8();

    uds_inbox_item_data_t *item = uds_inbox_item_data_create();
    uds_inbox_item_data_set_account_id(item, ACCOUNT_ID);
    uds_inbox_item_data_set_source_id(item, const_cast<char*>(threadIdUtf8.constData()));
    uds_inbox_item_data_set_name(item, titleUtf8.constData());
    uds_inbox_item_data_set_description(item, previewUtf8.constData());
    uds_inbox_item_data_set_icon(item, st.unread > 0 ? HUB_ICON_UNREAD_FILE : HUB_ICON_READ_FILE);
    uds_inbox_item_data_set_mime_type(item, HUB_MIME_TYPE_MESSAGE);
    uds_inbox_item_data_set_category_id(item, HUB_CATEGORY_ID);
    uds_inbox_item_data_set_timestamp(item, st.timestampMs);
    uds_inbox_item_data_set_unread_count(item, st.unread);
    uds_inbox_item_data_set_total_count(item, qMax(st.total, qMax(st.unread, 1)));
    uds_inbox_item_data_set_context_state(item, contextStateFor(st.unread > 0));
    uds_inbox_item_data_set_notification_state(item, notify);

    int rc = uds_item_updated(m_udsHandle, item);
    if (rc != UDS_SUCCESS && allowAdd) rc = uds_item_added(m_udsHandle, item);
    uds_inbox_item_data_destroy(item);

    if (rc == UDS_SUCCESS) m_knownThreadIds.insert(threadId);
    return rc == UDS_SUCCESS;
}

void HubIntegration::removeThreadItem(const QString &threadId)
{
    if (threadId.isEmpty()) return;

    if (m_ready) {
        QByteArray threadIdUtf8 = threadId.toUtf8();
        int rc = uds_item_removed(m_udsHandle, ACCOUNT_ID, const_cast<char*>(threadIdUtf8.constData()));
        if (rc != UDS_SUCCESS) {
            // The item may already be gone in Hub (account recreated at startup); still clear the saved state.
        }
    }
    forgetState(threadId);
}

bool HubIntegration::isGroupThread(const QString &threadId) const
{
    QMap<QString, ThreadItemState>::const_iterator it = m_threadItemState.find(threadId);
    if (it == m_threadItemState.end()) {
        QSettings s("BerryLife", "Zalo10");
        return s.value(QString("hubItems/%1/group").arg(threadId), false).toBool();
    }
    return it.value().isGroup;
}
