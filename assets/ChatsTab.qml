import bb.cascades 1.4
import bb.system 1.0
import QtQuick 1.0

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
    // Queue of {threadId, url} pending avatar downloads, drained a few at a
    // time by avatarDripTimer instead of firing zService.downloadAvatar() for
    // every friend synchronously in one tick (see onFriendsReady below). A
    // big synchronous burst of enqueueCommand() calls (one per avatar, 80+ on
    // a large friends list) was landing on top of HeadlessService's own
    // command_queue writes in a separate process and occasionally hitting
    // SQLITE_LOCKED — spacing them out avoids piling up that many DB writes
    // in the same instant to begin with.
    property variant pendingAvatarQueue: []

    function queueAvatarDownload(threadId, url) {
        var q = pendingAvatarQueue;
        q.push({ tid: threadId, url: url });
        pendingAvatarQueue = q;
        if (!avatarDripTimer.running) avatarDripTimer.start();
    }

    attachedObjects: [
        Timer {
            id: chatsRefreshCooldownTimer
            interval: 11000
            repeat: false
            onTriggered: chatsNav.refreshCooldown = false
        },
        // Drains pendingAvatarQueue a handful of items at a time — see property
        // comment above for why this exists.
        Timer {
            id: avatarDripTimer
            interval: 60
            repeat: true
            onTriggered: {
                var q = chatsNav.pendingAvatarQueue;
                var batch = 5;
                for (var i = 0; i < batch && q.length > 0; i++) {
                    var item = q.shift();
                    zService.downloadAvatar(item.tid, item.url);
                }
                chatsNav.pendingAvatarQueue = q;
                if (q.length === 0) avatarDripTimer.stop();
            }
        },
        // Forces the (large, ~1400-line) ChatView.qml document to be parsed
        // and its component type cached by the QML engine once, in the
        // background, instead of paying that cost on the user's first real
        // tap into a conversation. Creates a throwaway instance and destroys
        // it immediately — it's never pushed, never given a threadId, and
        // startChat() is never called on it, so it has no visible effect and
        // never touches zService. Delayed so it doesn't compete with the
        // friend list's own initial render.
        Timer {
            id: chatViewWarmupTimer
            interval: 800
            repeat: false
            running: true
            onTriggered: {
                var w = chatsDef.createObject();
                if (w) w.destroy();
            }
        }
    ]

    Page {
        id: chatsPage

        titleBar: TitleBar {
            kind: TitleBarKind.FreeForm
            kindProperties: FreeFormTitleBarKindProperties {
                content: Container {
                    background: Color.create("#2575fc")
                    horizontalAlignment: HorizontalAlignment.Fill
                    verticalAlignment: VerticalAlignment.Fill
                    layout: DockLayout {}
                    leftPadding: ui.du(2.5)
                    rightPadding: ui.du(2.5)
                    Label {
                        text: "Zalo10"
                        textStyle {
                            color: Color.White
                            base: SystemDefaults.TextStyles.TitleText
                            fontWeight: FontWeight.Bold
                        }
                        verticalAlignment: VerticalAlignment.Center
                        horizontalAlignment: HorizontalAlignment.Left
                        visible: !chatsNav.searchVisible
                    }
                    Container {
                        visible: chatsNav.searchVisible
                        horizontalAlignment: HorizontalAlignment.Fill
                        verticalAlignment: VerticalAlignment.Center
                        layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                        TextField {
                            id: chatsSearchField
                            hintText: "Search chats..."
                            verticalAlignment: VerticalAlignment.Center
                            textStyle { color: Color.White }
                            layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                            onTextChanging: {
                                chatsNav.searchText = text;
                                chatsNav.filterList();
                            }
                            onCreationCompleted: {
                                inputMode.type = TextInputFlag.AutoCapitalizationOff | TextInputFlag.AutoCorrectionOff | TextInputFlag.SpellCheckOff | TextInputFlag.PredictionOff;
                            }
                        }
                        Button {
                            text: "Cancel"
                            preferredWidth: ui.du(14)
                            verticalAlignment: VerticalAlignment.Center
                            onClicked: {
                                chatsSearchField.text = "";
                                chatsNav.searchText = "";
                                chatsNav.searchVisible = false;
                                chatsNav.filterList();
                            }
                        }
                    }
                }
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

                listItemComponents: [
                    ListItemComponent {
                        type: "item"
                        CustomListItem {
                            dividerVisible: true
                            Container {
                                layout: DockLayout {}
                                preferredHeight: ui.du(12.0)

                                Container {
                                    preferredWidth:  ui.du(12.0)
                                    preferredHeight: ui.du(12.0)
                                    horizontalAlignment: HorizontalAlignment.Left
                                    verticalAlignment: VerticalAlignment.Center
                                    layout: DockLayout {}
                                    ImageView {
                                        imageSource: ListItemData.localAvatar ? ListItemData.localAvatar : "asset:///images/ChatsTab/blank.png"
                                        horizontalAlignment: HorizontalAlignment.Fill
                                        verticalAlignment: VerticalAlignment.Fill
                                        scalingMethod: ScalingMethod.AspectFill
                                    }
                                }

                                Container {
                                    leftPadding: ui.du(13.0)
                                    rightPadding: ui.du(2.0)
                                    verticalAlignment: VerticalAlignment.Top
                                    layout: StackLayout { orientation: LayoutOrientation.TopToBottom }
                                    horizontalAlignment: HorizontalAlignment.Fill
                                    layoutProperties: StackLayoutProperties { spaceQuota: 1 }

                                    Container {
                                        layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                                        horizontalAlignment: HorizontalAlignment.Fill

                                        Label {
                                            text: ListItemData.name || ListItemData.displayName || "Unknown User"
                                            textStyle { base: SystemDefaults.TextStyles.TitleText }
                                            verticalAlignment: VerticalAlignment.Center
                                            layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                                        }
                                        Label {
                                            text: ListItemData.lastTime || ""
                                            textStyle {
                                                base: SystemDefaults.TextStyles.SubtitleText
                                                color: Color.Gray
                                                fontSize: FontSize.Small
                                            }
                                            verticalAlignment: VerticalAlignment.Center
                                        }
                                    }

                                    Label {
                                        text: {
                                            var lm = ListItemData.lastMessage || ListItemData.lastMsg || "";
                                            if (lm.length === 0) return "No messages yet";
                                            var prefix = "";
                                            if (ListItemData.lastMsgIsMine === true || ListItemData.lastMsgIsMine === "true") {
                                                prefix = "Me: ";
                                            } else if (ListItemData.lastSenderName && ListItemData.lastSenderName.length > 0) {
                                                prefix = ListItemData.lastSenderName.split(" ")[0] + ": ";
                                            }
                                            return prefix + lm;
                                        }
                                        textStyle {
                                            base: SystemDefaults.TextStyles.SubtitleText
                                            color: Color.DarkGray
                                            fontSize: FontSize.Small
                                        }
                                        topMargin: ui.du(1.0)
                                        multiline: false
                                    }
                                }
                            }
                        }
                    }
                ]

                onTriggered: {
                    var t0 = Date.now();
                    var item = dataModel.data(indexPath);
                    var page = chatsDef.createObject();
                    console.log("[ChatsTab] createObject took " + (Date.now() - t0) + "ms");
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
                    // Push first, load second: navigation should never wait on
                    // dbLoadMessages()/fetchMessages() — the conversation opens
                    // immediately and its messages populate right after.
                    var t1 = Date.now();
                    chatsNav.push(page);
                    console.log("[ChatsTab] push() took " + (Date.now() - t1) + "ms, total tap-to-push " + (Date.now() - t0) + "ms");
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
                // target starts null — binding directly to chatsNav.activeChatPage
                // (which is null until a page is pushed) makes Cascades try to
                // validate onQmRequestedChanged against a null target at
                // construction time, which is what caused the "Cannot assign to
                // non-existent property" warning on every launch. Assigned
                // explicitly in JS instead (see openThread() and onTriggered
                // below) once the real page instance exists.
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
            Connections {
                target: zService

                // Xem eventBridgeReconnected() trong ZaloServiceProxy.hpp: mọi
                // avatarReady broadcast trong lúc UI mất kết nối EventBridge bị
                // mất vĩnh viễn. Khi vừa kết nối lại, quét friendModel và phát
                // lại downloadAvatar cho bất kỳ ai chưa có localAvatar — rẻ vì
                // avatar đã tải xong nằm sẵn trên đĩa (avatar_meta cache), nên
                // đây gần như luôn là 1 lần đọc file, không phải tải mạng lại.
                onEventBridgeReconnected: {
                    for (var i = 0; i < friendModel.size(); i++) {
                        var d = friendModel.value(i);
                        if ((!d.localAvatar || d.localAvatar.length === 0) && d.avatar && d.avatar.length > 0 && d.threadId) {
                            chatsNav.queueAvatarDownload(d.threadId, d.avatar);
                        }
                    }
                }

                onFriendsReady: {
                    chatsLoading.visible = false;
                    friendModel.clear();
                    // The server's friends-list API never includes a last-message
                    // field, so without this, every restart shows "No messages yet"
                    // for everyone until a fresh message happens to arrive over the
                    // network. Pull the last message we already have locally for
                    // each thread and merge it in before the model is even built.
                    var lastMsgs = zService.getThreadLastMessages();
                    // Build a fresh plain JS array instead of writing merged fields
                    // back into `friends[i]` in place — `friends` here is backed by
                    // a QVariantList marshalled over from the C++ signal, and this
                    // codebase has hit multiple cases (see Qt4 QScriptEngine notes)
                    // where that bridge doesn't reliably persist array-element
                    // reassignment. Writing back silently no-opped, so the last
                    // message/time (and the sort key) were computed correctly but
                    // then lost before ever reaching friendModel.
                    var outArr = [];
                    for (var i = 0; i < friends.length; i++) {
                        var f = friends[i];
                        var out = {};
                        for (var key in f) out[key] = f[key];
                        out.localAvatar    = "";
                        out.hasUnread      = false;
                        out.lastMsgIsMine  = false;
                        out.lastSenderName = "";
                        out.lastMessage    = f.lastMessage || f.lastMsg || "";
                        out.sortTs         = 0;
                        var tid = out.threadId || out.uid || "";
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
                                snippet = (lm.content || "").substring(0, 60);
                            }
                            out.lastMessage    = snippet;
                            out.lastMsgIsMine  = isMine;
                            out.lastSenderName = lm.dName || "";
                            out.lastTime       = lm.ts || "";
                        }
                        if (out.lastTime && out.lastTime !== "") {
                            var ts = parseInt(out.lastTime);
                            if (!isNaN(ts)) {
                                // Keep the raw numeric timestamp around under its own
                                // key so we can still sort threads by recency below,
                                // even after lastTime itself gets overwritten with a
                                // human-readable string ("11:37 PM") on the next line.
                                out.sortTs = ts;
                                out.lastTime = chatsNav.formatTime(ts);
                            }
                        }
                        outArr.push(out);
                    }
                    // The friends-list API returns threads in a fixed server-side
                    // order, not by recency. Without re-sorting here, every
                    // logout/login (or cold app restart) reset each thread back to
                    // that original position instead of keeping the most recently
                    // active conversation at the top, which is what onNewMessage
                    // below maintains while the app stays running.
                    outArr.sort(function(a, b) { return (b.sortTs || 0) - (a.sortTs || 0); });
                    for (var j = 0; j < outArr.length; j++) {
                        var ff = outArr[j];
                        friendModel.append(ff);
                        var furl = ff.avatar || "";
                        var ftid = ff.threadId || ff.uid || "";
                        if (furl.length > 0 && ftid.length > 0)
                            chatsNav.queueAvatarDownload(ftid, furl);
                    }
                    chatsEmpty.visible = (outArr.length === 0);
                }

                onAvatarReady: {
                    for (var i = 0; i < friendModel.size(); i++) {
                        var d = friendModel.value(i);
                        if ((d.threadId || d.uid || "") === threadId) {
                            d.localAvatar = localPath;
                            friendModel.replace(i, d);
                            break;
                        }
                    }
                    if (chatsNav.searchVisible) {
                        for (var j = 0; j < searchModel.size(); j++) {
                            var sd = searchModel.value(j);
                            if ((sd.threadId || sd.uid || "") === threadId) {
                                sd.localAvatar = localPath;
                                searchModel.replace(j, sd);
                                break;
                            }
                        }
                    }
                }

                onLoginSuccess: {
                    if (typeof displayName !== "undefined" && displayName.length > 0)
                        chatsNav.selfName = displayName;
                    if (friendModel.size() === 0) {
                        chatsNav.refreshCooldown = true;
                        chatsRefreshCooldownTimer.restart();
                        zService.fetchFriends();
                        chatsLoading.visible = true;
                    }
                }

                onNewMessage: {
                    var tid = threadId;
                    var snippet = (message.msgType === 2 || message.msgType === "2")
                        ? "[Photo]" : (message.content || "").substring(0, 60);
                    var isMine = (message.isMine === true || message.isMine === "true" || message.isMine === 1);
                    var senderName = message.dName || "";
                    if (!isMine) chatsNav.onUnreadMessage();
                    for (var i = 0; i < friendModel.size(); i++) {
                        var d = friendModel.value(i);
                        if ((d.threadId || d.uid || "") === tid) {
                            d.lastMessage    = snippet;
                            d.lastMsgIsMine  = isMine;
                            d.lastSenderName = senderName;
                            d.hasUnread      = !isMine;
                            // Was missing entirely — the list previously froze on
                            // whatever time was last loaded from disk/server and
                            // never advanced as new messages came in (or went
                            // out), no matter how much later they actually arrived.
                            if (message.ts) d.lastTime = chatsNav.formatTime(message.ts);
                            friendModel.removeAt(i);
                            friendModel.insert(0, d);
                            return;
                        }
                    }
                }

                onClearHistoryDone: {
                    if (!success) return;
                    for (var i = 0; i < friendModel.size(); i++) {
                        var d = friendModel.value(i);
                        if ((d.threadId || d.uid || "") === threadId) {
                            d.lastMessage    = "";
                            d.lastMsgIsMine  = false;
                            d.lastSenderName = "";
                            friendModel.replace(i, d);
                            return;
                        }
                    }
                }
            }
        ]
    }

    property string selfName: ""
    signal onUnreadMessage()

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
        // Pop về root trước nếu đang trong chat khác
        chatsNav.popToRoot();
        var page = chatsDef.createObject();
        if (!page) return;
        // Tìm tên từ model nếu có
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
}
