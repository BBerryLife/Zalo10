// ContactsTab.qml
import bb.cascades 1.4
import QtQuick 1.0

NavigationPane {
    id: contactsNav
    peekEnabled: false

    Page {
        id: contactsPage
        property string selfName: ""   // set từ main.qml sau khi login

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
                    contactModel.clear()
                    zService.fetchFriends()
                    contactsLoading.visible = true
                }
            },
            ActionItem {
                title: "Add Contact"
                imageSource: "asset:///images/ic_add_friend.png"
                ActionBar.placement: ActionBarPlacement.InOverflow
                onTriggered: {
                    // TODO: mở màn hình tìm kiếm bạn bè
                }
            }
        ]

        Container {
            layout: DockLayout {}
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment:   VerticalAlignment.Fill

            ListView {
                id: contactListView
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment:   VerticalAlignment.Fill

                layout: GridListLayout {
                    columnCount: 4
                    cellAspectRatio: 1.0
                }

                property variant profileDef: contactsProfileDef
                property variant navPane: contactsNav
                property string  selfNameProp: contactsPage.selfName

                dataModel: ArrayDataModel { id: contactModel }

                function itemType(data, indexPath) {
                    return "gridItem";
                }

                listItemComponents: [
                    ListItemComponent {
                        type: "gridItem"
                        Container {
                            id: gridCell
                            preferredWidth:  ui.du(20)
                            preferredHeight: ui.du(20)
                            horizontalAlignment: HorizontalAlignment.Fill
                            verticalAlignment:   VerticalAlignment.Fill
                            layout: DockLayout {}

                            ImageView {
                                // Chỉ dùng file:// — không dùng https:// thô
                                imageSource: {
                                    var p = ListItemData.localAvatar || "";
                                    return (p.indexOf("file://") === 0)
                                        ? p : "asset:///images/blank.png";
                                }
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
                                        var n = ListItemData.name || "?";
                                        return n.length > 9 ? n.substring(0, 8) + "…" : n;
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
                                    page.contactName    = item.name || "?"
                                    page.avatarPath     = item.localAvatar || ""
                                    page.bgAvatarPath   = item.localBgAvatar || ""
                                    page.avatarUrl      = item.avatar || ""
                                    page.bgAvatarUrl    = item.bgavatar || ""
                                    page.selfName       = lv.selfNameProp
                                    page.navigationPane = lv.navPane  // set explicit ref
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
                preferredWidth: ui.du(12); preferredHeight: ui.du(12)
                running: visible; visible: false
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
                    if (friends.length === 0) return

                    // Rebuild model
                    contactModel.clear()
                    for (var i = 0; i < friends.length; i++)
                        contactModel.append(friends[i])

                    // Trigger download avatar cho những item chưa có localAvatar
                    for (var j = 0; j < friends.length; j++) {
                        var f = friends[j]
                        var tid = f.threadId || f.uid || ""
                        var url = f.avatar || ""
                        if (tid !== "" && url !== "" && (f.localAvatar || "") === "")
                            zService.downloadAvatar(tid, url)
                    }
                }

                onAvatarReady: {
                    for (var i = 0; i < contactModel.size(); i++) {
                        var d = contactModel.value(i)
                        if ((d.threadId || d.uid || "") === threadId) {
                            d.localAvatar = localPath
                            contactModel.replace(i, d)
                            break
                        }
                    }
                }

                onLoginSuccess: {
                    contactModel.clear()
                    zService.fetchFriends()
                    contactsLoading.visible = true
                }
            }
        ]
    }
}
