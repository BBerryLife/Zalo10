import bb.cascades 1.4
import QtQuick 1.0

Page {
    id: chatViewPage

    property string threadId:    ""
    property string threadName:  ""
    property bool   isGroup:     false
    property string avatarUrl:   ""
    property string pendingMsg:  ""
    property bool   initialized: false

    // ─── TITLE BAR ───────────────────────────────────────────────────────────
    titleBar: TitleBar {
        scrollBehavior: TitleBarScrollBehavior.Sticky
        kind: TitleBarKind.FreeForm
        kindProperties: FreeFormTitleBarKindProperties {
            content: Container {
                background: Color.create("#2575fc")
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment:   VerticalAlignment.Fill
                layout: StackLayout { orientation: LayoutOrientation.LeftToRight }

                // Avatar vuông full-height
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

                // Tên + subtitle
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

                // Voice call — kích thước cố định tránh giãn trên Q10/Q5/Classic
                ImageButton {
                    verticalAlignment: VerticalAlignment.Center
                    preferredWidth: ui.du(7); preferredHeight: ui.du(7)
                    defaultImageSource: "asset:///images/ic_voice_call.png"
                    pressedImageSource: "asset:///images/ic_voice_call.png"
                    rightMargin: ui.du(0.3)
                    onClicked: { callSheet.open(); }
                }
                // Video call
                ImageButton {
                    verticalAlignment: VerticalAlignment.Center
                    preferredWidth: ui.du(7); preferredHeight: ui.du(7)
                    defaultImageSource: "asset:///images/ca_video_chat_active.png"
                    pressedImageSource: "asset:///images/ca_video_chat_active.png"
                    rightMargin: ui.du(0.5)
                    onClicked: { videoCallSheet.open(); }
                }
            }
        }
    }

    // ─── LOAD MESSAGES ───────────────────────────────────────────────────────
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

    // ─── CONTENT ─────────────────────────────────────────────────────────────
    content: Container {
        layout: StackLayout { orientation: LayoutOrientation.TopToBottom }
        horizontalAlignment: HorizontalAlignment.Fill
        verticalAlignment:   VerticalAlignment.Fill
        background: Color.create("#e5e5e5")

        // ── Message list ──────────────────────────────────────────────────────
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
                        property bool mine: (ListItemData.isMine === true || ListItemData.isMine === "true")

                        topPadding: 3; bottomPadding: 3
                        // Outgoing: đẩy sang phải bằng leftPadding lớn
                        // Incoming: đẩy sang trái bằng rightPadding lớn
                        leftPadding:  rowRoot.mine ? 80 : 10
                        rightPadding: rowRoot.mine ? 10 : 80

                        // Tên group (chỉ tin người khác)
                        Label {
                            visible: !rowRoot.mine && (ListItemData.isGroup || false)
                                     && (ListItemData.dName || "").length > 0
                            text: ListItemData.dName || ""
                            horizontalAlignment: HorizontalAlignment.Left
                            topMargin: 2; bottomMargin: 2
                            textStyle { fontSize: FontSize.XSmall; fontWeight: FontWeight.W600; color: Color.create("#0073BC") }
                        }

                        // ── BUBBLE (QML thuần — không dùng image) ────────────
                        // outgoing: nền trắng  #FFFFFF  text đen  — như BBM
                        // incoming: nền trắng  #FFFFFF  text đen  (BBM cũng dùng trắng cho cả 2)
                        // Điểm phân biệt là vị trí căn trái/phải theo leftPadding/rightPadding ở rowRoot
                        Container {
                            horizontalAlignment: HorizontalAlignment.Fill
                            background: Color.White
                            topPadding: 14; bottomPadding: 14
                            leftPadding: 18; rightPadding: 18

                            // Header: dName + timestamp trên cùng 1 row
                            Container {
                                layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                                horizontalAlignment: HorizontalAlignment.Fill
                                bottomMargin: 6

                                Container {
                                    layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                                    Label {
                                        visible: !rowRoot.mine && (ListItemData.dName || "").length > 0
                                        text: ListItemData.dName || ""
                                        textStyle { fontSize: FontSize.Small; fontWeight: FontWeight.W500; color: Color.create("#0073BC") }
                                        topMargin: 0; bottomMargin: 0
                                    }
                                }

                                Label {
                                    text: {
                                        var ts = ListItemData.ts;
                                        if (!ts) return "";
                                        var n = ts * 1;
                                        if (n > 0 && n < 1e12) n *= 1000;
                                        var d = new Date(n);
                                        var h = d.getHours(), m2 = d.getMinutes();
                                        return h + ":" + (m2 < 10 ? "0" : "") + m2;
                                    }
                                    horizontalAlignment: HorizontalAlignment.Right
                                    textStyle { fontSize: FontSize.XSmall; color: Color.create("#999999") }
                                    topMargin: 0; bottomMargin: 0
                                }
                            }

                            // Nội dung tin
                            Label {
                                text: {
                                    var c = ListItemData.content;
                                    if (typeof c === "string" && c.length > 0) return c;
                                    if (c && typeof c === "object" && c.content) return c.content;
                                    return "[Image/Sticker]";
                                }
                                textStyle { base: SystemDefaults.TextStyles.BodyText; color: Color.Black }
                                multiline: true
                                topMargin: 0; bottomMargin: 0
                            }
                        }
                        // ── end BUBBLE ────────────────────────────────────────

                    }
                }
            ]
        } // end ListView

        // 1px separator
        Container { horizontalAlignment: HorizontalAlignment.Fill; preferredHeight: 1; background: Color.Black }

        // ── Input bar — BBM ConversationInputControl layout ────────────────
        // [attach] [TextField flex] [emoticon]   Send = ActionBar
        Container {
            horizontalAlignment: HorizontalAlignment.Fill
            background: Color.create("#F8F8F8")
            topPadding: ui.du(1); bottomPadding: ui.du(1)
            leftPadding: ui.du(1); rightPadding: ui.du(1)
            layout: StackLayout { orientation: LayoutOrientation.LeftToRight }

            ImageButton {
                verticalAlignment: VerticalAlignment.Center
                preferredWidth: ui.du(5.5); preferredHeight: ui.du(5.5)
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
                    input {
                        flags: TextInputFlag.SpellCheck | TextInputFlag.WordSubstitution
                        submitKey: SubmitKey.Send
                        onSubmitted: { doSend(); }
                    }
                    onTextChanging: { sendAction.enabled = (inputField.text.trim().length > 0); }
                }
            }

            ImageButton {
                verticalAlignment: VerticalAlignment.Center
                preferredWidth: ui.du(5.5); preferredHeight: ui.du(5.5)
                leftMargin: ui.du(0.5)
                defaultImageSource: "asset:///images/ic_emoticon_enabled.png"
                pressedImageSource: "asset:///images/ic_emoticon_enabled.png"
                onClicked: { /* TODO */ }
            }
        }
    }

    actions: [
        ActionItem {
            id: sendAction
            title: "Send"
            imageSource: "asset:///images/ConversationPaneSend.png"
            ActionBar.placement: ActionBarPlacement.OnBar
            enabled: false
            onTriggered: { doSend(); }
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

    attachedObjects: [
        // ── Voice call sheet ──────────────────────────────────────────────────
        Sheet {
            id: callSheet
            peekEnabled: false
            Page {
                titleBar: TitleBar {
                    title: "Voice Call"
                    dismissAction: ActionItem {
                        title: "End"
                        onTriggered: { callSheet.close(); }
                    }
                }
                content: Container {
                    layout: StackLayout { orientation: LayoutOrientation.TopToBottom }
                    horizontalAlignment: HorizontalAlignment.Fill
                    verticalAlignment:   VerticalAlignment.Fill
                    background: Color.create("#1a1a1a")

                    // Avatar lớn giữa màn hình
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
                                id: callStatus
                                text: "Calling..."
                                horizontalAlignment: HorizontalAlignment.Center
                                textStyle { color: Color.create("#aaaaaa"); fontSize: FontSize.Medium }
                                topMargin: ui.du(1)
                            }
                        }
                    }

                    // Nút End call
                    Container {
                        horizontalAlignment: HorizontalAlignment.Center
                        bottomPadding: ui.du(5)
                        topPadding: ui.du(3)

                        ImageButton {
                            horizontalAlignment: HorizontalAlignment.Center
                            preferredWidth: ui.du(14); preferredHeight: ui.du(14)
                            defaultImageSource: "asset:///images/ic_voice_call.png"
                            pressedImageSource: "asset:///images/ic_voice_call.png"
                            onClicked: { callSheet.close(); }
                        }
                        Label {
                            text: "End"
                            horizontalAlignment: HorizontalAlignment.Center
                            textStyle { color: Color.White; fontSize: FontSize.Small }
                            topMargin: ui.du(1)
                        }
                    }
                }
            }
        },

        // ── Video call sheet ──────────────────────────────────────────────────
        Sheet {
            id: videoCallSheet
            peekEnabled: false
            Page {
                titleBar: TitleBar {
                    title: "Video Call"
                    dismissAction: ActionItem {
                        title: "End"
                        onTriggered: { videoCallSheet.close(); }
                    }
                }
                content: Container {
                    layout: StackLayout { orientation: LayoutOrientation.TopToBottom }
                    horizontalAlignment: HorizontalAlignment.Fill
                    verticalAlignment:   VerticalAlignment.Fill
                    background: Color.create("#0d0d0d")

                    // "Camera feed" placeholder
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

                        // PiP: thumbnail camera mình (góc trên phải)
                        Container {
                            horizontalAlignment: HorizontalAlignment.Right
                            verticalAlignment:   VerticalAlignment.Top
                            rightPadding: ui.du(2); topPadding: ui.du(2)
                            preferredWidth: ui.du(16); preferredHeight: ui.du(22)
                            background: Color.create("#333333")
                            layout: DockLayout {}
                            Label {
                                text: "You"
                                horizontalAlignment: HorizontalAlignment.Center
                                verticalAlignment:   VerticalAlignment.Center
                                textStyle { color: Color.create("#888888"); fontSize: FontSize.Small }
                            }
                        }
                    }

                    // Controls: mute + camera + end
                    Container {
                        horizontalAlignment: HorizontalAlignment.Fill
                        background: Color.create("#1a1a1a")
                        topPadding: ui.du(2); bottomPadding: ui.du(3)
                        layout: StackLayout { orientation: LayoutOrientation.LeftToRight }

                        Container { layoutProperties: StackLayoutProperties { spaceQuota: 1 } }

                        Container {
                            layout: StackLayout { orientation: LayoutOrientation.TopToBottom }
                            horizontalAlignment: HorizontalAlignment.Center
                            ImageButton {
                                horizontalAlignment: HorizontalAlignment.Center
                                preferredWidth: ui.du(10); preferredHeight: ui.du(10)
                                defaultImageSource: "asset:///images/ic_microphone.png"
                                pressedImageSource: "asset:///images/ic_microphone.png"
                                onClicked: { /* TODO: mute */ }
                            }
                            Label { text: "Mute"; horizontalAlignment: HorizontalAlignment.Center; textStyle { color: Color.White; fontSize: FontSize.XSmall } }
                        }

                        Container { layoutProperties: StackLayoutProperties { spaceQuota: 1 } }

                        Container {
                            layout: StackLayout { orientation: LayoutOrientation.TopToBottom }
                            horizontalAlignment: HorizontalAlignment.Center
                            ImageButton {
                                horizontalAlignment: HorizontalAlignment.Center
                                preferredWidth: ui.du(13); preferredHeight: ui.du(13)
                                defaultImageSource: "asset:///images/ca_video_chat_active.png"
                                pressedImageSource: "asset:///images/ca_video_chat_active.png"
                                onClicked: { videoCallSheet.close(); }
                            }
                            Label { text: "End"; horizontalAlignment: HorizontalAlignment.Center; textStyle { color: Color.White; fontSize: FontSize.XSmall } }
                        }

                        Container { layoutProperties: StackLayoutProperties { spaceQuota: 1 } }

                        Container {
                            layout: StackLayout { orientation: LayoutOrientation.TopToBottom }
                            horizontalAlignment: HorizontalAlignment.Center
                            ImageButton {
                                horizontalAlignment: HorizontalAlignment.Center
                                preferredWidth: ui.du(10); preferredHeight: ui.du(10)
                                defaultImageSource: "asset:///images/ic_video_chat.png"
                                pressedImageSource: "asset:///images/ic_video_chat.png"
                                onClicked: { /* TODO: flip camera */ }
                            }
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
                if (added) msgList.scrollToPosition(ScrollPosition.End, ScrollAnimation.Smooth);
            }

            onMessageSent: {
                var sig_tid = threadId;
                if (sig_tid !== chatViewPage.threadId) return;
                sendAction.enabled = (inputField.text.trim().length > 0);
                if (success && chatViewPage.pendingMsg !== "") {
                    var m = {
                        msgId:   "local_" + new Date().getTime(),
                        content: chatViewPage.pendingMsg,
                        isMine:  true,
                        isGroup: chatViewPage.isGroup,
                        dName:   "",
                        ts:      String(new Date().getTime())
                    };
                    msgModel.append(m);
                    zService.dbSaveMessage(m, chatViewPage.threadId);
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
                msgModel.append(message);
                msgList.scrollToPosition(ScrollPosition.End, ScrollAnimation.Smooth);
            }
        }
    ]
}
