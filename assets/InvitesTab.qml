import bb.cascades 1.4
import QtQuick 1.0
import "lists"

NavigationPane {
    id: invitesNav
    signal myProfileRequested()
    peekEnabled: false

    property string selfName: ""
    property string selfAvatar: ""

    property bool searchVisible: false
    property string searchText: ""
    property variant allInvites: []
    // Number of pending invites, shown as the Invites tab subtitle ("N New Invites") by main.qml.
    property int inviteCount: 0

    function filterList() {
        var q = invitesNav.searchText.toLowerCase().trim();
        searchModel.clear();
        for (var i = 0; i < inviteModel.size(); i++) {
            var inv = inviteModel.value(i);
            if (q.length === 0) {
                searchModel.append(inv);
            } else {
                var name = (inv.name || inv.displayName || "").toLowerCase();
                if (name.indexOf(q) !== -1) searchModel.append(inv);
            }
        }
        invEmpty.visible = (!invitesNav.searchVisible && inviteModel.size() === 0);
    }

    Page {
        titleBar: ProfileTitleBar {
            id: invitesBar
            selfName:   invitesNav.selfName
            selfAvatar: invitesNav.selfAvatar
            profileHandled: true
            onProfileClicked: invitesNav.myProfileRequested()
            searchHint: "Search requests..."
            searchMode: invitesNav.searchVisible
            onSearchChanged: { invitesNav.searchText = text; invitesNav.filterList(); }
            onSearchClosed: {
                invitesNav.searchText = "";
                invitesNav.searchVisible = false;
                invitesNav.filterList();
            }
        }

        shortcuts: [
            Shortcut {
                key: "s"
                onTriggered: {
                    invitesNav.searchVisible = !invitesNav.searchVisible;
                    if (!invitesNav.searchVisible) {
                        invitesBar.clearSearch();
                        invitesNav.searchText = "";
                        invitesNav.filterList();
                    } else {
                        invitesBar.focusSearch();
                    }
                }
            }
        ]

        actions: [
            ActionItem {
                title: qsTr("Refresh")
                imageSource: "asset:///images/InvitesTab/ic_sync.png"
                ActionBar.placement: ActionBarPlacement.InOverflow
                onTriggered: {
                    inviteModel.clear();
                    zService.fetchInvites();
                    invitesLoading.visible = true;
                }
            },
            ActionItem {
                title: qsTr("Search")
                imageSource: "asset:///images/InvitesTab/action_icon_search.png"
                ActionBar.placement: ActionBarPlacement.InOverflow
                onTriggered: {
                    invitesNav.searchVisible = !invitesNav.searchVisible;
                    if (!invitesNav.searchVisible) {
                        invitesBar.clearSearch();
                        invitesNav.searchText = "";
                        invitesNav.filterList();
                    } else {
                        invitesBar.focusSearch();
                    }
                }
            },
            ActionItem {
                title: qsTr("Accept All")
                imageSource: "asset:///images/InvitesTab/ai_add_task.png"
                ActionBar.placement: ActionBarPlacement.InOverflow
                onTriggered: {
                    for (var i = 0; i < inviteModel.size(); i++) {
                        var uid = inviteModel.value(i).uid || "";
                        if (uid.length > 0) zService.acceptFriendRequest(uid);
                    }
                    inviteModel.clear();
                    invEmpty.visible = true;
                }
            }
        ]

        content: Container {
            layout: DockLayout {}
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment: VerticalAlignment.Fill

            FocusSink {}

            ListView {
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment: VerticalAlignment.Fill
                dataModel: invitesNav.searchVisible ? searchModel : inviteModel

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
                                    imageSource: ListItemData.localAvatar ? ListItemData.localAvatar : "asset:///images/ChatView/default_caller.png"
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
                                        text: ListItemData.name || qsTr("Unknown User")
                                        textStyle {
                                            base: SystemDefaults.TextStyles.PrimaryText
                                            fontWeight: FontWeight.Bold
                                        }
                                        bottomMargin: ui.du(0.3)
                                    }

                                    Label {
                                        text: ListItemData.msg || qsTr("Wants to be your friend")
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
                                            text: qsTr("Accept")
                                            preferredHeight: ui.du(5.5)
                                            preferredWidth: ui.du(18)
                                            rightMargin: ui.du(1.5)
                                            onClicked: {
                                                inviteRoot.ListItem.view.acceptRequest(inviteRoot.friendUid);
                                            }
                                        }
                                        Button {
                                            text: qsTr("Decline")
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
                text: qsTr("No friend requests")
                visible: false
                horizontalAlignment: HorizontalAlignment.Center
                verticalAlignment: VerticalAlignment.Center
            }
        }

        attachedObjects: [
            ArrayDataModel {
                id: inviteModel
                onItemAdded:    { invitesNav.inviteCount = inviteModel.size(); }
                onItemRemoved:  { invitesNav.inviteCount = inviteModel.size(); }
                onItemsChanged: { invitesNav.inviteCount = inviteModel.size(); }
            },
            ArrayDataModel { id: searchModel },
            Connections {
                target: zService

                onInvitesReady: {
                    invitesLoading.visible = false;
                    inviteModel.clear();
                    var arr = [];
                    for (var i = 0; i < invites.length; i++) {
                        var inv = invites[i];
                        inv.localAvatar = "";
                        arr.push(inv);
                        inviteModel.append(inv);
                        var url = inv.avatar || "";
                        var tid = inv.uid || "";
                        if (url.length > 0 && tid.length > 0)
                            zService.downloadAvatar(tid, url);
                    }
                    invitesNav.allInvites = arr;
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
                    if (invitesNav.searchVisible) {
                        for (var j = 0; j < searchModel.size(); j++) {
                            var sd = searchModel.value(j);
                            if ((sd.uid || "") === threadId) {
                                sd.localAvatar = localPath;
                                searchModel.replace(j, sd);
                                break;
                            }
                        }
                    }
                }

                onLoginSuccess: { zService.fetchInvites(); }
            }
        ]
    }
}
