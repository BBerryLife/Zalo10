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

    attachedObjects: [
        Timer {
            id: chatsRefreshCooldownTimer
            interval: 11000
            repeat: false
            onTriggered: chatsNav.refreshCooldown = false
        }
        // NOTE: used to have a warm-up Timer here that pre-parsed ChatView.qml via
        // createObject()+destroy() to avoid a parse-cost hit on first tap. Removed —
        // it caused a SIGSEGV on device (creating/destroying a parentless Page-derived
        // UIObject isn't safe on this Qt4 Cascades runtime). Not worth the crash risk
        // for a minor perf win.
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

                // Proxy function — ListItemComponent delegates KHÔNG resolve được
                // id của Page/NavigationPane cha (đây chính là nguyên nhân dòng
                // preview tin nhắn cuối biến mất hoàn toàn: "chatsNav.lastMessagePreview"
                // gọi thẳng từ trong delegate ném ReferenceError "Can't find variable:
                // chatsNav" ở mọi lần re-evaluate, binding rỗng nên Label không hiện gì).
                // ListView (friendList) thì gọi được chatsNav bình thường vì nó không
                // nằm trong 1 ListItemComponent — nên "tunnel" qua đây, delegate gọi
                // ListItem.view.lastMessagePreview(...) thay vì chatsNav trực tiếp.
                function lastMessagePreview(lastMessage, lastMsg, lastMsgIsMine, lastSenderName) {
                    return chatsNav.lastMessagePreview(lastMessage, lastMsg, lastMsgIsMine, lastSenderName);
                }

                listItemComponents: [
                    ListItemComponent {
                        type: "item"
                        CustomListItem {
                            id: friendRow
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
                                        text: friendRow.ListItem.view.lastMessagePreview(ListItemData.lastMessage, ListItemData.lastMsg,
                                                  ListItemData.lastMsgIsMine, ListItemData.lastSenderName)
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
                    // Push first, load second — navigation shouldn't wait on message fetch
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
                // target starts null and is assigned once the page exists (see
                // openThread()) — binding directly to chatsNav.activeChatPage causes a
                // "Cannot assign to non-existent property" warning since it's null
                // at construction time
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

                onFriendsReady: {
                    chatsLoading.visible = false;
                    friendModel.clear();
                    // Server's friends list has no last-message field, so pull it from
                    // local history and merge in before building the model
                    var lastMsgs = zService.getThreadLastMessages();
                    // Build a fresh array instead of mutating `friends` in place —
                    // it's a QVariantList from a C++ signal and writes back to it
                    // silently no-op on this runtime
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
                                snippet = chatsNav.msgSnippet(mt, lm.content);
                            }
                            out.lastMessage    = snippet;
                            out.lastMsgIsMine  = isMine;
                            out.lastSenderName = lm.dName || "";
                            out.lastTime       = lm.ts || "";
                        }
                        if (out.lastTime && out.lastTime !== "") {
                            var ts = parseInt(out.lastTime);
                            if (!isNaN(ts)) {
                                // Keep the raw timestamp for sorting since lastTime gets
                                // overwritten with a formatted string below
                                out.sortTs = ts;
                                out.lastTime = chatsNav.formatTime(ts);
                            }
                        }
                        outArr.push(out);
                    }
                    // Server returns threads in a fixed order, not by recency — re-sort
                    // so the most recently active conversation stays on top
                    outArr.sort(function(a, b) { return (b.sortTs || 0) - (a.sortTs || 0); });
                    for (var j = 0; j < outArr.length; j++) {
                        var ff = outArr[j];
                        friendModel.append(ff);
                        var furl = ff.avatar || "";
                        var ftid = ff.threadId || ff.uid || "";
                        if (furl.length > 0 && ftid.length > 0)
                            zService.downloadAvatar(ftid, furl);
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
                        ? "[Photo]" : chatsNav.msgSnippet(message.msgType, message.content);
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
                            // Update the displayed time as new messages come in
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

    // Preview snippet cho tin nhắn cuối trong danh sách chat. msgType===3
    // (video/file, xem ghi chú videoBubble ở ChatView.qml) trước đây luôn
    // hiện cứng "[Video]" kể cả với file tài liệu — giờ chỉ hiện "[Video]"
    // cho đúng .mp4/.mov/.3gp/.mkv, còn lại hiện tên file thật (theo yêu
    // cầu, không dùng "[File]"). fileName đọc thủ công từ content JSON
    // ({"fileName":"...","href":"...","fileSize":...}) bằng string scan
    // đơn giản, không parse JSON đầy đủ — đủ dùng và nhất quán với cách
    // ChatView.qml đọc field này (extractJsonStringField).
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
        if (mt !== 3 && mt !== "3") return (content || "").substring(0, 60);
        var fname = chatsNav.extractFileName(content);
        var ext = fname.substring(fname.lastIndexOf('.') + 1).toLowerCase();
        var isVideo = (ext === "mp4" || ext === "mov" || ext === "3gp" || ext === "mkv");
        if (isVideo) return "[Video]";
        return fname.length > 0 ? fname : "[File]";
    }
    // Nội dung dòng preview thứ 2 trong mỗi ô danh sách chat (tên người gửi
    // + tin nhắn cuối). Trước đây đây là 1 block-body binding trực tiếp
    // trên Label.text ({ var lm = ...; ...; return prefix + lm; }) — pattern
    // này bị cấm trên QtQuick1/Cascades (xem ghi chú ở videoBubble trong
    // ChatView.qml, nơi cùng lỗi này từng gây crash parse thật trên máy).
    // Chuyển thành hàm riêng để nhất quán và loại bỏ rủi ro, dù chỗ này có
    // thể đã "chạy được" nhờ may mắn về context binding cụ thể.
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
