import bb.cascades 1.4
import bb.system 1.0
import QtQuick 1.0
import "lists"
import "lists/avatarhub.js" as AvatarHub

NavigationPane {
    id: chatsNav
    peekEnabled: false

    onPopTransitionEnded: {
        zService.clearActiveThread();
    }

    onPushTransitionEnded: {
        if (page && typeof page.flushPendingImages === "function") {
            page.pageVisible = true;
            page.flushPendingImages();
        }
    }

    property bool searchVisible: false
    property string searchText: ""
    // Chats whose online dot is lit, shown as the Chats tab subtitle ("N Active Chats") by main.qml.
    // Maintained by friendList.setOnline() / startPresence().
    property int onlineChatCount: 0
    // Chats that still have unread messages (yellow pellet). main.qml shows the tab's red spark
    // while this is above 0, so the spark only goes away once those chats have been read.
    property int unreadCount: 0
    function recountUnread() {
        var n = 0;
        for (var k = 0; k < friendModel.size(); k++) {
            if (friendModel.value(k).hasUnread === true) n++;
        }
        chatsNav.unreadCount = n;
    }
    property variant activeChatPage: null
    property variant activeQmPage: null
    property variant activeGroupBoardPage: null

    attachedObjects: [
        // No warm-up Timer pre-parsing ChatView.qml via createObject()+destroy(): creating and
        // destroying a parentless Page UIObject caused SIGSEGV on this runtime.
    ]

    Page {
        id: chatsPage

        titleBar: ProfileTitleBar {
            selfName:   chatsNav.selfName
            selfAvatar: chatsNav.selfAvatar
            searchHint: "Search chats..."
            searchMode: chatsNav.searchVisible
            showSearchButton: true
            addIcon: "asset:///images/ChatsTab/ic_start_chat.png"
            addDialogTitle: "New Chat"
            addHandled: true
            onAddClicked: chatsNav.openNewChat()
            onSearchOpened: { chatsNav.searchVisible = true; focusSearch(); }
            onSearchChanged: { chatsNav.searchText = text; chatsNav.filterList(); }
            onSearchClosed: {
                chatsNav.searchText = "";
                chatsNav.searchVisible = false;
                chatsNav.filterList();
            }
        }

        shortcuts: [
            Shortcut {
                key: "s"
                onTriggered: {
                    chatsNav.searchVisible = !chatsNav.searchVisible;
                    if (!chatsNav.searchVisible) {
                        chatsNav.searchText = "";
                        chatsNav.filterList();
                    }
                }
            }
        ]

        actions: [
            ActionItem {
                title: "Mark All as Read"
                imageSource: "asset:///images/ChatsTab/ai_add_task.png"
                ActionBar.placement: ActionBarPlacement.InOverflow
                onTriggered: { markAllReadDialog.show() }
            },
            ActionItem {
                title: "Edit Status"
                imageSource: "asset:///images/ChatsTab/edit.png"
                ActionBar.placement: ActionBarPlacement.InOverflow
                onTriggered: { editStatusDialog.show() }
            }
        ]

        content: Container {
            layout: DockLayout {}
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment: VerticalAlignment.Fill

            FocusSink {}

            ListView {
                id: friendList
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment: VerticalAlignment.Fill
                // Stay on the full list until something is typed: the search model is empty before the
                // first keystroke, which showed a blank white page on the first search.
                dataModel: (chatsNav.searchVisible && chatsNav.searchText.trim().length > 0) ? searchModel : friendModel

                // Online dot next to the name: lastActionTime of the first few chats, one request every 1.5s.
                // Read by the delegate through ListItem.view (delegates can't see outer ids).
                property variant onlineMap: ({})
                property bool isDark: app.getDarkTheme()   // read by rows through ListItem.view
                property int     presenceIdx: 0
                function startPresence() {
                    friendList.onlineMap = ({});
                    chatsNav.onlineChatCount = 0;
                    friendList.presenceIdx = 0;
                    presenceTimer.restart();
                }
                function setOnline(uid, lastMs) {
                    lastMs = Number(lastMs);
                    if (lastMs > 0 && lastMs < 100000000000) lastMs = lastMs * 1000;
                    var on = (lastMs > 0) && ((Date.now() - lastMs) < 5 * 60 * 1000);
                    var m = friendList.onlineMap;
                    if ((m[uid] === true) === on) return;
                    var nm = {};
                    for (var k in m) nm[k] = m[k];
                    nm[uid] = on;
                    friendList.onlineMap = nm;
                    var n = 0;
                    for (var ok in nm) { if (nm[ok] === true) n++; }
                    chatsNav.onlineChatCount = n;
                }

                function itemType(data, indexPath) { return "item"; }

                // Proxy: ListItemComponent delegates can't resolve the parent Page/NavigationPane id (calling
                // chatsNav.lastMessagePreview threw "Can't find variable: chatsNav" and hid the preview).
                // Delegates call ListItem.view.lastMessagePreview(...) instead.
                function lastMessagePreview(lastMessage, lastMsg, lastMsgIsMine, lastSenderName) {
                    return chatsNav.lastMessagePreview(lastMessage, lastMsg, lastMsgIsMine, lastSenderName);
                }

                listItemComponents: [
                    ListItemComponent {
                        type: "item"
                        ChatsListItem {}
                    }
                ]

                onTriggered: {
                    chatsNav.openRow(dataModel.data(indexPath));
                }
            }

            Label {
                id: chatsEmpty
                text: "No conversations yet.\nTap the new chat button to start one."
                multiline: true
                visible: false
                textStyle.textAlign: TextAlign.Center
                horizontalAlignment: HorizontalAlignment.Center
                verticalAlignment: VerticalAlignment.Center
            }
        }

        attachedObjects: [
            ArrayDataModel {
                id: friendModel
                // Any change (new message, read, rebuild) re-counts the unread chats, once things settle.
                onItemAdded:    { unreadRecountTimer.restart(); }
                onItemRemoved:  { unreadRecountTimer.restart(); }
                onItemUpdated:  { unreadRecountTimer.restart(); }
                onItemsChanged: { unreadRecountTimer.restart(); }
            },
            Timer {
                id: unreadRecountTimer
                interval: 250
                repeat: false
                onTriggered: { chatsNav.recountUnread(); }
            },
            Timer {
                id: presenceTimer
                interval: 1500
                repeat: true
                onTriggered: {
                    if (friendList.presenceIdx >= 8 || friendList.presenceIdx >= friendModel.size()) { presenceTimer.stop(); return; }
                    var d = friendModel.value(friendList.presenceIdx);
                    friendList.presenceIdx++;
                    var uid = d ? (d.threadId || d.uid || "") : "";
                    if (uid.length > 0) zService.fetchProfileDetails(uid);
                }
            },
            Timer {
                id: friendAppendTimer
                interval: 40
                repeat: true
                onTriggered: {
                    chatsNav.appendPending(100);
                    if (chatsNav.pendingPos >= chatsNav.pendingRows.length) friendAppendTimer.stop();
                }
            },
            Timer {
                // Lite: hands avatar requests to zService at a steady pace (see lists/avatarhub.js).
                id: avatarPaceTimer
                interval: 300   // = AvatarHub.TICK_MS
                repeat: true
                onTriggered: {
                    if (AvatarHub.drain("chats", zService, Date.now()) === 0) avatarPaceTimer.stop();
                }
            },
            Timer {
                // Fills in the name / picture of chats that have none yet (people who are not friends),
                // one profile request at a time.
                id: profileFillTimer
                interval: 1200
                repeat: true
                onTriggered: chatsNav.fillNextProfile()
            },
            NewChatSheet {
                id: newChatSheet
                isDark: friendList.isDark
                onChatRequested: chatsNav.startNewChat(threadId, name, avatar, localAvatar)
            },
            ConfirmDialog {
                id: markAllReadDialog
                title: "Mark All as Read"
                body: "Mark every unread chat and group as read, here and in the official Zalo app?"
                confirmLabel: "Mark all"
                onConfirmed: { zService.markAllThreadsRead(); }
            },
            SystemDialog {
                id: editStatusDialog
                title: "Edit Status"
                body: "This feature is still under development."
                confirmButton.label: "OK"
                cancelButton.label: ""
                cancelButton.enabled: false
            },
            ArrayDataModel { id: searchModel },
            ComponentDefinition {
                id: chatsDef
                source: "asset:///ChatView.qml"
            },
            ComponentDefinition {
                id: qmPageDef
                source: "asset:///QuickMessagesSheet.qml"
            },
            ComponentDefinition {
                id: groupBoardPageDef
                source: "asset:///GroupBoardSheet.qml"
            },
            ComponentDefinition {
                id: stickerShopPageDef
                source: "asset:///sticker/StickerShopPage.qml"
            },
            ComponentDefinition {
                id: stickerSettingsPageDef
                source: "asset:///sticker/StickerSettingsPage.qml"
            },
            Connections {
                target: app
                onDarkThemeChanged: { friendList.isDark = dark; }
            },
            Connections {
                // target starts null and is assigned in openThread(): binding to chatsNav.activeChatPage warns
                // "Cannot assign to non-existent property" while it is null.
                id: chatPageConn
                target: null
                onGroupBoardRequestedChanged: {
                    if (!chatsNav.activeChatPage || !chatsNav.activeChatPage.groupBoardRequested) return;
                    chatsNav.activeChatPage.groupBoardRequested = false;
                    var boardPage = groupBoardPageDef.createObject();
                    if (!boardPage) return;
                    boardPage.groupId = chatsNav.activeChatPage.threadId;
                    boardPage.groupName = chatsNav.activeChatPage.threadName;
                    boardPage.groupsNavRef = chatsNav;
                    chatsNav.activeGroupBoardPage = boardPage;
                    chatsNav.push(boardPage);
                }
                onStickerShopRequestedChanged: {
                    if (!chatsNav.activeChatPage || !chatsNav.activeChatPage.stickerShopRequested) return;
                    chatsNav.activeChatPage.stickerShopRequested = false;
                    var shopPage = stickerShopPageDef.createObject();
                    if (shopPage) chatsNav.push(shopPage);
                }
                onStickerSettingsRequestedChanged: {
                    if (!chatsNav.activeChatPage || !chatsNav.activeChatPage.stickerSettingsRequested) return;
                    chatsNav.activeChatPage.stickerSettingsRequested = false;
                    var stPage = stickerSettingsPageDef.createObject();
                    if (stPage) chatsNav.push(stPage);
                }
                onQmRequestedChanged: {
                    if (!chatsNav.activeChatPage || !chatsNav.activeChatPage.qmRequested) return;
                    chatsNav.activeChatPage.qmRequested = false;
                    var qmPage = qmPageDef.createObject();
                    if (!qmPage) return;
                    chatsNav.activeQmPage = qmPage;
                    qmPageConn.target = qmPage;
                    chatsNav.push(qmPage);
                }
            },
            Connections {
                id: qmPageConn
                target: null
                onUseInChatRequestedChanged: {
                    if (!chatsNav.activeQmPage || !chatsNav.activeQmPage.useInChatRequested) return;
                    var content = chatsNav.activeQmPage.insertRequestedContent;
                    chatsNav.pop();
                    if (chatsNav.activeChatPage) chatsNav.activeChatPage.applyQuickMessage(content);
                }
            },
            ChatsServiceConnections {}
        ]
    }

    property string selfName: ""
    property string selfAvatar: ""
    signal onUnreadMessage()

    // People opened from the New Chat sheet (or a Hub link) before their first message: the row that
    // the first message creates takes the name / picture from here. threadId -> {name, avatar, localAvatar}.
    property variant knownPeople: ({})
    // uids whose profile (name / picture) still has to be read, and the ones already asked this session.
    property variant profileQueue: []
    property variant profileAsked: ({})

    // Big lists go into the model in slices (first slice at once, the rest every few ms) so the UI
    // thread never stalls on a few thousand appends.
    property variant pendingRows: []
    property int pendingPos: 0
    property bool pendingRestore: false

    function appendPending(n) {
        var rows = chatsNav.pendingRows;
        var end = Math.min(rows.length, chatsNav.pendingPos + n);
        for (var j = chatsNav.pendingPos; j < end; j++) {
            var ff = rows[j];
            friendModel.append(ff);
            var furl = ff.avatar || "";
            var ftid = ff.threadId || ff.uid || "";
            if (furl.length > 0 && ftid.length > 0 && !(chatsNav.pendingRestore && ff.localAvatar))
                AvatarHub.enqueue("chats", ftid, furl);
        }
        chatsNav.pendingPos = end;
        if (AvatarHub.pending("chats") && !avatarPaceTimer.running) avatarPaceTimer.start();
    }

    // Chats are the 1-1 threads that have stored messages (zService.cachedChatThreads(), local database
    // only). Nobody is listed just for being a friend, and no friend list is fetched here.
    function loadChats(restoreAvatars) {
        chatsNav.buildChats(zService.cachedChatThreads(), restoreAvatars);
        friendList.startPresence();
    }

    function buildChats(threads, restoreAvatars) {
        friendAppendTimer.stop();
        chatsNav.pendingRows = [];
        chatsNav.pendingPos = 0;
        AvatarHub.reset("chats");
        AvatarHub.invalidate("chats");
        var avatarByTid = {};
        if (restoreAvatars) {
            for (var a = 0; a < friendModel.size(); a++) {
                var ad = friendModel.value(a);
                var atid = ad.threadId || ad.uid || "";
                if (atid.length > 0 && ad.localAvatar) avatarByTid[atid] = ad.localAvatar;
            }
        }
        var pinnedIds = zService.pinnedThreadIds();
        var pinMap = {};
        for (var pn = 0; pn < pinnedIds.length; pn++) pinMap[pinnedIds[pn]] = true;
        friendModel.clear();
        var lastMsgs = zService.getThreadLastMessages();
        var unreadIds = zService.unreadThreadIds();
        var unreadMap = {};
        for (var u = 0; u < unreadIds.length; u++) unreadMap[unreadIds[u]] = true;
        // Build a fresh array: `threads` is a QVariantList from C++ and in-place writes
        // silently no-op on this runtime.
        var outArr = [];
        var needProfile = [];
        for (var i = 0; i < threads.length; i++) {
            var f = threads[i];
            var tid = f.threadId || f.uid || "";
            var lm = lastMsgs[tid];
            if (!lm) continue;   // a conversation is listed only when it has a message
            var out = {};
            for (var key in f) out[key] = f[key];
            out.threadId       = tid;
            out.isPinned       = (pinMap[tid] === true);
            out.hasUnread      = false;
            out.lastMsgIsMine  = false;
            out.lastMsgId      = "";
            out.lastMsgState   = 0;
            out.lastSenderName = "";
            out.lastMessage    = "";
            out.sortTs         = 0;
            if (!out.localAvatar) out.localAvatar = "";
            if (restoreAvatars && avatarByTid[tid]) out.localAvatar = avatarByTid[tid];
            var isMine = (lm.isMine === true || lm.isMine === "true" || lm.isMine === 1);
            var mt = lm.msgType;
            var snippet;
            if (mt === 99 || mt === "99") {
                snippet = isMine ? "You recalled a message" : "This message was recalled";
            } else if (mt === 2 || mt === "2") {
                snippet = "[Photo]";
            } else {
                snippet = chatsNav.msgSnippet(mt, lm.content);
            }
            out.lastMessage    = snippet;
            out.lastMsgIsMine  = isMine;
            out.lastMsgId      = lm.msgId || "";
            out.lastMsgState   = isMine ? (lm.state || 1) : 0;
            out.lastSenderName = lm.dName || "";
            out.lastTime       = lm.ts || "";
            out.hasUnread = (unreadMap[tid] === true) && !isMine;
            if (out.lastTime && out.lastTime !== "") {
                var ts = parseInt(out.lastTime);
                if (!isNaN(ts)) {
                    if (ts > 0 && ts < 100000000000) ts = ts * 1000;   // seconds -> ms so both sort together
                    out.sortTs = ts;
                    out.lastTime = chatsNav.formatTime(ts);
                }
            }
            if (!out.name || out.name.length === 0 || (!out.avatar && !out.localAvatar))
                needProfile.push(tid);
            outArr.push(out);
        }
        outArr.sort(function(a, b) {
            var pa = a.isPinned ? 1 : 0, pb = b.isPinned ? 1 : 0;
            if (pa !== pb) return pb - pa;
            return (b.sortTs || 0) - (a.sortTs || 0);
        });
        chatsNav.pendingRows = outArr;
        chatsNav.pendingPos = 0;
        chatsNav.pendingRestore = restoreAvatars;
        chatsNav.appendPending(60);
        if (chatsNav.pendingPos < outArr.length) friendAppendTimer.restart();
        chatsEmpty.visible = (outArr.length === 0);
        for (var np = 0; np < needProfile.length && np < 40; np++) chatsNav.queueProfile(needProfile[np]);
    }

    // ---- Names / pictures of people who are not in a friend list ----
    function queueProfile(uid) {
        if (!uid || uid.length === 0 || chatsNav.profileAsked[uid] === true) return;
        var asked = {};
        for (var k in chatsNav.profileAsked) asked[k] = true;
        asked[uid] = true;
        chatsNav.profileAsked = asked;
        chatsNav.profileQueue = chatsNav.profileQueue.concat([uid]);
        if (!profileFillTimer.running) profileFillTimer.start();
    }
    function fillNextProfile() {
        var q = chatsNav.profileQueue;
        if (q.length === 0) { profileFillTimer.stop(); return; }
        chatsNav.profileQueue = q.slice(1);
        zService.fetchProfileDetails(q[0]);
    }
    function applyProfile(uid, profile) {
        var hit = AvatarHub.find("chats", friendModel, uid);
        if (!hit) return;
        var row = hit.row;
        var changed = false;
        var nm = profile.zaloName || profile.displayName || "";
        if (nm.length > 0 && (!row.name || row.name.length === 0)) { row.name = nm; changed = true; }
        var av = profile.avatar || "";
        var wantAvatar = (av.length > 0 && !row.avatar && !row.localAvatar);
        if (wantAvatar) { row.avatar = av; changed = true; }
        if (changed) friendModel.replace(hit.i, row);
        if (wantAvatar) {
            AvatarHub.enqueue("chats", uid, av);
            if (!avatarPaceTimer.running) avatarPaceTimer.start();
        }
    }

    function requestAvatar(tid, url) {
        AvatarHub.enqueue("chats", tid, url);
        if (!avatarPaceTimer.running) avatarPaceTimer.start();
    }
    // The model's membership changed (row added / removed): the avatar index and the empty hint follow.
    function rowsChanged() {
        AvatarHub.invalidate("chats");
        chatsEmpty.visible = (friendModel.size() === 0);
        if (chatsNav.searchVisible) chatsNav.filterList();
    }
    function removeThread(tid) {
        for (var i = 0; i < friendModel.size(); i++) {
            var d = friendModel.value(i);
            if ((d.threadId || d.uid || "") === tid) {
                friendModel.removeAt(i);
                chatsNav.rowsChanged();
                return;
            }
        }
    }

    function rememberPerson(tid, name, avatar, localAvatar) {
        if (!tid || tid.length === 0) return;
        var m = {};
        for (var k in chatsNav.knownPeople) m[k] = chatsNav.knownPeople[k];
        m[tid] = { name: name || "", avatar: avatar || "", localAvatar: localAvatar || "" };
        chatsNav.knownPeople = m;
    }
    // Name / picture for a thread that has no row yet (first message of a new chat).
    function personFor(tid, fallbackName) {
        var kp = chatsNav.knownPeople[tid];
        var name = (kp && kp.name) ? kp.name : "";
        if (name.length === 0) name = zService.memberDisplayName(tid) || "";
        if (name.length === 0) name = fallbackName || "";
        return {
            name: name,
            avatar: kp ? kp.avatar : "",
            localAvatar: kp ? kp.localAvatar : ""
        };
    }

    // ---- Lite: avatar updates in O(1) instead of scanning the whole list ----
    function applyAvatar(tid, localPath) {
        AvatarHub.arrived("chats", tid);
        var hit = AvatarHub.find("chats", friendModel, tid);
        if (hit && hit.row.localAvatar !== localPath) {
            hit.row.localAvatar = localPath;
            friendModel.replace(hit.i, hit.row);
        }
        if (chatsNav.searchVisible) {
            var sh = AvatarHub.find("chatsSearch", searchModel, tid);
            if (sh && sh.row.localAvatar !== localPath) {
                sh.row.localAvatar = localPath;
                searchModel.replace(sh.i, sh.row);
            }
        }
    }

    function firstUnpinnedIndex() {
        var n = 0;
        for (var k = 0; k < friendModel.size(); k++) {
            if (friendModel.value(k).isPinned) n++; else break;
        }
        return n;
    }

    // A pinned chat that just got a message goes to the top of the pinned block.
    function firstPinnedSlot(tid) { return 0; }

    function filterList() {
        var q = chatsNav.searchText.toLowerCase().trim();
        searchModel.clear();
        AvatarHub.invalidate("chatsSearch");
        for (var i = 0; i < friendModel.size(); i++) {
            var f = friendModel.value(i);
            if (q.length === 0) {
                searchModel.append(f);
            } else {
                var name = (f.name || f.displayName || "").toLowerCase();
                if (name.indexOf(q) !== -1) searchModel.append(f);
            }
        }
        chatsEmpty.visible = (friendModel.size() === 0);
    }

    function openNewChat() {
        newChatSheet.openSheet();
    }

    // A person picked in the New Chat sheet: open an empty chat. She is listed in Chats only once the
    // first message exists (the new-message handler creates the row).
    function startNewChat(threadId, name, avatar, localAvatar) {
        chatsNav.rememberPerson(threadId, name, avatar, localAvatar);
        newChatSheet.close();
        chatsNav.openThread(threadId, false, name, localAvatar || avatar);
    }

    function openRow(item) {
        if (!item) return;
        var page = chatsDef.createObject();
        if (!page) return;
        var tid = item.threadId || item.uid || "";
        page.threadId   = tid;
        page.threadName = item.name || "Chat";
        page.isGroup    = false;
        page.avatarUrl  = item.localAvatar || item.avatar || "";
        page.selfName   = chatsNav.selfName;
        var hit = AvatarHub.find("chats", friendModel, tid);
        if (hit && hit.row.hasUnread) { hit.row.hasUnread = false; friendModel.replace(hit.i, hit.row); }
        chatsNav.activeChatPage = page;
        chatPageConn.target = page;
        chatsNav.push(page);
        page.startChat();
    }

    function openThread(threadId, isGroup, nameHint, avatarHint) {
        while (chatsNav.count() > 1) {
            chatsNav.pop();
        }
        var page = chatsDef.createObject();
        if (!page) return;
        var threadName = "";
        var avatarUrl  = "";
        var hit = AvatarHub.find("chats", friendModel, threadId);
        if (hit) {
            threadName = hit.row.name || hit.row.displayName || "";
            avatarUrl  = hit.row.localAvatar || hit.row.avatar || "";
        }
        if (threadName.length === 0 || avatarUrl.length === 0) {
            var pf = chatsNav.personFor(threadId, nameHint || "");
            if (threadName.length === 0) threadName = pf.name;
            if (avatarUrl.length === 0)  avatarUrl  = pf.localAvatar || pf.avatar || avatarHint || "";
        }
        if (threadName.length === 0) threadName = "Chat";
        page.threadId   = threadId;
        page.threadName = threadName;
        page.isGroup    = isGroup;
        page.avatarUrl  = avatarUrl;
        page.selfName   = chatsNav.selfName;
        chatsNav.activeChatPage = page;
        chatPageConn.target = page;
        chatsNav.push(page);
        page.startChat();
    }

    function formatTime(timestamp) {
        if (!timestamp || timestamp === "") return "";
        var date = new Date(timestamp * 1);
        var now  = new Date();
        if (date.toDateString() === now.toDateString()) {
            var h = date.getHours(), m = date.getMinutes();
            var ampm = h >= 12 ? "PM" : "AM";
            h = h % 12 || 12;
            return h + ":" + (m < 10 ? "0" : "") + m + " " + ampm;
        }
        if ((now - date) < 7 * 24 * 60 * 60 * 1000) {
            return ["Sun","Mon","Tue","Wed","Thu","Fri","Sat"][date.getDay()];
        }
        var mon = ["Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"][date.getMonth()];
        return mon + " " + date.getDate();
    }

    function extractFileName(content) {
        var c = content || "";
        if (c.length === 0 || c.charCodeAt(0) !== 123) return "";
        var key = '"fileName":"';
        var si = c.indexOf(key);
        if (si < 0) return "";
        si += key.length;
        var ei = si;
        while (ei < c.length) {
            var code = c.charCodeAt(ei);
            if (code === 92) { ei += 2; continue; }
            if (code === 34) break;
            ei++;
        }
        return c.substring(si, ei);
    }
    function msgSnippet(mt, content) {
        if (mt === 4 || mt === "4") {
            var c = content || "";
            var missed = c.indexOf('"callResult":"missed"') >= 0;
            var video  = c.indexOf('"callKind":"video"') >= 0;
            if (missed) return video ? "Missed video call" : "Missed call";
            return video ? "Video call" : "Voice call";
        }
        if (mt === 5 || mt === "5") return "[Sticker]";
        if (mt === 6 || mt === "6") {
            var lc = content || "";
            var lt = "";
            var li = lc.indexOf('"linkTitle":"');
            if (li >= 0) {
                li += 13;
                var le = lc.indexOf('"', li);
                if (le > li) lt = lc.substring(li, le);
            }
            return lt.length > 0 ? lt.substring(0, 60) : "[Link]";
        }
        if (mt !== 3 && mt !== "3") return (content || "").substring(0, 60);
        var fname = chatsNav.extractFileName(content);
        var ext = fname.substring(fname.lastIndexOf('.') + 1).toLowerCase();
        var isVideo = (ext === "mp4" || ext === "mov" || ext === "3gp" || ext === "mkv");
        if (isVideo) return "[Video]";
        return fname.length > 0 ? fname : "[File]";
    }
    // Preview line as a function: block-body bindings on Label.text are invalid in QtQuick1/Cascades
    // (see videoBubble in ChatView.qml).
    function lastMessagePreview(lastMessage, lastMsg, lastMsgIsMine, lastSenderName) {
        var lm = lastMessage || lastMsg || "";
        if (lm.length === 0) return "No messages yet";
        var prefix = "";
        if (lastMsgIsMine === true || lastMsgIsMine === "true") {
            prefix = "Me: ";
        } else if (lastSenderName && lastSenderName.length > 0) {
            prefix = lastSenderName.split(" ")[0] + ": ";
        }
        return prefix + lm;
    }
}
