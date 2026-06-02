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

                // Avatar vuông BBM — full-height, width = height, không bo góc
                Container {
                    id: avatarBlock
                    verticalAlignment: VerticalAlignment.Fill
                    layout: DockLayout {}
                    preferredWidth:  titleBarLUH.layoutFrame.height > 0
                                     ? titleBarLUH.layoutFrame.height : ui.du(7)
                    minWidth:        titleBarLUH.layoutFrame.height > 0
                                     ? titleBarLUH.layoutFrame.height : ui.du(7)

                    // Ảnh thật
                    ImageView {
                        horizontalAlignment: HorizontalAlignment.Fill
                        verticalAlignment:   VerticalAlignment.Fill
                        scalingMethod: ScalingMethod.AspectFill
                        imageSource: chatViewPage.avatarUrl
                        visible: chatViewPage.avatarUrl.length > 0
                    }

                    // Fallback: chữ cái đầu trên nền xanh đậm
                    Container {
                        horizontalAlignment: HorizontalAlignment.Fill
                        verticalAlignment:   VerticalAlignment.Fill
                        background: Color.create("#1a5fc8")
                        layout: DockLayout {}
                        visible: chatViewPage.avatarUrl.length === 0

                        Label {
                            text: chatViewPage.threadName.length > 0
                                  ? chatViewPage.threadName.charAt(0).toUpperCase()
                                  : "?"
                            horizontalAlignment: HorizontalAlignment.Center
                            verticalAlignment:   VerticalAlignment.Center
                            textStyle {
                                color:      Color.White
                                fontSize:   FontSize.XXLarge
                                fontWeight: FontWeight.Bold
                            }
                        }
                    }

                    attachedObjects: [
                        LayoutUpdateHandler { id: titleBarLUH }
                    ]
                }

                // Name + subtitle
                Container {
                    verticalAlignment: VerticalAlignment.Center
                    leftPadding:  ui.du(1.5)
                    rightPadding: ui.du(1)
                    layoutProperties: StackLayoutProperties { spaceQuota: 1 }

                    // Câu hỏi 2: threadName được bind trực tiếp — khi main.qml
                    // set page.threadName trước khi push, header hiển thị tên ngay lập tức.
                    Label {
                        text: chatViewPage.threadName.length > 0
                              ? chatViewPage.threadName : "..."
                        textStyle {
                            color:      Color.White
                            base:       SystemDefaults.TextStyles.TitleText
                            fontWeight: FontWeight.Bold
                        }
                        topMargin: 0; bottomMargin: 0
                    }
                    Label {
                        text: chatViewPage.isGroup ? "Group" : "Zalo Contact"
                        textStyle {
                            color:    Color.create("#b3d4ff")
                            fontSize: FontSize.XSmall
                        }
                        topMargin: ui.du(0.2); bottomMargin: 0
                    }
                }

                // Voice call button
                ImageButton {
                    verticalAlignment: VerticalAlignment.Center
                    defaultImageSource: "asset:///images/ic_voice_call.png"
                    rightMargin: ui.du(0.3)
                    onClicked: { /* TODO: voice call */ }
                }

                // Video call button
                ImageButton {
                    verticalAlignment: VerticalAlignment.Center
                    defaultImageSource: "asset:///images/ca_video_chat_active.png"
                    rightMargin: ui.du(0.5)
                    onClicked: { /* TODO: video call */ }
                }
            }
        }
    }

    // ─── LOAD MESSAGES ───────────────────────────────────────────
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
        // Background xám theo yêu cầu
        background: Color.create("#e5e5e5")

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

                        property bool mine: (ListItemData.isMine === true
                                             || ListItemData.isMine === "true")

                        topPadding:    2
                        bottomPadding: 2
                        // BBM padding: outgoing left=90 right=10, incoming left=10 right=90
                        leftPadding:   rowRoot.mine ? 90 : 10
                        rightPadding:  rowRoot.mine ? 10 : 90

                        // Sender name — group only, not mine
                        Label {
                            visible: !rowRoot.mine
                                && (ListItemData.isGroup || false)
                                && (ListItemData.dName || "").length > 0
                            text: ListItemData.dName || ""
                            horizontalAlignment: HorizontalAlignment.Left
                            topMargin: 2; bottomMargin: 2
                            textStyle {
                                fontSize:   FontSize.XSmall
                                fontWeight: FontWeight.W600
                                color:      Color.create("#0073BC")
                            }
                        }

                        // Bubble: ImageView nền BBM + nội dung bên trong
                        Container {
                            horizontalAlignment: HorizontalAlignment.Fill
                            layout: DockLayout {}

                            ImageView {
                                horizontalAlignment: HorizontalAlignment.Fill
                                verticalAlignment:   VerticalAlignment.Fill
                                scalingMethod: ScalingMethod.Fill
                                imageSource: rowRoot.mine
                                    ? "asset:///images/Bubble/outgoing/full.png"
                                    : "asset:///images/Bubble/incoming/full.png"
                            }

                            Container {
                                horizontalAlignment: HorizontalAlignment.Fill
                                topPadding:    20
                                bottomPadding: 46
                                leftPadding:   25
                                rightPadding:  27

                                // Header row: contact name + timestamp (BBM BubbleHeader)
                                Container {
                                    layout: StackLayout {
                                        orientation: LayoutOrientation.LeftToRight
                                    }
                                    horizontalAlignment: HorizontalAlignment.Fill
                                    bottomMargin: 4

                                    Container {
                                        layoutProperties: StackLayoutProperties {
                                            spaceQuota: 1
                                        }
                                        Label {
                                            visible: !rowRoot.mine
                                                && (ListItemData.dName || "").length > 0
                                            text: ListItemData.dName || ""
                                            textStyle {
                                                fontSize:   FontSize.Medium
                                                fontWeight: FontWeight.W500
                                                color:      Color.create("#262626")
                                            }
                                            topMargin: 0; bottomMargin: 0
                                        }
                                    }

                                    Label {
                                        text: {
                                            var ts = ListItemData.ts;
                                            if (!ts) return "";
                                            var n = ts * 1;
                                            if (n > 0 && n < 1e12) n *= 1000;
                                            var d  = new Date(n);
                                            var h  = d.getHours();
                                            var m2 = d.getMinutes();
                                            return h + ":" + (m2 < 10 ? "0" : "") + m2;
                                        }
                                        horizontalAlignment: HorizontalAlignment.Right
                                        textStyle {
                                            fontSize: FontSize.XSmall
                                            color:    Color.create("#323232")
                                        }
                                        topMargin: 0; bottomMargin: 0
                                    }
                                }

                                // Message text
                                Label {
                                    text: {
                                        var c = ListItemData.content;
                                        if (typeof c === "string" && c.length > 0) return c;
                                        if (c && typeof c === "object" && c.content)
                                            return c.content;
                                        return "[Image/Sticker]";
                                    }
                                    textStyle {
                                        base:  SystemDefaults.TextStyles.BodyText
                                        color: Color.create("#262626")
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

        // 1px black separator — BBM ConversationInputArea style
        Container {
            horizontalAlignment: HorizontalAlignment.Fill
            preferredHeight: 1
            background: Color.Black
        }

        // Input area — exact BBM ConversationInputControl layout:
        // [attach] [TextField spaceQuota:1] [emoticon]
        // Send button lives on NavigationPane ActionBar
        Container {
            horizontalAlignment: HorizontalAlignment.Fill
            background: Color.create("#F8F8F8")
            topPadding:    ui.du(1)
            bottomPadding: ui.du(1)
            leftPadding:   ui.du(1)
            rightPadding:  10
            layout: StackLayout { orientation: LayoutOrientation.LeftToRight }

            // Attach button (BBM leftButton / quick_actions)
            Container {
                horizontalAlignment: HorizontalAlignment.Center
                verticalAlignment:   VerticalAlignment.Center
                rightMargin: 10

                ImageButton {
                    maxHeight: ui.du(5)
                    maxWidth:  ui.du(5)
                    defaultImageSource: "asset:///images/ic_attach.png"
                    pressedImageSource: "asset:///images/QuickActions/quick_actions_highlighted.png"
                    onClicked: { /* TODO: attach */ }
                }
            }

            // TextField — BBM SmartTextArea, spaceQuota:1
            Container {
                layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                rightPadding: 10
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
                    onTextChanging: {
                        sendAction.enabled = (inputField.text.trim().length > 0);
                    }
                }
            }

            // Emoticon button (BBM vkbOrEmoticonButton)
            ImageButton {
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment:   VerticalAlignment.Center
                rightMargin: 0; leftMargin: 0; bottomMargin: 0; topMargin: 0
                maxHeight: ui.du(5)
                maxWidth:  ui.du(5)
                defaultImageSource: "asset:///images/ic_emoticon_enabled.png"
                pressedImageSource: "asset:///emoticons/emoticon_picker_selected.png"
                onClicked: { /* TODO: emoticon picker */ }
            }

        } // end inputBar

    } // end content

    // Send on NavigationPane ActionBar
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

    // Connections phải nằm trong attachedObjects,
    // và cần import QtQuick 1.0 để Connections được nhận diện đúng.
    attachedObjects: [
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
                if (added)
                    msgList.scrollToPosition(ScrollPosition.End, ScrollAnimation.Smooth);
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
                // Fix: copy signal param ra local var tránh shadow bởi Page property
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
