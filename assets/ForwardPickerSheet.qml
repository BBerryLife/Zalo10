import bb.cascades 1.4
import bb.system 1.0
import QtQuick 1.0

// ForwardPickerSheet — "Forward" flow for a chat bubble. Two steps, per the
// request:
//   1) A native SystemDialog offering Groups / Chat / Cancel (reusing the
//      exact SystemDialog pattern this codebase's own ConfirmDialog.qml
//      already uses — SystemListDialog's QML multi-select API couldn't be
//      confidently verified against any documented or precedented usage in
//      this codebase, so this deliberately avoids gambling on it for a
//      three-way single choice, which SystemDialog's confirm/cancel/extra-
//      button trio already covers natively).
//   2) A checklist Sheet of every group (or every 1-1 chat) with a Send /
//      Cancel bar — built from plain ListView + ArrayDataModel + a
//      checkmark toggled via the ListView's own onTriggered (kept OUT of
//      the delegate body, same "ListItemComponent is a separate QML scope"
//      constraint ChatView.qml's rowRoot already documents at length —
//      calling back into forwardSheet from inside a CheckBox's own
//      onCheckedChanged there would hit the exact same problem).
// Target lists are the SAME zService.fetchConversations()/fetchFriends()
// calls + onConversationsReady/onFriendsReady signals GroupsTab.qml/
// ChatsTab.qml already use — no new backend listing API needed. Only the
// fields already established there are read: threadId/uid, name/
// displayName, localAvatar/avatar (populated after downloadAvatar() calls
// elsewhere in the app during normal use; a cold app start might show
// blanks until those catch up, same as those two tabs).
Sheet {
    id: forwardSheet

    property string  pendingContent: ""
    property int     pendingMsgType: 0
    property bool    isDark: false
    property bool    pickingGroups: false
    property variant selectedIds: []

    function openFor(content, msgType) {
        forwardSheet.pendingContent = content || "";
        forwardSheet.pendingMsgType = msgType || 0;
        typeChooser.show();
    }

    function loadTargets() {
        pickModel.clear();
        forwardSheet.selectedIds = [];
        sendAction.enabled = false;
        if (forwardSheet.pickingGroups) zService.fetchConversations();
        else zService.fetchFriends();
    }

    function toggleSelected(id) {
        if (!id || id.length === 0) return;
        var arr = forwardSheet.selectedIds.slice();
        var idx = arr.indexOf(id);
        if (idx >= 0) arr.splice(idx, 1);
        else arr.push(id);
        forwardSheet.selectedIds = arr;
        sendAction.enabled = arr.length > 0;
    }

    function doSend() {
        if (forwardSheet.selectedIds.length === 0 || forwardSheet.pendingContent.length === 0) return;
        zService.forwardMessage(forwardSheet.pendingContent, forwardSheet.selectedIds, forwardSheet.pickingGroups);
        forwardSheet.close();
    }

    attachedObjects: [
        SystemDialog {
            id: typeChooser
            title: "Forward message"
            body: "Choose where to forward this message to"
            confirmButton.label: "Groups"
            confirmButton.enabled: true
            cancelButton.label: "Cancel"
            cancelButton.enabled: true
            buttons: [ SystemUiButton { id: chatButton; label: "Chat"; enabled: true } ]
            onFinished: {
                if (result === SystemUiResult.ConfirmButtonSelection) {
                    forwardSheet.pickingGroups = true;
                    forwardSheet.loadTargets();
                    forwardSheet.open();
                } else if (result === SystemUiResult.ButtonSelection) {
                    forwardSheet.pickingGroups = false;
                    forwardSheet.loadTargets();
                    forwardSheet.open();
                }
                // ConfirmButtonSelection via the extra button reports as
                // ButtonSelection per SystemDialog's own result model (see
                // the dialogs sample: RANDOM/RANDOM2 -> "button") — Cancel/
                // TimeOut/anything else: do nothing, dialog just closes.
            }
        },
        Connections {
            target: zService
            onConversationsReady: {
                if (!forwardSheet.pickingGroups) return;
                for (var i = 0; i < threads.length; i++) {
                    if (!threads[i].isGroup) continue;
                    var g = threads[i];
                    pickModel.append({
                        threadId: g.threadId || "", name: g.name || "Unknown Group",
                        localAvatar: g.avatar || "", checked: false
                    });
                }
            }
        },
        Connections {
            target: zService
            onFriendsReady: {
                if (forwardSheet.pickingGroups) return;
                for (var j = 0; j < friends.length; j++) {
                    var f = friends[j];
                    pickModel.append({
                        threadId: f.threadId || f.uid || "", name: f.name || f.displayName || "Unknown",
                        localAvatar: f.avatar || "", checked: false
                    });
                }
            }
        }
    ]

    content: Page {
        titleBar: TitleBar {
            title: forwardSheet.pickingGroups ? "Forward to Groups" : "Forward to Chats"
            dismissAction: ActionItem {
                title: "Cancel"
                onTriggered: { forwardSheet.close(); }
            }
            acceptAction: ActionItem {
                id: sendAction
                title: "Send"
                enabled: false
                onTriggered: { forwardSheet.doSend(); }
            }
        }

        Container {
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment: VerticalAlignment.Fill
            background: forwardSheet.isDark ? Color.create("#1a1a1a") : Color.White

            ListView {
                id: pickList
                layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                dataModel: ArrayDataModel { id: pickModel }
                property bool isDarkProxy: forwardSheet.isDark

                listItemComponents: [
                    ListItemComponent {
                        CustomListItem {
                            dividerVisible: true
                            Container {
                                id: rowRoot
                                background: (ListItemData.checked === true)
                                    ? Color.create("#cfe3fa")
                                    : (ListItem.view.isDarkProxy ? Color.create("#1a1a1a") : Color.White)
                                layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                                horizontalAlignment: HorizontalAlignment.Fill
                                verticalAlignment: VerticalAlignment.Center
                                topPadding: ui.du(1); bottomPadding: ui.du(1)
                                leftPadding: ui.du(1.2); rightPadding: ui.du(1.2)

                                ImageView {
                                    imageSource: (ListItemData.localAvatar && ListItemData.localAvatar.length > 0)
                                                 ? ListItemData.localAvatar : "asset:///images/ChatsTab/blank.png"
                                    preferredWidth: ui.du(5); preferredHeight: ui.du(5)
                                    scalingMethod: ScalingMethod.AspectFill
                                    rightMargin: ui.du(1.2)
                                }
                                Label {
                                    layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                                    text: ListItemData.name || "Unknown"
                                    textStyle { color: ListItem.view.isDarkProxy ? Color.White : Color.Black }
                                }
                                Label {
                                    visible: ListItemData.checked === true
                                    text: "\u2713"
                                    textStyle { color: Color.create("#2575fc"); fontWeight: FontWeight.Bold; fontSize: FontSize.Large }
                                }
                            }
                        }
                    }
                ]

                // Toggling selection lives HERE (ListView's own scope, not
                // inside the delegate body) — see this file's header
                // comment on why. dataModel.replace() takes a plain int
                // index for a flat ArrayDataModel (indexPath[0]), same
                // pattern ChatView.qml's msgModel.replace(j, d) already
                // uses elsewhere in this app.
                onTriggered: {
                    var item = dataModel.data(indexPath);
                    if (!item) return;
                    item.checked = !item.checked;
                    pickModel.replace(indexPath[0], item);
                    forwardSheet.toggleSelected(item.threadId || "");
                }
            }
        }
    }
}
