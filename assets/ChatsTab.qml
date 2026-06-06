import bb.cascades 1.4
import QtQuick 1.0

NavigationPane {
    id: chatsNav
    peekEnabled: false

    onPopTransitionEnded: {
        zService.clearActiveThread();
    }

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
                    }
                }
            }
        }

        actions: [
            ActionItem {
                title: "Refresh"
                imageSource: "asset:///images/ic_sync.png"
                ActionBar.placement: ActionBarPlacement.OnBar
                onTriggered: {
                    friendModel.clear();
                    zService.fetchFriends();
                    chatsLoading.visible = true;
                }
            },
            ActionItem {
                title: "Mark All as Read"
                imageSource: "asset:///images/ai_add_task.png"
                ActionBar.placement: ActionBarPlacement.InOverflow
                onTriggered: {}
            },
            ActionItem {
                title: "Edit Status"
                imageSource: "asset:///images/edit.png"
                ActionBar.placement: ActionBarPlacement.InOverflow
                onTriggered: {}
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
                dataModel: ArrayDataModel { id: friendModel }

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
                                        imageSource: ListItemData.localAvatar ? ListItemData.localAvatar : "asset:///images/blank.png"
                                        horizontalAlignment: HorizontalAlignment.Fill
                                        verticalAlignment: VerticalAlignment.Fill
                                        scalingMethod: ScalingMethod.AspectFill
                                    }
                                }

                                Container {
                                    leftPadding: ui.du(13.0)
                                    rightPadding: ui.du(2.0)
                                    verticalAlignment: VerticalAlignment.Center
                                    layout: StackLayout { orientation: LayoutOrientation.TopToBottom }
                                    horizontalAlignment: HorizontalAlignment.Fill
                                    layoutProperties: StackLayoutProperties { spaceQuota: 1 }

                                    Label {
                                        text: ListItemData.name || ListItemData.displayName || "Unknown User"
                                        textStyle { base: SystemDefaults.TextStyles.TitleText }
                                    }

                                    Container {
                                        layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
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
                                            }
                                            multiline: false
                                            layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                                        }
                                        Label {
                                            text: ListItemData.lastTime || ""
                                            textStyle {
                                                base: SystemDefaults.TextStyles.SubtitleText
                                                color: Color.Gray
                                                fontSize: FontSize.Small
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                ]

                onTriggered: {
                    var item = dataModel.data(indexPath);
                    var page = chatsDef.createObject();
                    if (!page) return;
                    page.threadId   = item.threadId || item.uid || "";
                    page.threadName = item.name || "Chat";
                    page.isGroup    = false;
                    page.avatarUrl  = item.localAvatar || item.avatar || "";
                    page.selfName   = chatsNav.selfName;
                    page.startChat();
                    var idx = indexPath[0];
                    var d = friendModel.value(idx);
                    if (d) { d.hasUnread = false; friendModel.replace(idx, d); }
                    chatsNav.push(page);
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
            ComponentDefinition {
                id: chatsDef
                source: "asset:///ChatView.qml"
            },
            Connections {
                target: zService

                onFriendsReady: {
                    chatsLoading.visible = false;
                    friendModel.clear();
                    for (var i = 0; i < friends.length; i++) {
                        var f = friends[i];
                        f.localAvatar    = "";
                        f.hasUnread      = false;
                        f.lastMsgIsMine  = false;
                        f.lastSenderName = "";
                        f.lastMessage    = f.lastMessage || f.lastMsg || "";
                        if (f.lastTime && f.lastTime !== "") {
                            var ts = parseInt(f.lastTime);
                            if (!isNaN(ts)) f.lastTime = chatsNav.formatTime(ts);
                        }
                        friendModel.append(f);
                        var url = f.avatar || "";
                        var tid = f.threadId || f.uid || "";
                        if (url.length > 0 && tid.length > 0)
                            zService.downloadAvatar(tid, url);
                    }
                    chatsEmpty.visible = (friends.length === 0);
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
                }

                onLoginSuccess: {
                    if (typeof displayName !== "undefined" && displayName.length > 0)
                        chatsNav.selfName = displayName;
                    zService.fetchFriends();
                    chatsLoading.visible = true;
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
                            friendModel.removeAt(i);
                            friendModel.insert(0, d);
                            return;
                        }
                    }
                }
            }
        ]
    }

    // Properties set by main.qml
    property string selfName: ""
    signal onUnreadMessage()

    // formatTime delegated from root
    property variant formatTime: null
}
