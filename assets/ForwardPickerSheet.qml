import bb.cascades 1.4
import bb.system 1.0
import QtQuick 1.0

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
        if (!id || id.length === 0) {
            return;
        }
        var arr = forwardSheet.selectedIds.slice();
        var idx = arr.indexOf(id);
        if (idx >= 0) arr.splice(idx, 1);
        else arr.push(id);
        forwardSheet.selectedIds = arr;
        sendAction.enabled = arr.length > 0;
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
                                    // enabled must stay true or Cascades greys out the checkbox. PassThrough lets taps reach the
                                    // ListItem's onTriggered; otherwise the checkbox flips but the selection never updates.
                                    touchPropagationMode: TouchPropagationMode.PassThrough
                                }
                            }
                        }
                    }
                ]

                onTriggered: {
                    var idx = indexPath[0];
                    var item = dataModel.value(idx);
                    if (!item) return;
                    item.checked = !item.checked;
                    pickModel.replace(idx, item);
                    forwardSheet.toggleSelected(item.threadId || "");
                }
            }
        }
    }
}
