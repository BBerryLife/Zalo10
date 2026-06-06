import bb.cascades 1.4
import QtQuick 1.0

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
                onTriggered: {}
            }
        ]

        content: Container {
            layout: DockLayout {}
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment: VerticalAlignment.Fill

            ListView {
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment: VerticalAlignment.Fill
                dataModel: ArrayDataModel { id: inviteModel }

                function itemType(data, indexPath) { return "item"; }

                listItemComponents: [
                    ListItemComponent {
                        type: "item"
                        CustomListItem {
                            id: inviteRoot
                            dividerVisible: true
                            property string friendUid: ListItemData.uid || ""

                            Container {
                                layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                                horizontalAlignment: HorizontalAlignment.Fill
                                topPadding: ui.du(1.5)
                                bottomPadding: ui.du(1.5)
                                leftPadding: ui.du(2)
                                rightPadding: ui.du(2)

                                ImageView {
                                    imageSource: ListItemData.localAvatar ? ListItemData.localAvatar : "asset:///images/blank.png"
                                    preferredWidth: ui.du(9)
                                    preferredHeight: ui.du(9)
                                    scalingMethod: ScalingMethod.AspectFill
                                    verticalAlignment: VerticalAlignment.Center
                                    rightMargin: ui.du(2)
                                }

                                Container {
                                    verticalAlignment: VerticalAlignment.Center
                                    layout: StackLayout { orientation: LayoutOrientation.TopToBottom }
                                    layoutProperties: StackLayoutProperties { spaceQuota: 1 }

                                    Label {
                                        text: ListItemData.name || "Unknown User"
                                        textStyle {
                                            base: SystemDefaults.TextStyles.PrimaryText
                                            fontWeight: FontWeight.Bold
                                        }
                                        bottomMargin: ui.du(0.3)
                                    }

                                    Label {
                                        text: ListItemData.msg || "Wants to be your friend"
                                        textStyle {
                                            base: SystemDefaults.TextStyles.SubtitleText
                                            color: Color.DarkGray
                                        }
                                        multiline: false
                                        bottomMargin: ui.du(1)
                                    }

                                    Container {
                                        layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                                        Button {
                                            text: "Accept"
                                            preferredHeight: ui.du(5.5)
                                            preferredWidth: ui.du(18)
                                            rightMargin: ui.du(1.5)
                                            onClicked: {
                                                inviteRoot.ListItem.view.acceptRequest(inviteRoot.friendUid);
                                            }
                                        }
                                        Button {
                                            text: "Decline"
                                            preferredHeight: ui.du(5.5)
                                            preferredWidth: ui.du(18)
                                            onClicked: {
                                                inviteRoot.ListItem.view.declineRequest(inviteRoot.friendUid);
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                ]

                function acceptRequest(uid)  { zService.acceptFriendRequest(uid); }
                function declineRequest(uid) { zService.rejectFriendRequest(uid); }
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
                    invitesLoading.visible = false;
                    inviteModel.clear();
                    for (var i = 0; i < invites.length; i++) {
                        var inv = invites[i];
                        inv.localAvatar = "";
                        inviteModel.append(inv);
                        var url = inv.avatar || "";
                        var tid = inv.uid || "";
                        if (url.length > 0 && tid.length > 0)
                            zService.downloadAvatar(tid, url);
                    }
                    invEmpty.visible = (invites.length === 0);
                }

                onFriendRequestResponded: {
                    if (success) {
                        for (var i = 0; i < inviteModel.size(); i++) {
                            if ((inviteModel.value(i).uid || "") === friendId) {
                                inviteModel.removeAt(i);
                                break;
                            }
                        }
                        invEmpty.visible = (inviteModel.size() === 0);
                    }
                }

                onAvatarReady: {
                    for (var i = 0; i < inviteModel.size(); i++) {
                        var d = inviteModel.value(i);
                        if ((d.uid || "") === threadId) {
                            d.localAvatar = localPath;
                            inviteModel.removeAt(i);
                            inviteModel.insert(i, d);
                            break;
                        }
                    }
                }

                onLoginSuccess: { zService.fetchInvites(); }
            }
        ]
    }
}
