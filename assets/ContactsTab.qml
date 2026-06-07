// ContactsTab.qml
import bb.cascades 1.4
import QtQuick 1.0

NavigationPane {
    id: contactsNav
    peekEnabled: false

    // Exposed so main.qml can set it via contactsTabContent.selfName = ...
    property string selfName: ""
    onSelfNameChanged: contactsPage.selfName = selfName

    property bool searchVisible: false
    property string searchText: ""

    function filterList() {
        var q = contactsNav.searchText.toLowerCase().trim();
        searchModel.clear();
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
                title: "Refresh"
                imageSource: "asset:///images/ic_sync.png"
                ActionBar.placement: ActionBarPlacement.OnBar
                onTriggered: {
                    contactsPage.populated = false
                    contactModel.clear()
                    zService.fetchFriends()
                    contactsLoading.visible = true
                }
            },
            ActionItem {
                title: "Add Contact"
                imageSource: "asset:///images/ic_add_contact.png"
                ActionBar.placement: ActionBarPlacement.InOverflow
                onTriggered: {}
            }
        ]
        
        onCreationCompleted: {
            // Fetch được trigger bởi onLoginSuccess signal từ C++ — không fetch ở đây
        }
        
        Container {
            layout: DockLayout {}
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment:   VerticalAlignment.Fill
            
            ArrayDataModel { id: searchModel }
            ArrayDataModel { id: contactModel }
            ListView {
                id: contactsGrid
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment:   VerticalAlignment.Fill
                
                layout: GridListLayout {
                    columnCount: 4
                    headerMode: ListHeaderMode.None
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
                                : "asset:///images/blank.png"
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
            ComponentDefinition {
                id: contactsProfileDef
                source: "asset:///ProfileView.qml"
            },
            Connections {
                target: zService
                
                onFriendsReady: {
                    contactsLoading.visible = false
                    if (contactsPage.populated && contactModel.size() > 0)
                        return
                    contactsPage.populated = true
                    contactModel.clear()
                    for (var i = 0; i < friends.length; i++) {
                        var f = friends[i]
                        contactModel.append(f)
                        var tid = f.threadId || f.uid || ""
                        if (tid.length > 0) {
                            if ((f.avatar || "").length > 0 && (!f.localAvatar || f.localAvatar.length === 0))
                                zService.downloadAvatar(tid, f.avatar)
                            if ((f.bgavatar || "").length > 0 && (!f.localBgAvatar || f.localBgAvatar.length === 0))
                                zService.downloadAvatar("bg_" + tid, f.bgavatar)
                        }
                    }
                    contactsEmpty.visible = (friends.length === 0)
                }
                
                onAvatarReady: {
                    var isBg = (threadId.indexOf("bg_") === 0)
                    // Update visible model
                    for (var i = 0; i < contactModel.size(); i++) {
                        var d = contactModel.value(i)
                        var tid = d.threadId || d.uid || ""
                        if (isBg) {
                            if (("bg_" + tid) === threadId) {
                                d.localBgAvatar = localPath
                                contactModel.replace(i, d)
                                break
                            }
                        } else {
                            if (tid === threadId) {
                                d.localAvatar = localPath
                                contactModel.replace(i, d)
                                break
                            }
                        }
                    }
                    // Update searchModel too if search is active
                    if (contactsNav.searchVisible) {
                        for (var j = 0; j < searchModel.size(); j++) {
                            var sd = searchModel.value(j)
                            var stid = sd.threadId || sd.uid || ""
                            if (isBg) {
                                if (("bg_" + stid) === threadId) {
                                    sd.localBgAvatar = localPath
                                    searchModel.replace(j, sd)
                                    break
                                }
                            } else {
                                if (stid === threadId) {
                                    sd.localAvatar = localPath
                                    searchModel.replace(j, sd)
                                    break
                                }
                            }
                        }
                    }
                }
                
                onLoginSuccess: {
                    contactsPage.populated = false
                    contactModel.clear()
                    zService.fetchFriends()
                    contactsLoading.visible = true
                }
            }
        ]
    }
}