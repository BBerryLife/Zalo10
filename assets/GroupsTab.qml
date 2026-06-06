import bb.cascades 1.4
import QtQuick 1.0

NavigationPane {
    id: groupsNav
    peekEnabled: false

    property string selfName: ""
    signal onUnreadMessage()

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

    onPopTransitionEnded: {
        zService.clearActiveThread();
    }

    Page {
        id: groupsPage

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
                        text: "Groups"
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
                    groupModel.clear();
                    zService.fetchConversations();
                    groupsLoading.visible = true;
                }
            },
            ActionItem {
                title: "Create Group"
                imageSource: "asset:///images/ic_create_group_disabled.png"
                ActionBar.placement: ActionBarPlacement.InOverflow
                onTriggered: {}
            }
        ]

        content: Container {
            layout: DockLayout {}
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment: VerticalAlignment.Fill

            ListView {
                id: groupList
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment: VerticalAlignment.Fill
                dataModel: ArrayDataModel { id: groupModel }

                function itemType(data, indexPath) { return "item"; }

                listItemComponents: [
                    ListItemComponent {
                        type: "item"
                        CustomListItem {
                            dividerVisible: true
                            Container {
                                layout: DockLayout {}
                                preferredHeight: ui.du(12.0)

                                ImageView {
                                    imageSource: ListItemData.localAvatar ? ListItemData.localAvatar : "asset:///images/blank.png"
                                    preferredWidth: ui.du(12.0)
                                    preferredHeight: ui.du(12.0)
                                    horizontalAlignment: HorizontalAlignment.Left
                                    verticalAlignment: VerticalAlignment.Center
                                    scalingMethod: ScalingMethod.AspectFill
                                }

                                Container {
                                    leftPadding: ui.du(13.0)
                                    rightPadding: ui.du(2.0)
                                    verticalAlignment: VerticalAlignment.Center
                                    layout: StackLayout { orientation: LayoutOrientation.TopToBottom }
                                    horizontalAlignment: HorizontalAlignment.Fill
                                    layoutProperties: StackLayoutProperties { spaceQuota: 1 }

                                    Label {
                                        text: ListItemData.name || "Unknown Group"
                                        textStyle { base: SystemDefaults.TextStyles.TitleText }
                                    }

                                    Container {
                                        layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                                        Label {
                                            text: ListItemData.lastMessage || "No messages yet"
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
                    var page = groupsDef.createObject();
                    if (!page) return;
                    page.threadId   = item.threadId || "";
                    page.threadName = item.name || "Group";
                    page.isGroup    = true;
                    var av = item.localAvatar || item.avatar || "";
                    if (av.length === 0) av = "asset:///images/blank.png";
                    page.avatarUrl  = av;
                    page.selfName   = groupsNav.selfName;
                    page.startChat();
                    groupsNav.push(page);
                }
            }

            ActivityIndicator {
                id: groupsLoading
                horizontalAlignment: HorizontalAlignment.Center
                verticalAlignment: VerticalAlignment.Center
                preferredWidth: ui.du(12)
                preferredHeight: ui.du(12)
                running: visible
                visible: false
            }
        }

        attachedObjects: [
            ComponentDefinition {
                id: groupsDef
                source: "asset:///ChatView.qml"
            },
            Connections {
                target: zService

                onConversationsReady: {
                    groupsLoading.visible = false;
                    groupModel.clear();
                    for (var i = 0; i < threads.length; i++) {
                        if (!threads[i].isGroup) continue;
                        var g = threads[i];
                        g.localAvatar = "";
                        if (g.lastTime && g.lastTime !== "") {
                            var ts = parseInt(g.lastTime);
                            if (!isNaN(ts)) g.lastTime = groupsNav.formatTime(ts);
                        }
                        groupModel.append(g);
                        var url = g.avatar || "";
                        var tid = g.threadId || "";
                        if (url.length > 0 && tid.length > 0)
                            zService.downloadAvatar(tid, url);
                    }
                }

                onAvatarReady: {
                    for (var i = 0; i < groupModel.size(); i++) {
                        var d = groupModel.value(i);
                        if (d.threadId === threadId) {
                            d.localAvatar = localPath;
                            groupModel.replace(i, d);
                            break;
                        }
                    }
                }

                onLoginSuccess: {
                    zService.fetchConversations();
                    groupsLoading.visible = true;
                }

                onNewMessage: {
                    if (message.isGroup === true || message.isGroup === "true") {
                        var isMine = (message.isMine === true || message.isMine === "true" || message.isMine === 1);
                        var tid = threadId;
                        var snippet = (message.msgType === 2 || message.msgType === "2")
                            ? "[Photo]" : (message.content || "").substring(0, 60);
                        if (!isMine) groupsNav.onUnreadMessage();
                        for (var i = 0; i < groupModel.size(); i++) {
                            var d = groupModel.value(i);
                            if (d.threadId === tid) {
                                d.lastMessage    = snippet;
                                d.lastMsgIsMine  = isMine;
                                d.lastSenderName = message.dName || "";
                                d.hasUnread      = !isMine;
                                groupModel.removeAt(i);
                                groupModel.insert(0, d);
                                break;
                            }
                        }
                    }
                }
            }
        ]
    }
}
