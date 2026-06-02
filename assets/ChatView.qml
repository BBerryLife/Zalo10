import bb.cascades 1.4
import QtQuick 1.0

Page {
    id: chatViewPage

    property string threadId:    ""
    property string threadName:  ""
    property bool   isGroup:     false
    property string avatarUrl:   ""
    property string selfName:    "Me"   // truyền từ main.qml
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

                // Avatar vuông BBM full-height
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

                // Name + subtitle
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

    onThreadIdChanged: {
        if (chatViewPage.threadId === "" || chatViewPage.initialized) return;
        chatViewPage.initialized = true;
        zService.setActiveThread(chatViewPage.threadId, chatViewPage.isGroup);
        var cached = zService.dbLoadMessages(chatViewPage.threadId);
        if (cached && cached.length > 0) {
            for (var i = 0; i < cached.length; i++)
                msgModel.append(cached[i]);
            msgList.scrollToPosition(ScrollPosition.End, ScrollAnimation.None);
        }
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

            listItemComponents: [
                ListItemComponent {
                    type: ""
                    Container {
                        id: rowRoot
                        horizontalAlignment: HorizontalAlignment.Fill

                        // mine = TIN CỦA TÔI → bên TRÁI (theo yêu cầu)
                        // người khác          → bên PHẢI
                        property bool mine: (ListItemData.isMine === true
                                             || ListItemData.isMine === "true")

                        // Gộp tin: nếu cùng sender với tin liền trước
                        property bool grouped: ListItemData.grouped === true

                        topPadding:    rowRoot.grouped ? 1 : 4
                        bottomPadding: 1
                        // mine (trái): rightPadding lớn để đẩy bubble vào bên trái
                        // người khác (phải): leftPadding lớn để đẩy sang phải
                        leftPadding:   rowRoot.mine ? 6  : 80
                        rightPadding:  rowRoot.mine ? 80 : 6

                        // ── Bubble BBM: dùng image làm nền ──────────
                        Container {
                            horizontalAlignment: HorizontalAlignment.Fill
                            layout: DockLayout {}

                            // Bubble image — full/top/middle/bottom theo nhóm
                            // mine=trái dùng outgoing, người khác=phải dùng incoming
                            ImageView {
                                objectName: "background"
                                horizontalAlignment: HorizontalAlignment.Fill
                                verticalAlignment:   VerticalAlignment.Fill
                                imageSource: {
                                    var dir = rowRoot.mine ? "outgoing" : "incoming";
                                    var variant = ListItemData.bubblePos || "full";
                                    return "asset:///images/Bubble/" + dir + "/" + variant + ".png.amd";
                                }
                            }

                            // Nội dung bên trong bubble
                            Container {
                                horizontalAlignment: HorizontalAlignment.Fill
                                topPadding:    rowRoot.grouped ? 6 : 18
                                bottomPadding: 18
                                leftPadding:   18
                                rightPadding:  18

                                // Header: tên bold + timestamp (chỉ ở tin đầu nhóm)
                                Container {
                                    visible: !rowRoot.grouped
                                    layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                                    horizontalAlignment: HorizontalAlignment.Fill
                                    bottomMargin: 4

                                    // Tên người gửi — BOLD như BBM (yêu cầu #2)
                                    Label {
                                        layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                                        // mine: hiển thị selfName; người khác: dName
                                        text: {
                                            if (rowRoot.mine) {
                                                // Lấy selfName từ ListItemData (được inject)
                                                return ListItemData.selfName || "Me";
                                            }
                                            return ListItemData.dName || "Unknown";
                                        }
                                        textStyle {
                                            fontSize:   FontSize.Small
                                            fontWeight: FontWeight.Bold
                                            color: rowRoot.mine
                                                ? Color.create("#555555")
                                                : Color.create("#0073BC")
                                        }
                                        topMargin: 0; bottomMargin: 0
                                    }

                                    // Timestamp (cập nhật lên tin mới nhất trong nhóm)
                                    Label {
                                        text: {
                                            var ts = ListItemData.latestTs || ListItemData.ts;
                                            if (!ts) return "";
                                            var n = ts * 1;
                                            if (n > 0 && n < 1e12) n *= 1000;
                                            var d  = new Date(n);
                                            var dow = ["Sun","Mon","Tue","Wed","Thu","Fri","Sat"][d.getDay()];
                                            var h  = d.getHours();
                                            var m2 = d.getMinutes();
                                            var ampm = h >= 12 ? "PM" : "AM";
                                            var h12 = h % 12; if (h12 === 0) h12 = 12;
                                            return dow + " " + h12 + ":" + (m2 < 10 ? "0" : "") + m2 + " " + ampm;
                                        }
                                        horizontalAlignment: HorizontalAlignment.Right
                                        textStyle {
                                            fontSize: FontSize.XSmall
                                            color:    Color.create("#777777")
                                        }
                                        topMargin: 0; bottomMargin: 0
                                    }
                                }

                                // Tin nhắn
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
                            }
                        } // end bubble DockLayout
                    } // end rowRoot
                }
            ]
        } // end ListView

        // 1px separator
        Container { horizontalAlignment: HorizontalAlignment.Fill; preferredHeight: 1; background: Color.Black }

        // ── Input bar ─────────────────────────────────────────────
        Container {
            horizontalAlignment: HorizontalAlignment.Fill
            background: Color.create("#F8F8F8")
            topPadding:    ui.du(1.2)
            bottomPadding: ui.du(1.2)
            leftPadding:   ui.du(1.2)
            rightPadding:  ui.du(1.2)
            layout: StackLayout { orientation: LayoutOrientation.LeftToRight }

            // Attach — to hơn (yêu cầu #3)
            ImageButton {
                verticalAlignment: VerticalAlignment.Center
                preferredWidth: ui.du(7); preferredHeight: ui.du(7)
                rightMargin: ui.du(1)
                defaultImageSource: "asset:///images/ic_attach.png"
                pressedImageSource: "asset:///images/ic_attach.png"
                onClicked: { /* TODO */ }
            }

            // TextField — cao hơn (yêu cầu #3)
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

            // Emoticon — to hơn (yêu cầu #3)
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

    // Send button trên NavigationPane ActionBar
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

    // ─── Helper: tính bubblePos và grouped cho toàn bộ model ─────
    // Gọi lại mỗi khi thêm tin để cập nhật nhóm và timestamp
    function rebuildGroups() {
        var size = msgModel.size();
        if (size === 0) return;
        for (var i = 0; i < size; i++) {
            var cur  = msgModel.value(i);
            var prev = (i > 0) ? msgModel.value(i - 1) : null;
            var next = (i < size - 1) ? msgModel.value(i + 1) : null;

            var samePrev = prev && (prev.isMine === cur.isMine
                           || (String(prev.isMine) === String(cur.isMine)))
                           && (prev.senderId === cur.senderId || (!prev.senderId && !cur.senderId));
            var sameNext = next && (next.isMine === cur.isMine
                           || (String(next.isMine) === String(cur.isMine)))
                           && (next.senderId === cur.senderId || (!next.senderId && !cur.senderId));

            var pos;
            if (samePrev && sameNext)      pos = "middle";
            else if (samePrev && !sameNext) pos = "bottom";
            else if (!samePrev && sameNext) pos = "top";
            else                            pos = "full";

            cur.bubblePos = pos;
            cur.grouped   = samePrev; // ẩn header (tên+time) nếu grouped
            cur.selfName  = chatViewPage.selfName;

            // Timestamp hiển thị = latestTs của nhóm
            // Tìm tin cuối cùng của nhóm liên tiếp này
            if (!sameNext) {
                // Đây là tin cuối nhóm — gán latestTs = ts của chính nó
                cur.latestTs = cur.ts;
                // Backfill latestTs cho tất cả tin trước trong cùng nhóm
                var k = i - 1;
                while (k >= 0) {
                    var prev2 = msgModel.value(k);
                    var sameAsCur = (prev2.isMine === cur.isMine
                                    || String(prev2.isMine) === String(cur.isMine))
                                    && (prev2.senderId === cur.senderId);
                    if (!sameAsCur) break;
                    prev2.latestTs = cur.ts;
                    msgModel.replace(k, prev2);
                    k--;
                }
            }
            msgModel.replace(i, cur);
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
                var sig_tid = threadId;
                if (sig_tid !== chatViewPage.threadId) return;
                var existing = {};
                for (var i = 0; i < msgModel.size(); i++)
                    existing[msgModel.value(i).msgId] = true;
                var added = false;
                for (var j = 0; j < messages.length; j++) {
                    if (!existing[messages[j].msgId]) {
                        msgModel.append(messages[j]);
                        added = true;
                    }
                }
                if (added) {
                    chatViewPage.rebuildGroups();
                    msgList.scrollToPosition(ScrollPosition.End, ScrollAnimation.Smooth);
                }
            }

            onMessageSent: {
                var sig_tid = threadId;
                if (sig_tid !== chatViewPage.threadId) return;
                sendAction.enabled = (inputField.text.trim().length > 0);
                if (success && chatViewPage.pendingMsg !== "") {
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
                    zService.dbSaveMessage(m, chatViewPage.threadId);
                    chatViewPage.rebuildGroups();
                    msgList.scrollToPosition(ScrollPosition.End, ScrollAnimation.Smooth);
                    chatViewPage.pendingMsg = "";
                }
            }

            onNewMessage: {
                var sig_tid = threadId;
                if (sig_tid !== chatViewPage.threadId) return;
                for (var i = 0; i < msgModel.size(); i++) {
                    if (msgModel.value(i).msgId === message.msgId) return;
                }
                var msg = message;
                msg.selfName = chatViewPage.selfName;
                msgModel.append(msg);
                chatViewPage.rebuildGroups();
                msgList.scrollToPosition(ScrollPosition.End, ScrollAnimation.Smooth);
            }
        }
    ]
}
