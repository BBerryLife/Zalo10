import bb.cascades 1.4
import bb.system 1.0
import QtQuick 1.0
import "lists/avatarhub.js" as AvatarHub

NavigationPane {
    id: contactsNav
    peekEnabled: false

    property string selfName: ""
    onSelfNameChanged: contactsPage.selfName = selfName

    property bool searchVisible: false
    property string searchText: ""
    property bool refreshCooldown: false

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
    // Avatar requests themselves are paced by the Chats tab (same friend list); this tab only
    // receives avatarReady. Cover images ("bg_<id>") are no longer fetched for every friend at
    // start-up: ProfileView downloads the cover when that profile is opened.
    function applyAvatar(threadId, localPath) {
        var isBg = (threadId.indexOf("bg_") === 0);
        var id   = isBg ? threadId.substring(3) : threadId;
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
        }
    ]

    Page {
        id: contactsPage
        property bool populated: false
        property string selfName: contactsNav.selfName

        titleBar: TitleBar {
            kind: TitleBarKind.FreeForm
            kindProperties: FreeFormTitleBarKindProperties {
                content: Container {
                    background: Color.create("#2575fc")
                    horizontalAlignment: HorizontalAlignment.Fill
                    verticalAlignment:   VerticalAlignment.Fill
                    layout: DockLayout {}
                    leftPadding: ui.du(2.5)
                    rightPadding: ui.du(2.5)
                    Label {
                        text: "Contacts"
                        textStyle {
                            color: Color.White
                            base: SystemDefaults.TextStyles.TitleText
                            fontWeight: FontWeight.Bold
                        }
                        verticalAlignment:   VerticalAlignment.Center
                        horizontalAlignment: HorizontalAlignment.Left
                        visible: !contactsNav.searchVisible
                    }
                    Container {
                        visible: contactsNav.searchVisible
                        horizontalAlignment: HorizontalAlignment.Fill
                        verticalAlignment: VerticalAlignment.Center
                        layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                        TextField {
                            id: contactsSearchField
                            hintText: "Search contacts..."
                            verticalAlignment: VerticalAlignment.Center
                            textStyle { color: Color.White }
                            layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                            onTextChanging: {
                                contactsNav.searchText = text;
                                contactsNav.filterList();
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
                                contactsSearchField.text = "";
                                contactsNav.searchText = "";
                                contactsNav.searchVisible = false;
                                contactsNav.filterList();
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
                    contactsNav.searchVisible = !contactsNav.searchVisible;
                    if (!contactsNav.searchVisible) {
                        contactsNav.searchText = "";
                        contactsNav.filterList();
                    }
                }
            }
        ]

        actions: [
            ActionItem {
                id: contactsRefreshAction
                title: contactsNav.refreshCooldown ? "Please wait..." : "Refresh"
                enabled: !contactsNav.refreshCooldown
                imageSource: "asset:///images/ContactsTab/ic_sync.png"
                ActionBar.placement: ActionBarPlacement.OnBar
                onTriggered: {
                    if (contactsNav.refreshCooldown) return;
                    contactsNav.refreshCooldown = true;
                    refreshCooldownTimer.restart();
                    contactsPage.populated = false
                    contactModel.clear()
                    AvatarHub.invalidate("contacts")
                    zService.fetchFriends()
                    contactsLoading.visible = true
                }
            },
            ActionItem {
                title: "Search"
                imageSource: "asset:///images/ContactsTab/action_icon_search.png"
                ActionBar.placement: ActionBarPlacement.InOverflow
                onTriggered: {
                    contactsNav.searchVisible = !contactsNav.searchVisible;
                    if (!contactsNav.searchVisible) {
                        contactsNav.searchText = "";
                        contactsNav.filterList();
                    }
                }
            },
            ActionItem {
                title: "Add Contact"
                imageSource: "asset:///images/ContactsTab/ic_add_contact.png"
                ActionBar.placement: ActionBarPlacement.InOverflow
                onTriggered: { addContactDialog.show() }
            }
        ]

        Container {
            layout: DockLayout {}
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment:   VerticalAlignment.Fill

            ListView {
                id: contactsGrid
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment:   VerticalAlignment.Fill

                layout: GridListLayout {
                    columnCount: 4
                    headerMode: ListHeaderMode.None
                    horizontalCellSpacing: ui.du(0.1)
                    verticalCellSpacing:   ui.du(0.1)
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
                                    : "asset:///images/ChatView/default_caller.png"
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

        attachedObjects: [
            ArrayDataModel { id: contactModel },
            SystemDialog {
                id: addContactDialog
                title: "Add Contact"
                body: "This feature is still under development."
                confirmButton.label: "OK"
                cancelButton.label: ""
                cancelButton.enabled: false
            },
            ArrayDataModel { id: searchModel },
            ComponentDefinition {
                id: contactsProfileDef
                source: "asset:///ProfileView.qml"
            },
            Connections {
                target: zService

                onFriendsFetchFailed: {
                    // Lite: the Chats tab retries the fetch; do not leave this spinner running forever.
                    contactsLoading.visible = false
                }

                onFriendsReady: {
                    contactsLoading.visible = false
                    if (contactsPage.populated && contactModel.size() > 0)
                        return
                    contactsPage.populated = true
                    contactModel.clear()
                    AvatarHub.invalidate("contacts")
                    for (var i = 0; i < friends.length; i++)
                        contactModel.append(friends[i])
                    contactsEmpty.visible = (friends.length === 0)
                }

                onAvatarReady: {
                    contactsNav.applyAvatar(threadId, localPath)
                }

                onLoginSuccess: {
                    contactsPage.populated = false
                    contactModel.clear()
                    AvatarHub.invalidate("contacts")
                }
            }
        ]
    }
}
