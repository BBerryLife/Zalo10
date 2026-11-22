import bb.cascades 1.4
import bb.system 1.0
import QtQuick 1.0
import "lists"
import "lists/avatarhub.js" as AvatarHub

NavigationPane {
    id: contactsNav
    signal myProfileRequested()
    peekEnabled: false

    property string selfName: ""
    property string selfAvatar: ""
    onSelfNameChanged: contactsPage.selfName = selfName

    property bool searchVisible: false
    property string searchText: ""
    property bool refreshCooldown: false
    property int  friendsRetries: 0
    property bool isDark: app.getDarkTheme()
    // Number of contacts, shown as the Contacts tab subtitle ("N Contacts") by main.qml.
    property int contactCount: 0

    // Friends go into the model in slices so a few thousand rows never stall the UI.
    property variant pendingRows: []
    property int pendingPos: 0

    function appendPending(n) {
        var rows = contactsNav.pendingRows;
        var end = Math.min(rows.length, contactsNav.pendingPos + n);
        for (var j = contactsNav.pendingPos; j < end; j++) {
            var cf = rows[j];
            contactModel.append(cf);
            // This tab asks for its own avatars (paced, see lists/avatarhub.js); the Chats tab no
            // longer has the friend list, so nobody else does it.
            var curl = cf.avatar || "";
            var cid  = cf.threadId || cf.uid || "";
            if (curl.length > 0 && cid.length > 0 && !cf.localAvatar)
                AvatarHub.enqueue("contacts", cid, curl);
        }
        contactsNav.pendingPos = end;
        if (AvatarHub.pending("contacts") && !contactsAvatarTimer.running) contactsAvatarTimer.start();
    }

    function resetPending() {
        contactAppendTimer.stop();
        AvatarHub.reset("contacts");
        contactsNav.pendingRows = [];
        contactsNav.pendingPos = 0;
    }

    function filterList() {
        var q = contactsNav.searchText.toLowerCase().trim();
        searchModel.clear();
        AvatarHub.invalidate("contactsSearch");
        for (var i = 0; i < contactModel.size(); i++) {
            var f = contactModel.value(i);
            if (q.length === 0) {
                searchModel.append(f);
            } else {
                var name = (f.name || f.displayName || "").toLowerCase();
                if (name.indexOf(q) !== -1) searchModel.append(f);
            }
        }
        contactsEmpty.visible = (contactModel.size() === 0);
    }

    // ---- Lite: avatar updates in O(1) instead of scanning the whole grid ----
    // Avatar requests are paced by this tab's own queue (contactsAvatarTimer); avatarReady lands here.
    // Cover images ("bg_<id>") are no longer fetched for every friend at
    // start-up: ProfileView downloads the cover when that profile is opened.
    function applyAvatar(threadId, localPath) {
        var isBg = (threadId.indexOf("bg_") === 0);
        var id   = isBg ? threadId.substring(3) : threadId;
        if (!isBg) AvatarHub.arrived("contacts", id);   // frees its download slot
        var hit = AvatarHub.find("contacts", contactModel, id);
        if (hit) {
            if (isBg) {
                if (hit.row.localBgAvatar !== localPath) { hit.row.localBgAvatar = localPath; contactModel.replace(hit.i, hit.row); }
            } else {
                if (hit.row.localAvatar !== localPath)   { hit.row.localAvatar   = localPath; contactModel.replace(hit.i, hit.row); }
            }
        }
        if (contactsNav.searchVisible) {
            var sh = AvatarHub.find("contactsSearch", searchModel, id);
            if (sh) {
                if (isBg) {
                    if (sh.row.localBgAvatar !== localPath) { sh.row.localBgAvatar = localPath; searchModel.replace(sh.i, sh.row); }
                } else {
                    if (sh.row.localAvatar !== localPath)   { sh.row.localAvatar   = localPath; searchModel.replace(sh.i, sh.row); }
                }
            }
        }
    }

    attachedObjects: [
        Timer {
            id: refreshCooldownTimer
            interval: 11000
            repeat: false
            onTriggered: contactsNav.refreshCooldown = false
        },
        Timer {
            // The Chats tab no longer fetches friends, so this tab fetches its own list and retries
            // (a few times) when the fetch ends without a usable list.
            id: friendsRetryTimer
            interval: 20000
            repeat: false
            onTriggered: {
                if (contactsPage.populated || contactsNav.friendsRetries >= 3) return;
                contactsNav.friendsRetries++;
                contactsLoading.visible = true;
                zService.fetchFriends();
            }
        }
    ]

    Page {
        id: contactsPage
        property bool populated: false
        property string selfName: contactsNav.selfName

        titleBar: ProfileTitleBar {
            selfName:   contactsNav.selfName
            selfAvatar: contactsNav.selfAvatar
            profileHandled: true
            onProfileClicked: contactsNav.myProfileRequested()
            addIcon: "asset:///images/ContactsTab/ic_add_contact.png"
            addDialogTitle: "Add Contact"
        }

        shortcuts: [
            Shortcut {
                key: "s"
                onTriggered: { contactsSearchField.requestFocus(); }
            }
        ]

        actions: [
            ActionItem {
                id: contactsRefreshAction
                title: contactsNav.refreshCooldown ? "Please wait..." : "Refresh"
                enabled: !contactsNav.refreshCooldown
                imageSource: "asset:///images/ContactsTab/ic_sync.png"
                ActionBar.placement: ActionBarPlacement.InOverflow
                onTriggered: {
                    if (contactsNav.refreshCooldown) return;
                    contactsNav.refreshCooldown = true;
                    refreshCooldownTimer.restart();
                    contactsNav.resetPending()
                    contactsPage.populated = false
                    contactModel.clear()
                    AvatarHub.invalidate("contacts")
                    zService.fetchFriends()
                    contactsLoading.visible = true
                }
            }
        ]

        Container {
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment:   VerticalAlignment.Fill

            FocusSink {}

            // Search box under the header, drawn with a visible border. Colours follow the theme
            // (Settings > Dark Theme) live, through contactsNav.isDark.
            Container {
                background: contactsNav.isDark ? Color.create("#000000") : Color.create("#f2f2f2")
                horizontalAlignment: HorizontalAlignment.Fill
                topPadding: ui.du(1.0); bottomPadding: ui.du(1.0)
                leftPadding: ui.du(1.5); rightPadding: ui.du(1.5)
                Container {
                    background: contactsNav.isDark ? Color.create("#3a3a3a") : Color.create("#9a9a9a")
                    horizontalAlignment: HorizontalAlignment.Fill
                    topPadding: ui.du(0.2); bottomPadding: ui.du(0.2)
                    leftPadding: ui.du(0.2); rightPadding: ui.du(0.2)
                    Container {
                        background: contactsNav.isDark ? Color.create("#1a1a1a") : Color.White
                        horizontalAlignment: HorizontalAlignment.Fill
                        TextField {
                            id: contactsSearchField
                            hintText: "Search contacts..."
                            backgroundVisible: false
                            horizontalAlignment: HorizontalAlignment.Fill
                            textStyle.color: contactsNav.isDark ? Color.White : Color.Black
                            onTextChanging: {
                                contactsNav.searchText = text;
                                contactsNav.searchVisible = (text.trim().length > 0);
                                contactsNav.filterList();
                            }
                            onCreationCompleted: {
                                inputMode.type = TextInputFlag.AutoCapitalizationOff | TextInputFlag.AutoCorrectionOff | TextInputFlag.SpellCheckOff | TextInputFlag.PredictionOff;
                            }
                        }
                    }
                }
            }

            Container {
            layout: DockLayout {}
            layoutProperties: StackLayoutProperties { spaceQuota: 1 }
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment:   VerticalAlignment.Fill
            // Outer margin = half the gap between tiles, like the BBM contacts grid.
            leftPadding: ui.du(1.0); rightPadding: ui.du(1.0)

            ListView {
                id: contactsGrid
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment:   VerticalAlignment.Fill

                // Still exactly 4 tiles per row: the layout divides (width - 3 gaps) by 4. The gap is
                // thicker (was 0.1 du) to match the BBM contacts grid.
                layout: GridListLayout {
                    columnCount: 4
                    headerMode: ListHeaderMode.None
                    horizontalCellSpacing: ui.du(2.0)
                    verticalCellSpacing:   ui.du(2.0)
                }

                property variant profileDef: contactsProfileDef
                property variant navPane:    contactsNav
                property string  selfNameProp: contactsPage.selfName

                dataModel: contactsNav.searchVisible ? searchModel : contactModel

                listItemComponents: [
                    ListItemComponent {
                        type: ""
                        Container {
                            id: gridCell
                            horizontalAlignment: HorizontalAlignment.Fill
                            verticalAlignment:   VerticalAlignment.Fill
                            layout: DockLayout {}

                            ImageView {
                                imageSource: (ListItemData.localAvatar && ListItemData.localAvatar.length > 0)
                                    ? ListItemData.localAvatar
                                    : "asset:///images/ContactsTab/avatar_default.png"
                                horizontalAlignment: HorizontalAlignment.Fill
                                verticalAlignment:   VerticalAlignment.Fill
                                scalingMethod: ScalingMethod.AspectFill
                            }

                            Container {
                                preferredHeight: ui.du(5)
                                background: Color.create("#99000000")
                                verticalAlignment:   VerticalAlignment.Bottom
                                horizontalAlignment: HorizontalAlignment.Fill
                                layout: DockLayout {}
                                Label {
                                    text: {
                                        var n = ListItemData.name || ListItemData.displayName || "?"
                                        return n.length > 9 ? n.substring(0, 8) + "…" : n
                                    }
                                    textStyle { fontSize: FontSize.XXSmall; color: Color.White }
                                    horizontalAlignment: HorizontalAlignment.Center
                                    verticalAlignment:   VerticalAlignment.Center
                                    multiline: false
                                }
                            }

                            onTouch: {
                                if (event.isUp()) {
                                    var item = ListItemData
                                    var lv   = gridCell.ListItem.view
                                    var page = lv.profileDef.createObject()
                                    if (!page) return
                                    page.contactId      = item.threadId || item.uid || ""
                                    page.contactName    = item.name || item.displayName || "?"
                                    page.avatarPath     = item.localAvatar || ""
                                    page.bgAvatarPath   = item.localBgAvatar || ""
                                    page.avatarUrl      = item.avatar || ""
                                    page.bgAvatarUrl    = item.bgavatar || ""
                                    page.selfName       = lv.selfNameProp
                                    page.navigationPane = lv.navPane
                                    lv.navPane.push(page)
                                }
                            }
                        }
                    }
                ]
            }

            ActivityIndicator {
                id: contactsLoading
                horizontalAlignment: HorizontalAlignment.Center
                verticalAlignment:   VerticalAlignment.Center
                preferredWidth: ui.du(12)
                preferredHeight: ui.du(12)
                running: visible
                visible: false
            }

            Label {
                id: contactsEmpty
                text: "No contacts found"
                visible: false
                horizontalAlignment: HorizontalAlignment.Center
                verticalAlignment:   VerticalAlignment.Center
            }
            }
        }

        attachedObjects: [
            Timer {
                // Hands avatar requests to zService at a steady pace (see lists/avatarhub.js).
                id: contactsAvatarTimer
                interval: 300   // = AvatarHub.TICK_MS
                repeat: true
                onTriggered: {
                    if (AvatarHub.drain("contacts", zService, Date.now()) === 0) contactsAvatarTimer.stop();
                }
            },
            ArrayDataModel { id: contactModel },
            ArrayDataModel { id: searchModel },
            ComponentDefinition {
                id: contactsProfileDef
                source: "asset:///ProfileView.qml"
            },
            Timer {
                id: contactAppendTimer
                interval: 40
                repeat: true
                onTriggered: {
                    contactsNav.appendPending(100)
                    if (contactsNav.pendingPos >= contactsNav.pendingRows.length) contactAppendTimer.stop()
                }
            },
            Connections {
                target: app
                onDarkThemeChanged: { contactsNav.isDark = dark; }
            },
            Connections {
                target: zService

                onFriendsFetchFailed: {
                    contactsLoading.visible = false
                    if (!contactsPage.populated) friendsRetryTimer.restart()
                }

                onFriendsReady: {
                    contactsLoading.visible = false
                    if (contactsPage.populated && contactModel.size() > 0)
                        return
                    contactsPage.populated = true
                    contactsNav.contactCount = friends.length
                    contactsNav.resetPending()
                    contactModel.clear()
                    AvatarHub.invalidate("contacts")
                    contactsNav.pendingRows = friends
                    contactsNav.appendPending(60)
                    if (contactsNav.pendingPos < friends.length) contactAppendTimer.restart()
                    contactsEmpty.visible = (friends.length === 0)
                }

                onAvatarReady: {
                    contactsNav.applyAvatar(threadId, localPath)
                }

                onLoginSuccess: {
                    contactsNav.contactCount = 0
                    contactsNav.resetPending()
                    contactsPage.populated = false
                    contactModel.clear()
                    AvatarHub.invalidate("contacts")
                    contactsNav.friendsRetries = 0
                    contactsLoading.visible = true
                    zService.fetchFriends()
                }
            }
        ]
    }
}
