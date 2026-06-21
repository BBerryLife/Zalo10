import bb.cascades 1.4
import bb.system 1.0
import QtQuick 1.0

// Quick Messages management screen — now a Page pushed into chatsNav
// (was a Sheet) so it gets the native NavigationPane back chevron.
//
// "Use in Chat": sets useInChatRequested = true and insertRequestedContent,
// ChatsTab.qml watches onUseInChatRequestedChanged, pops this page,
// then injects content into the ChatView below.
Page {
    id: quickMsgPage

    property variant allQuickMessages: []
    property string  insertRequestedContent: ""
    property bool    useInChatRequested: false

    function reload() {
        quickMsgPage.allQuickMessages = zService.getQuickMessages();
        applyFilter(qmSearchField.text);
    }

    function applyFilter(query) {
        var q = (query || "").toLowerCase().trim();
        var src = quickMsgPage.allQuickMessages;
        var filtered = [];
        for (var i = 0; i < src.length; i++) {
            var it = src[i];
            var nm = (it.name || "").toLowerCase();
            var ct = (it.content || "").toLowerCase();
            if (q.length === 0 || nm.indexOf(q) !== -1 || ct.indexOf(q) !== -1)
                filtered.push(it);
        }
        qmModel.clear();
        qmModel.insertList(filtered);
        qmEmpty.visible = (filtered.length === 0);
        qmEmpty.text = (src.length === 0)
            ? "No quick messages yet.\nTap \u201cAdd New QM\u201d above to create one."
            : "No quick messages match your search.";
    }

    onCreationCompleted: {
        quickMsgPage.insertRequestedContent = "";
        quickMsgPage.useInChatRequested = false;
        qmSearchField.text = "";
        reload();
    }

    titleBar: TitleBar {
        scrollBehavior: TitleBarScrollBehavior.Sticky
        kind: TitleBarKind.FreeForm
        kindProperties: FreeFormTitleBarKindProperties {
            content: Container {
                background: Color.create("#2575fc")
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment:   VerticalAlignment.Fill
                layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                leftPadding: ui.du(2)

                Label {
                    text: "Quick Messages"
                    layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                    horizontalAlignment: HorizontalAlignment.Fill
                    verticalAlignment: VerticalAlignment.Center
                    textStyle { color: Color.White; base: SystemDefaults.TextStyles.TitleText; fontWeight: FontWeight.Bold }
                }
            }
        }
    }

    actions: [
        ActionItem {
            title: "Add New QM"
            imageSource: "asset:///images/ChatView/add.png"
            ActionBar.placement: ActionBarPlacement.OnBar
            onTriggered: {
                qmEditSheet.editId = -1;
                qmEditSheet.resetFields();
                qmEditSheet.open();
            }
        },
        ActionItem {
            title: "Get from Zalo"
            imageSource: "asset:///images/ChatView/ic_replace_message.png"
            ActionBar.placement: ActionBarPlacement.InOverflow
            onTriggered: {
                qmFetchProgressToast.body = "Fetching quick messages from Zalo…";
                qmFetchProgressToast.show();
                zService.fetchServerQuickMessages();
            }
        }
    ]

    Container {
        layout: StackLayout {}
        horizontalAlignment: HorizontalAlignment.Fill
        verticalAlignment:   VerticalAlignment.Fill

        Container {
            horizontalAlignment: HorizontalAlignment.Fill
            leftPadding: ui.du(2); rightPadding: ui.du(2)
            topPadding: ui.du(1); bottomPadding: ui.du(1)

            TextField {
                id: qmSearchField
                hintText: "Search quick messages"
                horizontalAlignment: HorizontalAlignment.Fill
                input {
                    flags: TextInputFlag.AutoCapitalizationOff | TextInputFlag.SpellCheckOff | TextInputFlag.PredictionOff
                }
                onTextChanging: { quickMsgPage.applyFilter(text); }
            }
        }

        Container {
            layout: DockLayout {}
            horizontalAlignment: HorizontalAlignment.Fill
            layoutProperties: StackLayoutProperties { spaceQuota: 1 }

            ListView {
                id: qmListView
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment:   VerticalAlignment.Fill

                dataModel: GroupDataModel {
                    id: qmModel
                    sortingKeys: ["name"]
                    grouping: ItemGrouping.ByFirstChar
                }

                function itemType(data, indexPath) {
                    return (indexPath.length === 1) ? "header" : "item";
                }

                listItemComponents: [
                    ListItemComponent {
                        type: "header"
                        Header { title: ListItemData.toString().toUpperCase() }
                    },
                    ListItemComponent {
                        type: "item"
                        CustomListItem {
                            id: qmRow
                            highlightAppearance: HighlightAppearance.Full
                            dividerVisible: true

                            contextActions: [
                                ActionSet {
                                    title: "/" + ListItemData.name
                                    ActionItem {
                                        title: "Edit"
                                        imageSource: "asset:///images/ChatView/ic_edit.png"
                                        onTriggered: { qmRow.ListItem.view.doEdit(ListItemData.id); }
                                    }
                                    ActionItem {
                                        title: "Use in Chat"
                                        imageSource: "asset:///images/ChatView/ic_replace_message.png"
                                        onTriggered: { qmRow.ListItem.view.doUseInChat(ListItemData.content); }
                                    }
                                    DeleteActionItem {
                                        title: "Delete"
                                        onTriggered: { qmRow.ListItem.view.doDelete(ListItemData.id, ListItemData.name); }
                                    }
                                }
                            ]

                            Container {
                                layout: StackLayout { orientation: LayoutOrientation.TopToBottom }
                                horizontalAlignment: HorizontalAlignment.Fill
                                leftPadding: ui.du(2.4); rightPadding: ui.du(2.4)
                                topPadding: ui.du(1.2); bottomPadding: ui.du(1.2)

                                Label {
                                    text: "/" + ListItemData.name
                                    textStyle {
                                        base: SystemDefaults.TextStyles.TitleText
                                        fontWeight: FontWeight.Bold
                                    }
                                    multiline: false
                                }
                                Label {
                                    text: ListItemData.content
                                    textStyle {
                                        base: SystemDefaults.TextStyles.SubtitleText
                                        color: Color.DarkGray
                                    }
                                    multiline: false
                                    topMargin: ui.du(0.3)
                                }
                            }
                        }
                    }
                ]

                onTriggered: {
                    if (indexPath.length === 1) return;
                    var item = dataModel.data(indexPath);
                    if (item === null || item === undefined || item.id === undefined) return;
                    qmListView.doEdit(item.id);
                }

                function doEdit(qid) {
                    qmEditSheet.editId = qid;
                    qmEditSheet.prefillFromId(qid);
                    qmEditSheet.open();
                }

                function doUseInChat(content) {
                    quickMsgPage.insertRequestedContent = content;
                    quickMsgPage.useInChatRequested = true;
                }

                function doDelete(qid, qname) {
                    qmDeleteDialog.targetId = qid;
                    qmDeleteDialog.body = "Delete the quick message \"/" + qname + "\"?";
                    qmDeleteDialog.show();
                }
            }

            Label {
                id: qmEmpty
                visible: false
                horizontalAlignment: HorizontalAlignment.Center
                verticalAlignment:   VerticalAlignment.Center
                multiline: true
                textStyle { color: Color.Gray; textAlign: TextAlign.Center }
            }
        }
    }

    attachedObjects: [
        QuickMessageEditSheet {
            id: qmEditSheet
            onClosed: { quickMsgPage.reload(); }
        },
        SystemDialog {
            id: qmDeleteDialog
            property int targetId: -1
            title: "Delete Quick Message"
            confirmButton.label: "Delete"
            cancelButton.label: "Cancel"
            onFinished: {
                if (result === SystemUiResult.ConfirmButtonSelection) {
                    zService.deleteQuickMessage(qmDeleteDialog.targetId);
                    quickMsgPage.reload();
                }
            }
        },
        SystemToast {
            id: qmFetchProgressToast
            body: ""
        },
        SystemToast {
            id: qmFetchDoneToast
            body: ""
        },
        Connections {
            target: zService
            onServerQuickMessagesReady: {
                qmFetchProgressToast.cancel();
                if (error.length > 0) {
                    qmFetchDoneToast.body = "Could not fetch from Zalo: " + error;
                } else {
                    qmFetchDoneToast.body = "Got " + imported + " new quick message(s) from Zalo"
                        + (skipped > 0 ? " (" + skipped + " already had)" : "");
                    quickMsgPage.reload();
                }
                qmFetchDoneToast.show();
            }
        }
    ]
}
