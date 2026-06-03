import bb.cascades 1.4
import QtQuick 1.0

Page {
    id: chatViewPage

    property string threadId:    ""
    property string threadName:  ""
    property bool   isGroup:     false
    property string avatarUrl:   ""
    property string selfName:    "Me"
    property string pendingMsg:  ""
    property bool   initialized: false

    // ─── TITLE BAR ───────────────────────────────────────────────
    titleBar: TitleBar {
        scrollBehavior: TitleBarScrollBehavior.Sticky
        kind: TitleBarKind.FreeForm
        kindProperties: FreeFormTitleBarKindProperties {
            content: Container {
                background: Color.create("#2575fc")
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment:   VerticalAlignment.Fill
                layout: StackLayout { orientation: LayoutOrientation.LeftToRight }

                Container {
                    id: avatarBlock
                    verticalAlignment: VerticalAlignment.Fill
                    layout: DockLayout {}
                    preferredWidth:  titleBarLUH.layoutFrame.height > 0
                                     ? titleBarLUH.layoutFrame.height : ui.du(7)
                    minWidth:        titleBarLUH.layoutFrame.height > 0
                                     ? titleBarLUH.layoutFrame.height : ui.du(7)

                    ImageView {
                        horizontalAlignment: HorizontalAlignment.Fill
                        verticalAlignment:   VerticalAlignment.Fill
                        scalingMethod: ScalingMethod.AspectFill
                        imageSource: chatViewPage.avatarUrl
                        visible: chatViewPage.avatarUrl.length > 0
                    }
                    Container {
                        horizontalAlignment: HorizontalAlignment.Fill
                        verticalAlignment:   VerticalAlignment.Fill
                        background: Color.create("#1a5fc8")
                        layout: DockLayout {}
                        visible: chatViewPage.avatarUrl.length === 0
                        Label {
                            text: chatViewPage.threadName.length > 0
                                  ? chatViewPage.threadName.charAt(0).toUpperCase() : "?"
                            horizontalAlignment: HorizontalAlignment.Center
                            verticalAlignment:   VerticalAlignment.Center
                            textStyle { color: Color.White; fontSize: FontSize.XXLarge; fontWeight: FontWeight.Bold }
                        }
                    }
                    attachedObjects: [ LayoutUpdateHandler { id: titleBarLUH } ]
                }

                Container {
                    verticalAlignment: VerticalAlignment.Center
                    leftPadding: ui.du(1.5)
                    layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                    Label {
                        text: chatViewPage.threadName.length > 0 ? chatViewPage.threadName : "..."
                        textStyle { color: Color.White; base: SystemDefaults.TextStyles.TitleText; fontWeight: FontWeight.Bold }
                        topMargin: 0; bottomMargin: 0
                    }
                    Label {
                        text: chatViewPage.isGroup ? "Group" : "Zalo Contact"
                        textStyle { color: Color.create("#b3d4ff"); fontSize: FontSize.XSmall }
                        topMargin: ui.du(0.2); bottomMargin: 0
                    }
                }

                ImageButton {
                    verticalAlignment: VerticalAlignment.Center
                    preferredWidth: ui.du(7); preferredHeight: ui.du(7)
                    defaultImageSource: "asset:///images/ic_voice_call.png"
                    pressedImageSource: "asset:///images/ic_voice_call.png"
                    rightMargin: ui.du(0.3)
                    onClicked: { callSheet.open() }
                }
                ImageButton {
                    verticalAlignment: VerticalAlignment.Center
                    preferredWidth: ui.du(7); preferredHeight: ui.du(7)
                    defaultImageSource: "asset:///images/ca_video_chat_active.png"
                    pressedImageSource: "asset:///images/ca_video_chat_active.png"
                    rightMargin: ui.du(0.5)
                    onClicked: { videoCallSheet.open() }
                }
            }
        }
    }

    // Chờ cả threadId lẫn selfName được set trước khi load
    // main.qml assign threadId trước selfName nên cần cả 2 handler
    onThreadIdChanged: { chatViewPage.tryInit() }
    onSelfNameChanged: { chatViewPage.tryInit() }

    function tryInit() {
        // Chỉ init khi cả threadId và selfName đã có giá trị thực
        if (chatViewPage.threadId === "") return;
        if (chatViewPage.selfName === "" || chatViewPage.selfName === "Me") {
            // selfName chưa set từ server, chờ thêm — nhưng nếu threadId đã set
            // thì vẫn init với selfName mặc định nếu đã chờ đủ lâu
        }
        chatViewPage.doInit();
    }



    function doInit() {
        if (chatViewPage.threadId === "") return;
        if (chatViewPage.initialized) return;
        chatViewPage.initialized = true;

        // Đảm bảo model sạch trước khi load
        msgModel.clear();

        zService.setActiveThread(chatViewPage.threadId, chatViewPage.isGroup);

        // Load từ DB trước — selfName đã được set đúng lúc này
        var cached = zService.dbLoadMessages(chatViewPage.threadId);
        if (cached && cached.length > 0) {
            for (var i = 0; i < cached.length; i++) {
                var c = cached[i];
                c.selfName = chatViewPage.selfName;
                msgModel.append(c);
            }
            chatViewPage.rebuildGroups();
            msgList.scrollToPosition(ScrollPosition.End, ScrollAnimation.None);
        }

        // Fetch từ server
        zService.fetchMessages(chatViewPage.threadId, chatViewPage.isGroup);
    }

    // ─── CONTENT ─────────────────────────────────────────────────
    content: Container {
        layout: StackLayout { orientation: LayoutOrientation.TopToBottom }
        horizontalAlignment: HorizontalAlignment.Fill
        verticalAlignment:   VerticalAlignment.Fill
        background: Color.create("#d6d6d6")

        // ── Message list ─────────────────────────────────────────
        ListView {
            id: msgList
            horizontalAlignment: HorizontalAlignment.Fill
            layoutProperties: StackLayoutProperties { spaceQuota: 1 }
            dataModel: ArrayDataModel { id: msgModel }

            flickMode: FlickMode.Momentum

            listItemComponents: [
                ListItemComponent {
                    type: ""
                    Container {
                        id: rowRoot
                        horizontalAlignment: HorizontalAlignment.Fill
                        topPadding:    ListItemData.grouped === true ? 0 : 6
                        bottomPadding: 0

                        // Dùng LeftToRight + spaceQuota để force bubble fill đúng width
                        layout: StackLayout { orientation: LayoutOrientation.LeftToRight }

                        property bool mine: (ListItemData.isMine === true
                                             || ListItemData.isMine === "true"
                                             || ListItemData.isMine === 1)
                        property bool grouped: ListItemData.grouped === true

                        // Spacer trái: nhỏ nếu mine(trái), lớn nếu người khác(phải)
                        Container {
                            preferredWidth: rowRoot.mine ? 6 : 60
                            minWidth:       rowRoot.mine ? 6 : 60
                            maxWidth:       rowRoot.mine ? 6 : 60
                        }

                        // ── Bubble chiếm phần còn lại ─────────────────
                        Container {
                            layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                            background: Color.White
                            topPadding:    rowRoot.grouped ? 6 : 10
                            bottomPadding: 10
                            leftPadding:   14
                            rightPadding:  14

                            // Header: tên + timestamp — chỉ hiện ở tin ĐẦU nhóm
                            Container {
                                visible: !rowRoot.grouped
                                layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                                horizontalAlignment: HorizontalAlignment.Fill
                                bottomMargin: 2

                                Label {
                                    layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                                    text: rowRoot.mine
                                          ? (ListItemData.selfName || "Me")
                                          : (ListItemData.dName    || "Unknown")
                                    textStyle {
                                        fontSize:   FontSize.Small
                                        fontWeight: FontWeight.Bold
                                        color: rowRoot.mine
                                            ? Color.create("#555555")
                                            : Color.create("#0073BC")
                                    }
                                    topMargin: 0; bottomMargin: 0
                                }

                                Label {
                                    text: {
                                        var ts = ListItemData.latestTs || ListItemData.ts;
                                        if (!ts) return "";
                                        var n = ts * 1;
                                        if (n > 0 && n < 1e12) n *= 1000;
                                        var d   = new Date(n);
                                        var dow = ["Sun","Mon","Tue","Wed","Thu","Fri","Sat"][d.getDay()];
                                        var h   = d.getHours();
                                        var m2  = d.getMinutes();
                                        var ap  = h >= 12 ? "PM" : "AM";
                                        var h12 = h % 12; if (h12 === 0) h12 = 12;
                                        return dow + " " + h12 + ":" + (m2 < 10 ? "0" : "") + m2 + " " + ap;
                                    }
                                    horizontalAlignment: HorizontalAlignment.Right
                                    textStyle { fontSize: FontSize.XSmall; color: Color.create("#777777") }
                                    topMargin: 0; bottomMargin: 0
                                }
                            }

                            // Nội dung tin nhắn
                            Label {
                                text: {
                                    var c = ListItemData.content;
                                    if (typeof c === "string" && c.length > 0) return c;
                                    if (c && typeof c === "object" && c.content) return c.content;
                                    return "[Image/Sticker]";
                                }
                                textStyle {
                                    base:  SystemDefaults.TextStyles.BodyText
                                    color: Color.create("#111111")
                                }
                                multiline: true
                                topMargin: 0; bottomMargin: 0
                            }
                        } // end bubble

                        // Spacer phải: lớn nếu mine(trái), nhỏ nếu người khác(phải)
                        Container {
                            preferredWidth: rowRoot.mine ? 60 : 6
                            minWidth:       rowRoot.mine ? 60 : 6
                            maxWidth:       rowRoot.mine ? 60 : 6
                        }
                    } // end rowRoot
                }
            ]
        } // end ListView

        // 1px separator
        Container { horizontalAlignment: HorizontalAlignment.Fill; preferredHeight: 1; background: Color.Black }

        // ── Input bar ────────────────────────────────────────────
        Container {
            horizontalAlignment: HorizontalAlignment.Fill
            background: Color.create("#F8F8F8")
            topPadding:    ui.du(1.2)
            bottomPadding: ui.du(1.2)
            leftPadding:   ui.du(1.2)
            rightPadding:  ui.du(1.2)
            layout: StackLayout { orientation: LayoutOrientation.LeftToRight }

            ImageButton {
                verticalAlignment: VerticalAlignment.Center
                preferredWidth: ui.du(7); preferredHeight: ui.du(7)
                rightMargin: ui.du(1)
                defaultImageSource: "asset:///images/ic_attach.png"
                pressedImageSource: "asset:///images/ic_attach.png"
                onClicked: { /* TODO */ }
            }

            Container {
                layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                verticalAlignment: VerticalAlignment.Center
                TextField {
                    id: inputField
                    hintText: "Enter a message"
                    inputMode: TextFieldInputMode.Chat
                    minHeight: ui.du(6)
                    input {
                        flags: TextInputFlag.SpellCheck | TextInputFlag.WordSubstitution
                        submitKey: SubmitKey.Send
                        onSubmitted: { doSend() }
                    }
                    onTextChanging: {
                        sendAction.enabled = (inputField.text.trim().length > 0)
                    }
                }
            }

            ImageButton {
                verticalAlignment: VerticalAlignment.Center
                preferredWidth: ui.du(7); preferredHeight: ui.du(7)
                leftMargin: ui.du(1)
                defaultImageSource: "asset:///images/ic_emoticon_enabled.png"
                pressedImageSource: "asset:///images/ic_emoticon_enabled.png"
                onClicked: { /* TODO */ }
            }
        }

    } // end content

    actions: [
        ActionItem {
            id: sendAction
            title: "Send"
            imageSource: "asset:///images/ConversationPaneSend.png"
            ActionBar.placement: ActionBarPlacement.OnBar
            enabled: false
            onTriggered: { doSend() }
        }
    ]

    function doSend() {
        var txt = inputField.text.trim();
        if (txt.length === 0) return;
        chatViewPage.pendingMsg = txt;
        sendAction.enabled = false;
        inputField.text = "";
        zService.sendMessage(chatViewPage.threadId, txt, chatViewPage.isGroup);
    }

    // ─── isMine normalizer ───────────────────────────────────────
    function normMine(v) {
        if (v === true  || v === 1)  return true;
        if (v === false || v === 0)  return false;
        if (typeof v === "string")   return (v === "true" || v === "1");
        return false;
    }

    // ─── Xây lại bubblePos + grouped + latestTs cho toàn bộ model
    function rebuildGroups() {
        var size = msgModel.size();
        if (size === 0) return;

        for (var i = 0; i < size; i++) {
            var cur  = msgModel.value(i);
            var prev = (i > 0)          ? msgModel.value(i - 1) : null;
            var next = (i < size - 1)   ? msgModel.value(i + 1) : null;

            var curMine  = chatViewPage.normMine(cur.isMine);
            var prevMine = prev ? chatViewPage.normMine(prev.isMine) : !curMine;
            var nextMine = next ? chatViewPage.normMine(next.isMine) : !curMine;

            var samePrev = (prev !== null) && (prevMine === curMine);
            var sameNext = (next !== null) && (nextMine === curMine);

            var pos;
            if      ( samePrev &&  sameNext) pos = "middle";
            else if ( samePrev && !sameNext) pos = "bottom";
            else if (!samePrev &&  sameNext) pos = "top";
            else                             pos = "full";

            cur.bubblePos = pos;
            cur.grouped   = samePrev;
            cur.selfName  = chatViewPage.selfName;
            // Đảm bảo isMine không bị mất sau replace — normalize về bool
            cur.isMine    = curMine;

            // Timestamp hiển thị ở tin CUỐI nhóm — cập nhật ngược lên các tin trước
            if (!sameNext) {
                cur.latestTs = cur.ts;
                var k = i - 1;
                while (k >= 0) {
                    var prev2 = msgModel.value(k);
                    if (chatViewPage.normMine(prev2.isMine) !== curMine) break;
                    prev2.latestTs = cur.ts;
                    msgModel.replace(k, prev2);
                    k--;
                }
            }
            msgModel.replace(i, cur);
        }
    }

    // ─── Helper: lấy set msgId hiện có trong model (chỉ id thật, bỏ local_)
    function buildExistingIds() {
        var ids = {};
        for (var i = 0; i < msgModel.size(); i++) {
            var mid = msgModel.value(i).msgId;
            if (mid && mid.indexOf("local_") !== 0)
                ids[mid] = true;
        }
        return ids;
    }

    // ─── Helper: xóa local placeholder có content khớp (tìm từ cuối lên)
    function removeLocalPlaceholder(content) {
        for (var k = msgModel.size() - 1; k >= 0; k--) {
            var item = msgModel.value(k);
            if (item.msgId && item.msgId.indexOf("local_") === 0
                    && item.content === content) {
                msgModel.removeAt(k);
                return;
            }
        }
    }

    attachedObjects: [
        Sheet {
            id: callSheet
            peekEnabled: false
            Page {
                titleBar: TitleBar {
                    title: "Voice Call"
                    dismissAction: ActionItem { title: "End"; onTriggered: { callSheet.close() } }
                }
                content: Container {
                    layout: StackLayout { orientation: LayoutOrientation.TopToBottom }
                    horizontalAlignment: HorizontalAlignment.Fill
                    verticalAlignment:   VerticalAlignment.Fill
                    background: Color.create("#1a1a1a")
                    Container {
                        layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                        layout: DockLayout {}
                        horizontalAlignment: HorizontalAlignment.Fill
                        Container {
                            horizontalAlignment: HorizontalAlignment.Center
                            verticalAlignment:   VerticalAlignment.Center
                            layout: StackLayout { orientation: LayoutOrientation.TopToBottom }
                            ImageView {
                                horizontalAlignment: HorizontalAlignment.Center
                                preferredWidth: ui.du(22); preferredHeight: ui.du(22)
                                scalingMethod: ScalingMethod.AspectFill
                                imageSource: chatViewPage.avatarUrl.length > 0
                                             ? chatViewPage.avatarUrl : "asset:///images/ic_contact.png"
                            }
                            Label {
                                text: chatViewPage.threadName
                                horizontalAlignment: HorizontalAlignment.Center
                                textStyle { color: Color.White; base: SystemDefaults.TextStyles.TitleText; fontWeight: FontWeight.Bold }
                                topMargin: ui.du(2)
                            }
                            Label {
                                text: "Calling..."
                                horizontalAlignment: HorizontalAlignment.Center
                                textStyle { color: Color.create("#aaaaaa"); fontSize: FontSize.Medium }
                                topMargin: ui.du(1)
                            }
                        }
                    }
                    Container {
                        horizontalAlignment: HorizontalAlignment.Center
                        bottomPadding: ui.du(5); topPadding: ui.du(3)
                        ImageButton {
                            horizontalAlignment: HorizontalAlignment.Center
                            preferredWidth: ui.du(14); preferredHeight: ui.du(14)
                            defaultImageSource: "asset:///images/ic_voice_call.png"
                            pressedImageSource: "asset:///images/ic_voice_call.png"
                            onClicked: { callSheet.close() }
                        }
                        Label { text: "End"; horizontalAlignment: HorizontalAlignment.Center; textStyle { color: Color.White; fontSize: FontSize.Small } }
                    }
                }
            }
        },

        Sheet {
            id: videoCallSheet
            peekEnabled: false
            Page {
                titleBar: TitleBar {
                    title: "Video Call"
                    dismissAction: ActionItem { title: "End"; onTriggered: { videoCallSheet.close() } }
                }
                content: Container {
                    layout: StackLayout { orientation: LayoutOrientation.TopToBottom }
                    horizontalAlignment: HorizontalAlignment.Fill
                    verticalAlignment:   VerticalAlignment.Fill
                    background: Color.create("#0d0d0d")
                    Container {
                        layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                        layout: DockLayout {}
                        horizontalAlignment: HorizontalAlignment.Fill
                        background: Color.create("#111111")
                        Container {
                            horizontalAlignment: HorizontalAlignment.Center
                            verticalAlignment:   VerticalAlignment.Center
                            layout: StackLayout { orientation: LayoutOrientation.TopToBottom }
                            ImageView {
                                horizontalAlignment: HorizontalAlignment.Center
                                preferredWidth: ui.du(22); preferredHeight: ui.du(22)
                                scalingMethod: ScalingMethod.AspectFill
                                imageSource: chatViewPage.avatarUrl.length > 0
                                             ? chatViewPage.avatarUrl : "asset:///images/ic_contact.png"
                            }
                            Label {
                                text: chatViewPage.threadName
                                horizontalAlignment: HorizontalAlignment.Center
                                textStyle { color: Color.White; base: SystemDefaults.TextStyles.TitleText; fontWeight: FontWeight.Bold }
                                topMargin: ui.du(2)
                            }
                            Label {
                                text: "Connecting video..."
                                horizontalAlignment: HorizontalAlignment.Center
                                textStyle { color: Color.create("#aaaaaa"); fontSize: FontSize.Medium }
                                topMargin: ui.du(1)
                            }
                        }
                        Container {
                            horizontalAlignment: HorizontalAlignment.Right
                            verticalAlignment:   VerticalAlignment.Top
                            rightPadding: ui.du(2); topPadding: ui.du(2)
                            preferredWidth: ui.du(16); preferredHeight: ui.du(22)
                            background: Color.create("#333333")
                            layout: DockLayout {}
                            Label { text: "You"; horizontalAlignment: HorizontalAlignment.Center; verticalAlignment: VerticalAlignment.Center; textStyle { color: Color.create("#888888"); fontSize: FontSize.Small } }
                        }
                    }
                    Container {
                        horizontalAlignment: HorizontalAlignment.Fill
                        background: Color.create("#1a1a1a")
                        topPadding: ui.du(2); bottomPadding: ui.du(3)
                        layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                        Container { layoutProperties: StackLayoutProperties { spaceQuota: 1 } }
                        Container {
                            layout: StackLayout { orientation: LayoutOrientation.TopToBottom }
                            ImageButton { horizontalAlignment: HorizontalAlignment.Center; preferredWidth: ui.du(10); preferredHeight: ui.du(10); defaultImageSource: "asset:///images/ic_microphone.png"; pressedImageSource: "asset:///images/ic_microphone.png"; onClicked: {} }
                            Label { text: "Mute"; horizontalAlignment: HorizontalAlignment.Center; textStyle { color: Color.White; fontSize: FontSize.XSmall } }
                        }
                        Container { layoutProperties: StackLayoutProperties { spaceQuota: 1 } }
                        Container {
                            layout: StackLayout { orientation: LayoutOrientation.TopToBottom }
                            ImageButton { horizontalAlignment: HorizontalAlignment.Center; preferredWidth: ui.du(13); preferredHeight: ui.du(13); defaultImageSource: "asset:///images/ca_video_chat_active.png"; pressedImageSource: "asset:///images/ca_video_chat_active.png"; onClicked: { videoCallSheet.close() } }
                            Label { text: "End"; horizontalAlignment: HorizontalAlignment.Center; textStyle { color: Color.White; fontSize: FontSize.XSmall } }
                        }
                        Container { layoutProperties: StackLayoutProperties { spaceQuota: 1 } }
                        Container {
                            layout: StackLayout { orientation: LayoutOrientation.TopToBottom }
                            ImageButton { horizontalAlignment: HorizontalAlignment.Center; preferredWidth: ui.du(10); preferredHeight: ui.du(10); defaultImageSource: "asset:///images/ic_video_chat.png"; pressedImageSource: "asset:///images/ic_video_chat.png"; onClicked: {} }
                            Label { text: "Flip"; horizontalAlignment: HorizontalAlignment.Center; textStyle { color: Color.White; fontSize: FontSize.XSmall } }
                        }
                        Container { layoutProperties: StackLayoutProperties { spaceQuota: 1 } }
                    }
                }
            }
        },

        Connections {
            target: zService

            onMessagesReady: {
                // Bỏ qua nếu không phải thread đang mở
                if (threadId !== chatViewPage.threadId) return;

                // Tập hợp tất cả msgId thật đã có trong model
                var existing = chatViewPage.buildExistingIds();

                var added = false;
                for (var j = 0; j < messages.length; j++) {
                    var msg = messages[j];
                    // Bỏ qua tin đã có
                    if (existing[msg.msgId]) continue;

                    // Nếu là tin của mình → xóa local placeholder trùng content
                    if (chatViewPage.normMine(msg.isMine)) {
                        chatViewPage.removeLocalPlaceholder(msg.content);
                    }

                    var nm = msg;
                    nm.selfName = chatViewPage.selfName;
                    msgModel.append(nm);
                    added = true;
                }

                if (added) {
                    chatViewPage.rebuildGroups();
                    msgList.scrollToPosition(ScrollPosition.End, ScrollAnimation.Smooth);
                }
            }

            onMessageSent: {
                if (threadId !== chatViewPage.threadId) return;
                sendAction.enabled = (inputField.text.trim().length > 0);

                if (success && chatViewPage.pendingMsg !== "") {
                    // Thêm local placeholder — KHÔNG lưu DB
                    // Sẽ bị xóa khi tin thật từ server về qua onMessagesReady/onNewMessage
                    var m = {
                        msgId:    "local_" + new Date().getTime(),
                        content:  chatViewPage.pendingMsg,
                        isMine:   true,
                        isGroup:  chatViewPage.isGroup,
                        senderId: "self",
                        dName:    chatViewPage.selfName,
                        ts:       String(new Date().getTime()),
                        selfName: chatViewPage.selfName
                    };
                    msgModel.append(m);
                    chatViewPage.rebuildGroups();
                    msgList.scrollToPosition(ScrollPosition.End, ScrollAnimation.Smooth);
                    chatViewPage.pendingMsg = "";
                }
            }

            onNewMessage: {
                if (threadId !== chatViewPage.threadId) return;

                // Bỏ qua nếu msgId đã có
                for (var i = 0; i < msgModel.size(); i++) {
                    if (msgModel.value(i).msgId === message.msgId) return;
                }

                var msg = message;
                msg.selfName = chatViewPage.selfName;

                // Nếu là tin của mình → xóa local placeholder trùng content
                if (chatViewPage.normMine(msg.isMine)) {
                    chatViewPage.removeLocalPlaceholder(msg.content);
                }

                msgModel.append(msg);
                chatViewPage.rebuildGroups();
                msgList.scrollToPosition(ScrollPosition.End, ScrollAnimation.Smooth);
            }
        }
    ]
}
