import bb.cascades 1.4
import QtQuick 1.0

Page {
    id: chatViewPage

    property string threadId:    ""
    property string threadName:  ""
    property bool   isGroup:     false
    property string pendingMsg:  ""
    property bool   initialized: false

    titleBar: TitleBar {
        kind: TitleBarKind.FreeForm
        kindProperties: FreeFormTitleBarKindProperties {
            content: Container {
                background: Color.create("#2575fc")
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment:   VerticalAlignment.Fill
                layout: DockLayout {}
                leftPadding: ui.du(2)
                Label {
                    text: chatViewPage.threadName || "Chat"
                    textStyle {
                        color: Color.White
                        base: SystemDefaults.TextStyles.TitleText
                        fontWeight: FontWeight.Bold
                    }
                    verticalAlignment:   VerticalAlignment.Center
                    horizontalAlignment: HorizontalAlignment.Left
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

    actions: [
        ActionItem {
            id: sendAction
            title: "Send"
            imageSource: "asset:///images/ConversationPaneSend.png"
            ActionBar.placement: ActionBarPlacement.OnBar
            enabled: false
            onTriggered: {
                var txt = inputField.text.trim();
                if (txt.length === 0) return;
                chatViewPage.pendingMsg = txt;
                sendAction.enabled = false;
                inputField.text = "";
                zService.sendMessage(chatViewPage.threadId, txt, chatViewPage.isGroup);
            }
        }
    ]

    content: Container {
        layout: StackLayout { orientation: LayoutOrientation.TopToBottom }
        horizontalAlignment: HorizontalAlignment.Fill
        verticalAlignment:   VerticalAlignment.Fill
        background: Color.White

        // Vùng tin nhắn — chiếm hết không gian còn lại
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
                        topPadding:    ui.du(0.8)
                        bottomPadding: ui.du(0.2)
                        leftPadding:   ui.du(1.5)
                        rightPadding:  ui.du(1.5)

                        property bool mine: (ListItemData.isMine === true || ListItemData.isMine === "true")

                        // Tên người gửi — chỉ group, không phải tin mình
                        Label {
                            visible: !rowRoot.mine
                                && (ListItemData.isGroup || false)
                                && (ListItemData.dName || "").length > 0
                            text:    ListItemData.dName || ""
                            horizontalAlignment: HorizontalAlignment.Left
                            textStyle {
                                base: SystemDefaults.TextStyles.SmallText
                                color: Color.create("#555555")
                                fontWeight: FontWeight.Bold
                            }
                            leftMargin: ui.du(1)
                        }

                        // Bubble căn phải (mine) hoặc trái (người khác)
                        Container {
                            horizontalAlignment: rowRoot.mine
                                ? HorizontalAlignment.Right
                                : HorizontalAlignment.Left
                            maxWidth: ui.du(52)
                            background: rowRoot.mine
                                ? Color.create("#c8e6fa")
                                : Color.create("#f0f0f0")
                            topPadding:    ui.du(1.2)
                            bottomPadding: ui.du(1.2)
                            leftPadding:   ui.du(2)
                            rightPadding:  ui.du(2)

                            Label {
                                text: {
                                    var c = ListItemData.content;
                                    if (typeof c === "string" && c.length > 0) return c;
                                    if (c && typeof c === "object" && c.content) return c.content;
                                    return "[Anh/Sticker]";
                                }
                                textStyle {
                                    base:  SystemDefaults.TextStyles.BodyText
                                    color: Color.Black
                                }
                                multiline: true
                            }
                        }

                        // Giờ gửi
                        Label {
                            text: {
                                var ts = ListItemData.ts;
                                if (!ts) return "";
                                var n = ts * 1;
                                if (n > 0 && n < 1e12) n = n * 1000;
                                var d = new Date(n);
                                var h = d.getHours(), m2 = d.getMinutes();
                                return h + ":" + (m2 < 10 ? "0" : "") + m2;
                            }
                            horizontalAlignment: rowRoot.mine
                                ? HorizontalAlignment.Right
                                : HorizontalAlignment.Left
                            textStyle {
                                base:  SystemDefaults.TextStyles.SmallText
                                color: Color.create("#aaaaaa")
                            }
                            topMargin: ui.du(0.3)
                        }
                    }
                }
            ]
        }

        // Input bar — nằm trong content Container, sát đáy
        Container {
            horizontalAlignment: HorizontalAlignment.Fill
            background: Color.create("#eeeeee")
            topPadding:    ui.du(1)
            bottomPadding: ui.du(1)
            leftPadding:   ui.du(2)
            rightPadding:  ui.du(2)
            layout: StackLayout { orientation: LayoutOrientation.LeftToRight }

            TextField {
                id: inputField
                hintText: "Nhan tin..."
                verticalAlignment: VerticalAlignment.Center
                layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                inputMode: TextFieldInputMode.Chat
                onTextChanging: {
                    sendAction.enabled = (inputField.text.trim().length > 0);
                }
            }
        }
    }

    attachedObjects: [
        Connections {
            target: zService

            onMessagesReady: {
                if (threadId !== chatViewPage.threadId) return;
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
                if (threadId !== chatViewPage.threadId) return;
                sendAction.enabled = true;
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
                if (threadId !== chatViewPage.threadId) return;
                for (var i = 0; i < msgModel.size(); i++) {
                    if (msgModel.value(i).msgId === message.msgId) return;
                }
                msgModel.append(message);
                msgList.scrollToPosition(ScrollPosition.End, ScrollAnimation.Smooth);
            }
        }
    ]
}
