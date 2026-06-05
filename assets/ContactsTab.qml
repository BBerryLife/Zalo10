// ContactsTab.qml
import bb.cascades 1.4
import QtQuick 1.0

NavigationPane {
    id: contactsNav
    peekEnabled: false
    
    Page {
        id: contactsPage
        property bool populated: false
        property string selfName: ""
        
        titleBar: TitleBar {
            kind: TitleBarKind.FreeForm
            kindProperties: FreeFormTitleBarKindProperties {
                content: Container {
                    background: Color.create("#2575fc")
                    horizontalAlignment: HorizontalAlignment.Fill
                    verticalAlignment:   VerticalAlignment.Fill
                    layout: DockLayout {}
                    leftPadding: ui.du(2.5)
                    Label {
                        text: "Contacts"
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
            }
        ]
        
        onCreationCompleted: {
            if (zService.loggedIn) {
                zService.fetchFriends()
                contactsLoading.visible = true
            }
        }
        
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
                }
                
                property variant profileDef: contactsProfileDef
                property variant navPane:    contactsNav
                property string  selfNameProp: contactsPage.selfName
                
                dataModel: ArrayDataModel { id: contactModel }
                
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
                    for (var i = 0; i < contactModel.size(); i++) {
                        var d = contactModel.value(i)
                        var tid = d.threadId || d.uid || ""
                        if (threadId.indexOf("bg_") === 0) {
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