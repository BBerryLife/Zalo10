import bb.cascades 1.4
import bb.system 1.0
import QtQuick 1.0
import "lists"

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
    property bool refreshCooldown: false
    property variant activeChatPage: null
    property variant activeQmPage: null

    attachedObjects: [
        Timer {
            id: chatsRefreshCooldownTimer
            interval: 11000
            repeat: false
            onTriggered: chatsNav.refreshCooldown = false
        }
        // No warm-up Timer pre-parsing ChatView.qml via createObject()+destroy(): creating and
        // destroying a parentless Page UIObject caused SIGSEGV on this runtime.
    ]

    Page {
        id: chatsPage

        titleBar: ChatsTitleBar {}

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
                title: chatsNav.refreshCooldown ? "Please wait..." : "Refresh"
                enabled: !chatsNav.refreshCooldown
                imageSource: "asset:///images/ChatsTab/ic_sync.png"
                ActionBar.placement: ActionBarPlacement.OnBar
                onTriggered: {
                    if (chatsNav.refreshCooldown) return;
                    chatsNav.refreshCooldown = true;
                    chatsRefreshCooldownTimer.restart();
                    friendModel.clear();
                    zService.fetchFriends();
                    chatsLoading.visible = true;
                }
            },
            ActionItem {
                title: "Search"
                imageSource: "asset:///images/ChatsTab/action_icon_search.png"
                ActionBar.placement: ActionBarPlacement.InOverflow
                onTriggered: {
                    chatsNav.searchVisible = !chatsNav.searchVisible;
                    if (!chatsNav.searchVisible) {
                        chatsNav.searchText = "";
                        chatsNav.filterList();
                    }
                }
            },
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

            ListView {
                id: friendList
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment: VerticalAlignment.Fill
                dataModel: chatsNav.searchVisible ? searchModel : friendModel

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
                    var t0 = Date.now();
                    var item = dataModel.data(indexPath);
                    var page = chatsDef.createObject();
                    if (!page) return;
                    page.threadId   = item.threadId || item.uid || "";
                    page.threadName = item.name || "Chat";
                    page.isGroup    = false;
                    page.avatarUrl  = item.localAvatar || item.avatar || "";
                    page.selfName   = chatsNav.selfName;
                    var idx = indexPath[0];
                    var d = friendModel.value(idx);
                    if (d) { d.hasUnread = false; friendModel.replace(idx, d); }
                    chatsNav.activeChatPage = page;
                    chatPageConn.target = page;
                    var t1 = Date.now();
                    chatsNav.push(page);
                    page.startChat();
                }
            }

            ActivityIndicator {
                id: chatsLoading
                horizontalAlignment: HorizontalAlignment.Center
                verticalAlignment: VerticalAlignment.Center
                preferredWidth: ui.du(12)
                preferredHeight: ui.du(12)
                running: visible
                visible: false
            }

            Label {
                id: chatsEmpty
                text: "No friends found"
                visible: false
                horizontalAlignment: HorizontalAlignment.Center
                verticalAlignment: VerticalAlignment.Center
            }
        }

        attachedObjects: [
            ArrayDataModel { id: friendModel },
            SystemDialog {
                id: markAllReadDialog
                title: "Mark All as Read"
                body: "This feature is still under development."
                confirmButton.label: "OK"
                cancelButton.label: ""
                cancelButton.enabled: false
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
            Connections {
                // target starts null and is assigned in openThread(): binding to chatsNav.activeChatPage warns
                // "Cannot assign to non-existent property" while it is null.
                id: chatPageConn
                target: null
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
    signal onUnreadMessage()

    property variant lastFriends: []

    function buildFriends(friends, restoreAvatars) {
        chatsLoading.visible = false;
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
        // Build a fresh array: `friends` is a QVariantList from a C++ signal and in-place writes
        // silently no-op on this runtime.
        var outArr = [];
        for (var i = 0; i < friends.length; i++) {
            var f = friends[i];
            var out = {};
            for (var key in f) out[key] = f[key];
            out.localAvatar    = "";
            out.isPinned       = false;
            out.hasUnread      = false;
            out.lastMsgIsMine  = false;
            out.lastMsgId      = "";
            out.lastMsgState   = 0;
            out.lastSenderName = "";
            out.lastMessage    = f.lastMessage || f.lastMsg || "";
            out.sortTs         = 0;
            var tid = out.threadId || out.uid || "";
            out.isPinned = (pinMap[tid] === true);
            if (restoreAvatars && avatarByTid[tid]) out.localAvatar = avatarByTid[tid];
            var lm = lastMsgs[tid];
            if (lm) {
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
            }
            out.hasUnread = (unreadMap[tid] === true) && (out.lastMessage.length > 0)
                            && !out.lastMsgIsMine;
            if (out.lastTime && out.lastTime !== "") {
                var ts = parseInt(out.lastTime);
                if (!isNaN(ts)) {
                    out.sortTs = ts;
                    out.lastTime = chatsNav.formatTime(ts);
                }
            }
            outArr.push(out);
        }
        outArr.sort(function(a, b) {
            var pa = a.isPinned ? 1 : 0, pb = b.isPinned ? 1 : 0;
            if (pa !== pb) return pb - pa;
            return (b.sortTs || 0) - (a.sortTs || 0);
        });
        for (var j = 0; j < outArr.length; j++) {
            var ff = outArr[j];
            friendModel.append(ff);
            var furl = ff.avatar || "";
            var ftid = ff.threadId || ff.uid || "";
            if (furl.length > 0 && ftid.length > 0 && !(restoreAvatars && ff.localAvatar))
                zService.downloadAvatar(ftid, furl);
        }
        chatsEmpty.visible = (outArr.length === 0);

    }

    function firstUnpinnedIndex() {
        var n = 0;
        for (var k = 0; k < friendModel.size(); k++) {
            if (friendModel.value(k).isPinned) n++; else break;
        }
        return n;
    }

    function filterList() {
        var q = chatsNav.searchText.toLowerCase().trim();
        searchModel.clear();
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

    function openThread(threadId, isGroup) {
        while (chatsNav.count() > 1) {
            chatsNav.pop();
        }
        var page = chatsDef.createObject();
        if (!page) return;
        var threadName = "Chat";
        var avatarUrl  = "";
        for (var i = 0; i < friendModel.size(); i++) {
            var d = friendModel.value(i);
            var tid = d.threadId || d.uid || "";
            if (tid === threadId) {
                threadName = d.name || d.displayName || "Chat";
                avatarUrl  = d.localAvatar || d.avatar || "";
                break;
            }
        }
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
