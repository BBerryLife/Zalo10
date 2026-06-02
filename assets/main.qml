import bb.cascades 1.4
import bb.system 1.0
import QtQuick 1.0

TabbedPane {
    id: root
    showTabsOnActionBar: false
    sidebarState: SidebarState.VisibleCompact

    // Self display name 2014 populated on loginSuccess
    property string selfName: "Me"
    
    // ========== MENU TRƯỢT (TRÁI - HỆ THỐNG) ==========
    Menu.definition: MenuDefinition {
        actions: [
            ActionItem {
                title: "About"
                imageSource: "asset:///images/ic_info.png"
                onTriggered: { aboutSheet.open() }
            },
            ActionItem {
                title: "Settings"
                imageSource: "asset:///images/ic_settings.png"
                onTriggered: { settingsSheet.open() }
            },
            ActionItem {
                title: "Email"
                imageSource: "asset:///images/ic_mail.png"
                onTriggered: {
                    app.invokeEmail("Berrylife2025@gmail.com", "Zalo10 Feedback")
                }
            }
        ]
    }
    
    // ========== HÀM FORMAT THỜI GIAN ==========
    function formatTime(timestamp) {
        if (!timestamp || timestamp === "") return "";
        var date = new Date(timestamp * 1);
        var now = new Date();
        
        if (date.toDateString() === now.toDateString()) {
            var hours = date.getHours();
            var mins = date.getMinutes();
            var ampm = hours >= 12 ? 'PM' : 'AM';
            hours = hours % 12 || 12;
            return hours + ":" + (mins < 10 ? '0' : '') + mins + " " + ampm;
        } 
        else if ((now - date) < 7 * 24 * 60 * 60 * 1000) {
            var days = ["Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"];
            return days[date.getDay()];
        }
        else {
            var month = ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"][date.getMonth()];
            return month + " " + date.getDate();
        }
    }
    
    onCreationCompleted: {
        splashDialog.open();
        splashTimer.start();
    }
    
    // ========== TAB 1: CHATS (PFP to chạm divider) ==========
    Tab {
        id: chatsTab
        title: "Chats"
        description: "Messages"
        imageSource: "asset:///images/ic_bbm.png"
        
        NavigationPane {
            id: chatsNav
            peekEnabled: false

            // onPopTransitionEnded là signal đúng của NavigationPane (không phải Page).
            // Dùng để clearActiveThread khi user back ra khỏi ChatView.
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
                        onTriggered: { }
                    },
                    ActionItem {
                        title: "Edit Status"
                        imageSource: "asset:///images/edit.png"
                        ActionBar.placement: ActionBarPlacement.InOverflow
                        onTriggered: { }
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
                        
                        function itemType(data, indexPath) {
                            return "item";
                        }
                        
                        listItemComponents: [
                            ListItemComponent {
                                type: "item"
                                CustomListItem {
                                    dividerVisible: true
                                    Container {
                                        layout: DockLayout {}
                                        preferredHeight: ui.du(12.0)
                                        
                                        // PFP - to bằng chiều cao hàng để chạm divider
                                        ImageView {
                                            imageSource: ListItemData.localAvatar ? ListItemData.localAvatar : "asset:///images/blank.png"
                                            preferredWidth: ui.du(12.0)
                                            preferredHeight: ui.du(12.0)
                                            horizontalAlignment: HorizontalAlignment.Left
                                            verticalAlignment: VerticalAlignment.Center
                                            scalingMethod: ScalingMethod.AspectFill
                                        }
                                        
                                        // Text container
                                        Container {
                                            leftPadding: ui.du(13.0)
                                            rightPadding: ui.du(2.0)
                                            verticalAlignment: VerticalAlignment.Center
                                            layout: StackLayout { orientation: LayoutOrientation.TopToBottom }
                                            horizontalAlignment: HorizontalAlignment.Fill
                                            layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                                            
                                            Label {
                                                text: ListItemData.name || ListItemData.displayName || "Unknown User"
                                                textStyle {
                                                    base: SystemDefaults.TextStyles.TitleText
                                                }
                                            }
                                            
                                            Container {
                                                layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                                                
                                                Label {
                                                    text: (ListItemData.lastMessage || ListItemData.lastMsg || "No messages yet")
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
                            var item = dataModel.data(indexPath)
                            var page = chatsDef.createObject()
                            if (!page) return
                            page.threadId = item.threadId || item.uid || ""
                            page.threadName = item.name || "Chat"
                            page.isGroup = false
                            page.avatarUrl = item.localAvatar || item.avatar || ""
                            page.selfName = root.selfName
                            chatsNav.push(page)
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
                            chatsLoading.visible = false
                            friendModel.clear()
                            for (var i = 0; i < friends.length; i++) {
                                var f = friends[i]
                                f.localAvatar = ""
                                if (f.lastTime && f.lastTime !== "") {
                                    var ts = parseInt(f.lastTime)
                                    if (!isNaN(ts)) f.lastTime = root.formatTime(ts)
                                }
                                friendModel.append(f)
                                var url = f.avatar || ""
                                var tid = f.threadId || f.uid || ""
                                if (url.length > 0 && tid.length > 0)
                                    zService.downloadAvatar(tid, url)
                            }
                            chatsEmpty.visible = (friends.length === 0)
                        }
                        onAvatarReady: {
                            for (var i = 0; i < friendModel.size(); i++) {
                                var d = friendModel.value(i)
                                if ((d.threadId || d.uid || "") === threadId) {
                                    d.localAvatar = localPath
                                    friendModel.replace(i, d)
                                    break
                                }
                            }
                        }
                        onLoginSuccess: {
                            if (typeof displayName !== "undefined" && displayName.length > 0)
                                root.selfName = displayName;
                            zService.fetchFriends()
                            chatsLoading.visible = true
                        }
                    }
                ]
            }
        }
    }
    
    // ========== TAB 2: CONTACTS (tách sang ContactsTab.qml) ==========
    Tab {
        id: contactsTab
        title: "Contacts"
        description: "Friends"
        imageSource: "asset:///images/ic_contact.png"

        attachedObjects: [
            ComponentDefinition {
                id: contactsTabDef
                source: "asset:///ContactsTab.qml"
            }
        ]

        onCreationCompleted: {
            contactsTab.content = contactsTabDef.createObject()
        }
    }
    
    // ========== TAB 3: GROUPS (PFP to chạm divider) ==========
    Tab {
        id: groupsTab
        title: "Groups"
        description: "Group chats"
        imageSource: "asset:///images/ic_groups_white.png"
        
        NavigationPane {
            id: groupsNav
            peekEnabled: false

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
                        onTriggered: { }
                    }
                ]
                
                onCreationCompleted: {
                    if (zService.loggedIn) {
                        zService.fetchConversations();
                        groupsLoading.visible = true;
                    }
                }
                
                content: Container {
                    layout: DockLayout {}
                    horizontalAlignment: HorizontalAlignment.Fill
                    verticalAlignment: VerticalAlignment.Fill
                    
                    ListView {
                        id: groupList
                        horizontalAlignment: HorizontalAlignment.Fill
                        verticalAlignment: VerticalAlignment.Fill
                        dataModel: ArrayDataModel { id: groupModel }
                        
                        function itemType(data, indexPath) {
                            return "item";
                        }
                        
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
                                                textStyle {
                                                    base: SystemDefaults.TextStyles.TitleText
                                                }
                                            }
                                            
                                            Container {
                                                layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                                                
                                                Label {
                                                    text: (ListItemData.lastMessage || "No messages yet")
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
                            var item = dataModel.data(indexPath)
                            var page = groupsDef.createObject()
                            if (!page) return
                            page.threadId = item.threadId || ""
                            page.threadName = item.name || "Group"
                            page.isGroup = true
                            page.avatarUrl = item.localAvatar || item.avatar || ""
                            page.selfName = root.selfName
                            groupsNav.push(page)
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
                            groupsLoading.visible = false
                            groupModel.clear()
                            for (var i = 0; i < threads.length; i++) {
                                if (!threads[i].isGroup) continue
                                var g = threads[i]
                                g.localAvatar = ""
                                if (g.lastTime && g.lastTime !== "") {
                                    var ts = parseInt(g.lastTime)
                                    if (!isNaN(ts)) g.lastTime = root.formatTime(ts)
                                }
                                groupModel.append(g)
                                var url = g.avatar || ""
                                var tid = g.threadId || ""
                                if (url.length > 0 && tid.length > 0)
                                    zService.downloadAvatar(tid, url)
                            }
                        }
                        onAvatarReady: {
                            for (var i = 0; i < groupModel.size(); i++) {
                                var d = groupModel.value(i)
                                if (d.threadId === threadId) {
                                    d.localAvatar = localPath
                                    groupModel.replace(i, d)
                                    break
                                }
                            }
                        }
                        onLoginSuccess: {
                            zService.fetchConversations()
                            groupsLoading.visible = true
                        }
                    }
                ]
            }
        }
    }
    
    // ========== TAB 4: INVITES ==========
    Tab {
        id: invitesTab
        title: "Invites"
        description: "Friend requests"
        imageSource: "asset:///images/ic_add_contact.png"
        
        NavigationPane {
            id: invitesNav
            peekEnabled: false
            
            Page {
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
                                text: "Friend Requests"
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
                            inviteModel.clear();
                            zService.fetchInvites();
                            invitesLoading.visible = true;
                        }
                    },
                    ActionItem {
                        title: "Accept All"
                        imageSource: "asset:///images/ai_add_task.png"
                        ActionBar.placement: ActionBarPlacement.InOverflow
                        onTriggered: { }
                    }
                ]
                
                onCreationCompleted: {
                    if (zService.loggedIn) {
                        zService.fetchInvites()
                        invitesLoading.visible = true
                    }
                }
                
                content: Container {
                    layout: DockLayout {}
                    horizontalAlignment: HorizontalAlignment.Fill
                    verticalAlignment: VerticalAlignment.Fill
                    
                    ListView {
                        horizontalAlignment: HorizontalAlignment.Fill
                        verticalAlignment: VerticalAlignment.Fill
                        dataModel: ArrayDataModel { id: inviteModel }
                        
                        function itemType(data, indexPath) {
                            return "item";
                        }
                        
                        listItemComponents: [
                            ListItemComponent {
                                type: "item"
                                CustomListItem {
                                    dividerVisible: true
                                    Container {
                                        layout: DockLayout {}
                                        preferredHeight: ui.du(14.0)
                                        
                                        // Avatar
                                        ImageView {
                                            imageSource: ListItemData.localAvatar ? ListItemData.localAvatar : "asset:///images/blank.png"
                                            preferredWidth: ui.du(10.0)
                                            preferredHeight: ui.du(10.0)
                                            horizontalAlignment: HorizontalAlignment.Left
                                            verticalAlignment: VerticalAlignment.Center
                                            scalingMethod: ScalingMethod.AspectFill
                                            leftMargin: ui.du(2)
                                        }
                                        
                                        // Tên + tin nhắn
                                        Container {
                                            leftPadding: ui.du(14.0)
                                            rightPadding: ui.du(2.0)
                                            verticalAlignment: VerticalAlignment.Center
                                            layout: StackLayout { orientation: LayoutOrientation.TopToBottom }
                                            horizontalAlignment: HorizontalAlignment.Fill
                                            layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                                            
                                            Label {
                                                text: ListItemData.name || "Unknown User"
                                                textStyle {
                                                    base: SystemDefaults.TextStyles.TitleText
                                                    fontWeight: FontWeight.Bold
                                                }
                                            }
                                            
                                            Label {
                                                text: ListItemData.msg || "Muốn kết bạn với bạn"
                                                textStyle {
                                                    base: SystemDefaults.TextStyles.SubtitleText
                                                    color: Color.DarkGray
                                                }
                                                multiline: false
                                            }
                                            
                                            // Nút Accept nhỏ gọn
                                            Button {
                                                text: "Chấp nhận"
                                                topMargin: ui.du(0.8)
                                                preferredHeight: ui.du(5)
                                                onClicked: {
                                                    // TODO: gọi zService.acceptFriendRequest(ListItemData.uid)
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        ]
                    }
                    
                    ActivityIndicator {
                        id: invitesLoading
                        horizontalAlignment: HorizontalAlignment.Center
                        verticalAlignment: VerticalAlignment.Center
                        preferredWidth: ui.du(12)
                        preferredHeight: ui.du(12)
                        running: visible
                        visible: false
                    }
                    
                    Label {
                        id: invEmpty
                        text: "No friend requests"
                        visible: false
                        horizontalAlignment: HorizontalAlignment.Center
                        verticalAlignment: VerticalAlignment.Center
                    }
                }
                
                attachedObjects: [
                    Connections {
                        target: zService
                        onInvitesReady: {
                            invitesLoading.visible = false
                            inviteModel.clear()
                            for (var i = 0; i < invites.length; i++) {
                                var inv = invites[i]
                                inv.localAvatar = ""
                                inviteModel.append(inv)
                                var url = inv.avatar || ""
                                var tid = inv.uid || ""
                                if (url.length > 0 && tid.length > 0)
                                    zService.downloadAvatar(tid, url)
                            }
                            invEmpty.visible = (invites.length === 0)
                        }
                        onAvatarReady: {
                            for (var i = 0; i < inviteModel.size(); i++) {
                                var d = inviteModel.value(i)
                                if ((d.uid || "") === threadId) {
                                    d.localAvatar = localPath
                                    inviteModel.removeAt(i)
                                    inviteModel.insert(i, d)
                                    break
                                }
                            }
                        }
                        onLoginSuccess: { zService.fetchInvites() }
                    }
                ]
            }
        }
    }
    
    // ========== SPLASH SCREEN, TIMER & SHEETS ==========
    attachedObjects: [
        Dialog {
            id: splashDialog
            Container {
                background: Color.create("#2575fc")
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment: VerticalAlignment.Fill
                layout: DockLayout {}
                
                Label {
                    text: "Zalo10"
                    textStyle {
                        color: Color.White
                        fontWeight: FontWeight.Bold
                        fontSize: FontSize.PointValue
                        fontSizeValue: 30
                    }
                    horizontalAlignment: HorizontalAlignment.Center
                    verticalAlignment: VerticalAlignment.Center
                }
            }
        },
        
        Timer {
            id: splashTimer
            interval: 2000
            repeat: false
            onTriggered: {
                splashDialog.close();
                if (!zService.loadSession()) {
                    loginSheet.open();
                } else {
                    zService.fetchConversations();
                    zService.fetchFriends();
                    zService.fetchInvites();
                }
            }
        },
        
        Sheet {
            id: loginSheet
            LoginView {
                onLoginSuccessful: { 
                    loginSheet.close();
                    zService.fetchConversations();
                    zService.fetchFriends();
                    zService.fetchInvites();
                }
            }
        },
        SettingsSheet { id: settingsSheet },
        AboutSheet { id: aboutSheet }
    ]
}