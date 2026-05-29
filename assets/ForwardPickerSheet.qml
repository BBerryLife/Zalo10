import bb.cascades 1.4
import bb.system 1.0
import QtQuick 1.0

// "Forward" flow for a chat bubble, in two steps:
//   1) A native SystemDialog offering Groups / Chat / Cancel
//   2) A checklist Sheet of every group (or every 1-1 chat) with a Send/Cancel bar,
//      built from ListView + ArrayDataModel; selection toggling lives in the
//      ListView's onTriggered, not the delegate (see rowRoot in ChatView.qml for why)
// Reuses the same zService.fetchConversations()/fetchFriends() calls GroupsTab.qml
// and ChatsTab.qml already use for their lists
Sheet {
    id: forwardSheet

    property string  pendingContent: ""
    property int     pendingMsgType: 0
    property string  pendingOrigMsgId: ""
    property string  pendingOrigTs: ""
    property bool    isDark: false
    property bool    pickingGroups: false
    property variant selectedIds: []

    function openFor(content, msgType, origMsgId, origTs) {
        forwardSheet.pendingContent = content || "";
        forwardSheet.pendingMsgType = msgType || 0;
        forwardSheet.pendingOrigMsgId = origMsgId || "";
        forwardSheet.pendingOrigTs = origTs || "";
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
        console.log("[Zalo QML] ForwardPickerSheet.toggleSelected called with id=\"" + id + "\"");
        if (!id || id.length === 0) {
            console.log("[Zalo QML] ForwardPickerSheet.toggleSelected: EMPTY id, returning early (Send will NOT be enabled)");
            return;
        }
        var arr = forwardSheet.selectedIds.slice();
        var idx = arr.indexOf(id);
        if (idx >= 0) arr.splice(idx, 1);
        else arr.push(id);
        forwardSheet.selectedIds = arr;
        sendAction.enabled = arr.length > 0;
        console.log("[Zalo QML] ForwardPickerSheet.toggleSelected: selectedIds.length=" + arr.length + " sendAction.enabled=" + sendAction.enabled);
    }

    function doSend() {
        if (forwardSheet.selectedIds.length === 0 || forwardSheet.pendingContent.length === 0) return;
        zService.forwardMessage(forwardSheet.pendingContent, forwardSheet.selectedIds, forwardSheet.pickingGroups,
                                 forwardSheet.pendingOrigMsgId, forwardSheet.pendingOrigTs);
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
                // The extra button reports as ButtonSelection; Cancel/TimeOut/anything
                // else just closes the dialog
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
                            id: rowRoot
                            dividerVisible: true
                            Container {
                                background: rowRoot.ListItem.view.isDarkProxy ? Color.create("#1a1a1a") : Color.White
                                layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                                horizontalAlignment: HorizontalAlignment.Fill
                                verticalAlignment: VerticalAlignment.Center
                                topPadding: ui.du(1); bottomPadding: ui.du(1)
                                leftPadding: ui.du(1.2); rightPadding: ui.du(1.2)

                                Label {
                                    layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                                    text: ListItemData.name || "Unknown"
                                    textStyle { color: rowRoot.ListItem.view.isDarkProxy ? Color.White : Color.Black; fontSize: FontSize.Medium }
                                }
                                CheckBox {
                                    checked: ListItemData.checked === true
                                    // enabled must stay true or Cascades greys the checkbox out
                                    //
                                    // PassThrough so a tap directly on the checkbox still reaches
                                    // the ListItem's onTriggered below — otherwise the checkbox
                                    // flips visually but selection never updates and Send stays disabled
                                    touchPropagationMode: TouchPropagationMode.PassThrough
                                }
                            }
                        }
                    }
                ]

                // Toggling selection lives here in the ListView, not the delegate body
                // dataModel.replace() takes a plain int index for a flat ArrayDataModel
                onTriggered: {
                    var idx = indexPath[0];
                    var item = dataModel.value(idx);
                    if (!item) { console.log("[Zalo QML] ForwardPickerSheet onTriggered: no item at idx=" + idx); return; }
                    item.checked = !item.checked;
                    pickModel.replace(idx, item);
                    console.log("[Zalo QML] ForwardPickerSheet onTriggered: idx=" + idx + " name=\"" + item.name + "\" threadId=\"" + item.threadId + "\" checked=" + item.checked);
                    forwardSheet.toggleSelected(item.threadId || "");
                }
            }
        }
    }
}
