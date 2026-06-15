import bb.cascades 1.4
import bb.system 1.0
import QtQuick 1.0

NavigationPane {
    id: groupsNav
    peekEnabled: false

    property string selfName: ""
    signal onUnreadMessage()
    property variant currentPage: null
    property bool searchVisible: false
    property string searchText: ""
    property variant allGroups: []
    property bool refreshCooldown: false

    attachedObjects: [
        Timer {
            id: groupsRefreshCooldownTimer
            interval: 11000
            repeat: false
            onTriggered: groupsNav.refreshCooldown = false
        }
    ]

    function filterList() {
        var q = groupsNav.searchText.toLowerCase().trim();
        searchModel.clear();
        for (var i = 0; i < groupModel.size(); i++) {
            var g = groupModel.value(i);
            if (q.length === 0) {
                searchModel.append(g);
            } else {
                var name = (g.name || "").toLowerCase();
                if (name.indexOf(q) !== -1) searchModel.append(g);
            }
        }
    }

    onCurrentPageChanged: {
        if (!currentPage) return;
        popWatcher.target = currentPage;
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

    onPopTransitionEnded: {
        zService.clearActiveThread();
    }

    onPushTransitionEnded: {
        if (page && typeof page.flushPendingImages === "function") {
            page.pageVisible = true;
            page.flushPendingImages();
        }
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
                        visible: !groupsNav.searchVisible
                    }
                    Container {
                        visible: groupsNav.searchVisible
                        horizontalAlignment: HorizontalAlignment.Fill
                        verticalAlignment: VerticalAlignment.Center
                        layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                        TextField {
                            id: groupsSearchField
                            hintText: "Search groups..."
                            verticalAlignment: VerticalAlignment.Center
                            textStyle { color: Color.White }
                            layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                            onTextChanging: {
                                groupsNav.searchText = text;
                                groupsNav.filterList();
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
                                groupsSearchField.text = "";
                                groupsNav.searchText = "";
                                groupsNav.searchVisible = false;
                                groupsNav.filterList();
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
                    groupsNav.searchVisible = !groupsNav.searchVisible;
                    if (!groupsNav.searchVisible) {
                        groupsNav.searchText = "";
                        groupsNav.filterList();
                    }
                }
            }
        ]

        actions: [
            ActionItem {
                title: groupsNav.refreshCooldown ? "Please wait..." : "Refresh"
                enabled: !groupsNav.refreshCooldown
                imageSource: "asset:///images/GroupsTab/ic_sync.png"
                ActionBar.placement: ActionBarPlacement.OnBar
                onTriggered: {
                    if (groupsNav.refreshCooldown) return;
                    groupsNav.refreshCooldown = true;
                    groupsRefreshCooldownTimer.restart();
                    groupModel.clear();
                    zService.fetchConversations();
                    groupsLoading.visible = true;
                }
            },
            ActionItem {
                title: "Create Group"
                imageSource: "asset:///images/GroupsTab/ic_create_group_disabled.png"
                ActionBar.placement: ActionBarPlacement.InOverflow
                onTriggered: { createGroupDialog.show() }
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
                dataModel: groupsNav.searchVisible ? searchModel : groupModel

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
                                    imageSource: ListItemData.localAvatar ? ListItemData.localAvatar : "asset:///images/GroupsTab/blank.png"
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
                    if (av.length === 0) av = "asset:///images/GroupsTab/blank.png";
                    page.avatarUrl  = av;
                    page.selfName   = groupsNav.selfName;
                    page.startChat();
                    groupsNav.push(page);
                    groupsNav.currentPage = page;
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
            ArrayDataModel { id: groupModel },
            SystemDialog {
                id: createGroupDialog
                title: "Create Group"
                body: "This feature is still under development."
                confirmButton.label: "OK"
                cancelButton.label: ""
                cancelButton.enabled: false
            },
            ArrayDataModel { id: searchModel },
            ComponentDefinition {
                id: groupsDef
                source: "asset:///ChatView.qml"
            },
            Connections {
                id: popWatcher
                target: null
                onPopRequestedChanged: {
                    if (target && target.popRequested)
                        groupsNav.pop();
                }
            },
            Connections {
                target: zService

                onConversationsReady: {
                    groupsLoading.visible = false;
                    groupModel.clear();
                    var arr = [];
                    for (var i = 0; i < threads.length; i++) {
                        if (!threads[i].isGroup) continue;
                        var g = threads[i];
                        g.localAvatar = "";
                        if (g.lastTime && g.lastTime !== "") {
                            var ts = parseInt(g.lastTime);
                            if (!isNaN(ts)) g.lastTime = groupsNav.formatTime(ts);
                        }
                        arr.push(g);
                        groupModel.append(g);
                        var url = g.avatar || "";
                        var tid = g.threadId || "";
                        if (url.length > 0 && tid.length > 0)
                            zService.downloadAvatar(tid, url);
                    }
                    groupsNav.allGroups = arr;
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
                    if (groupsNav.searchVisible) {
                        for (var j = 0; j < searchModel.size(); j++) {
                            var sd = searchModel.value(j);
                            if (sd.threadId === threadId) {
                                sd.localAvatar = localPath;
                                searchModel.replace(j, sd);
                                break;
                            }
                        }
                    }
                }

                onLoginSuccess: {
                    if (groupModel.size() === 0) {
                        groupsNav.refreshCooldown = true;
                        groupsRefreshCooldownTimer.restart();
                        zService.fetchConversations();
                        groupsLoading.visible = true;
                    }
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

                onClearHistoryDone: {
                    if (!success) return;
                    for (var i = 0; i < groupModel.size(); i++) {
                        var d = groupModel.value(i);
                        if (d.threadId === threadId) {
                            d.lastMessage    = "";
                            d.lastMsgIsMine  = false;
                            d.lastSenderName = "";
                            groupModel.replace(i, d);
                            return;
                        }
                    }
                }

                onLeaveGroupDone: {
                    if (!success) return;
                    for (var i = 0; i < groupModel.size(); i++) {
                        if (groupModel.value(i).threadId === groupId) {
                            groupModel.removeAt(i);
                            return;
                        }
                    }
                }
            }
        ]
    }

    function openThread(threadId, isGroup) {
        groupsNav.popToRoot();
        var page = groupsDef.createObject();
        if (!page) return;
        var threadName = "Group";
        var avatarUrl  = "asset:///images/GroupsTab/blank.png";
        for (var i = 0; i < groupModel.size(); i++) {
            var d = groupModel.value(i);
            if ((d.threadId || "") === threadId) {
                threadName = d.name || "Group";
                var av = d.localAvatar || d.avatar || "";
                if (av.length > 0) avatarUrl = av;
                break;
            }
        }
        page.threadId   = threadId;
        page.threadName = threadName;
        page.isGroup    = true;
        page.avatarUrl  = avatarUrl;
        page.selfName   = groupsNav.selfName;
        page.startChat();
        groupsNav.push(page);
        groupsNav.currentPage = page;
    }
}
