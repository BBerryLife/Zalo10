import bb.cascades 1.4
import bb.system 1.0
import QtQuick 1.0

// Quick Messages management screen. Replaces the old "Timed Messages"
// under-development placeholder. Opened from the icon next to the chat
// input field in ChatView.qml.
//
// Styled after SmartList10's ItemPageDef.qml: a plain system TitleBar (auto
// themed) with a single action-bar button ("Add New QM"), and a long-press
// ActionSet on each row (Edit / Use in Chat / Delete) instead of any custom
// colored chrome — so everything re-themes for dark mode "for free".
//
// Search box stays pinned to the top, A-Z sorted/grouped ListView
// (GroupDataModel, same pattern as CatTabDef.qml) fills the rest.
Sheet {
    id: quickMsgSheetRoot

    property variant allQuickMessages: []   // full unfiltered list cached from zService
    property string  insertRequestedContent: "" // set by "Use in Chat", read by ChatView.qml's onClosed

    function reload() {
        quickMsgSheetRoot.allQuickMessages = zService.getQuickMessages();
        applyFilter(qmSearchField.text);
    }

    function applyFilter(query) {
        var q = (query || "").toLowerCase().trim();
        var src = quickMsgSheetRoot.allQuickMessages;
        var filtered = [];
        for (var i = 0; i < src.length; i++) {
            var it = src[i];
            var nm = (it.name || "").toLowerCase();
            var ct = (it.content || "").toLowerCase();
            if (q.length === 0 || nm.indexOf(q) !== -1 || ct.indexOf(q) !== -1) {
                filtered.push(it);
            }
        }
        qmModel.clear();
        qmModel.insertList(filtered);

        qmEmpty.visible = (filtered.length === 0);
        qmEmpty.text = (src.length === 0)
            ? "No quick messages yet.\nTap \u201cAdd New QM\u201d above to create one."
            : "No quick messages match your search.";
    }

    onOpened: {
        quickMsgSheetRoot.insertRequestedContent = "";
        qmSearchField.text = "";
        reload();
    }

    Page {
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
            }
        ]

        Container {
            layout: StackLayout {}
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment:   VerticalAlignment.Fill

            // --- Search box, pinned to the top ---
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
                    onTextChanging: { quickMsgSheetRoot.applyFilter(text); }
                }
            }

            // --- A-Z sorted list, fills remaining space ---
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
                        if (indexPath.length === 1) return; // header row tapped
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
                        quickMsgSheetRoot.insertRequestedContent = content;
                        quickMsgSheetRoot.close();
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
                onClosed: { quickMsgSheetRoot.reload(); }
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
                        quickMsgSheetRoot.reload();
                    }
                }
            }
        ]
    }
}
