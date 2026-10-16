import bb.cascades 1.4
import bb.system 1.0
import QtQuick 1.0

Page {
    id: groupBoardPage

    property string groupId: ""
    property string groupName: ""
    property variant allItems: []
    property bool    loading: true
    property string  loadError: ""
    property variant groupsNavRef: null

    property string scrollToMsgId: ""

    function reload() {
        if (groupBoardPage.groupId.length === 0) return;
        groupBoardPage.loading = true;
        groupBoardPage.loadError = "";
        zService.fetchGroupBoard(groupBoardPage.groupId, 1, 50);
    }

    function applyFilter() {
        var tab = tabStrip.selectedIndex;
        var src = groupBoardPage.allItems;
        var filtered = [];
        for (var i = 0; i < src.length; i++) {
            var it = src[i];
            if (tab === 0) { filtered.push(it); continue; }
            if (tab === 1 && it.boardType === "pin")  { filtered.push(it); continue; }
            if (tab === 2 && it.boardType === "note") { filtered.push(it); continue; }
            if (tab === 3 && it.boardType === "poll") { filtered.push(it); continue; }
        }
        filtered.sort(function(a, b) {
            var ta = a.createTime || 0, tb = b.createTime || 0;
            return tb - ta;
        });
        boardModel.clear();
        for (var bi = 0; bi < filtered.length; bi++) boardModel.append(filtered[bi]);
        boardEmpty.visible = (filtered.length === 0 && !groupBoardPage.loading);
        boardEmpty.text = (src.length === 0 && !groupBoardPage.loading)
            ? "Nothing pinned, noted, or polled yet.\nUse the buttons below to create one."
            : "No items in this tab.";
    }

    function creatorLabel(creatorId) {
        if (creatorId && creatorId.length > 0 && creatorId === zService.selfUid) return "Me";
        if (!creatorId || creatorId.length === 0) return "Unknown";
        var memName = zService.memberDisplayName(creatorId);
        return (memName && memName.length > 0) ? memName : ("User " + creatorId.slice(-4));
    }

    function doVoteOption(pollId, optionId, allowMulti, options) {
        if (!pollId || pollId.length === 0 || !options) return;
        var newIds = [];
        if (allowMulti) {
            var alreadyVoted = false;
            for (var i = 0; i < options.length; i++) {
                var isThisOne = (options[i].optionId === optionId);
                var wasVoted = !!options[i].voted;
                if (isThisOne) alreadyVoted = wasVoted;
            }
            for (var j = 0; j < options.length; j++) {
                var thisId = options[j].optionId;
                var voted = !!options[j].voted;
                if (thisId === optionId) {
                    if (!alreadyVoted) newIds.push(thisId);
                } else if (voted) {
                    newIds.push(thisId);
                }
            }
        } else {
            var currentlyVotedHere = false;
            for (var k = 0; k < options.length; k++) {
                if (options[k].optionId === optionId && options[k].voted) currentlyVotedHere = true;
            }
            if (!currentlyVotedHere) newIds = [optionId];
        }
        zService.voteGroupPoll(groupBoardPage.groupId, pollId, newIds);
    }

    // Applies a fresh options[] array to the poll row and mirrors it into allItems so tab switches
    // don't show stale data.
    function applyVoteUpdate(pollId, updatedOptions) {
        for (var i = 0; i < groupBoardPage.allItems.length; i++) {
            if (groupBoardPage.allItems[i].boardType === "poll" && groupBoardPage.allItems[i].id === pollId) {
                var item = groupBoardPage.allItems[i];
                item.options = updatedOptions;
                var totalVotes = 0;
                for (var j = 0; j < updatedOptions.length; j++) totalVotes += (updatedOptions[j].votes || 0);
                // numVote is the voter count; votePollDone doesn't return it, so leave it until the next full fetch.
                groupBoardPage.allItems[i] = item;
                break;
            }
        }
        groupBoardPage.applyFilter();
    }

    onCreationCompleted: { reload(); }

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
                    text: "Group Board"
                    layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                    horizontalAlignment: HorizontalAlignment.Fill
                    verticalAlignment: VerticalAlignment.Center
                    textStyle { color: Color.White; base: SystemDefaults.TextStyles.TitleText; fontWeight: FontWeight.Bold }
                }
            }
        }
    }

    Container {
        horizontalAlignment: HorizontalAlignment.Fill
        verticalAlignment:   VerticalAlignment.Fill
        layout: StackLayout { orientation: LayoutOrientation.TopToBottom }

        Container {
            horizontalAlignment: HorizontalAlignment.Fill
            SegmentedControl {
                id: tabStrip
                horizontalAlignment: HorizontalAlignment.Fill
                topMargin: ui.du(0.5); bottomMargin: ui.du(0.5)
                leftMargin: ui.du(1); rightMargin: ui.du(1)
                Option { text: "All" }
                Option { text: "Pinned Message" }
                Option { text: "Note" }
                Option { text: "Poll" }
                onSelectedIndexChanged: { groupBoardPage.applyFilter(); }
            }
        }

        Container {
            layout: DockLayout {}
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment:   VerticalAlignment.Fill
            layoutProperties: StackLayoutProperties { spaceQuota: 1 }

            ListView {
                id: boardListView
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment:   VerticalAlignment.Fill

                dataModel: ArrayDataModel { id: boardModel }

                listItemComponents: [
                    ListItemComponent {
                        type: ""
                    CustomListItem {
                        id: boardRow
                        highlightAppearance: HighlightAppearance.None
                        dividerVisible: true

                        Container {
                            horizontalAlignment: HorizontalAlignment.Fill
                            leftPadding: ui.du(1.0); rightPadding: ui.du(1.0)
                            topPadding: ui.du(1.2); bottomPadding: ui.du(1.2)
                            layout: StackLayout { orientation: LayoutOrientation.TopToBottom }

                            Container {
                                horizontalAlignment: HorizontalAlignment.Fill
                                layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                                bottomMargin: ui.du(0.6)

                                ImageView {
                                    verticalAlignment: VerticalAlignment.Center
                                    preferredWidth: ui.du(4); preferredHeight: ui.du(4)
                                    rightMargin: ui.du(1)
                                    imageSource: ListItemData.boardType === "poll" ? "asset:///images/ChatView/ic_list.png"
                                               : ListItemData.boardType === "note" ? "asset:///images/ChatView/ic_sb_notes.png"
                                               : "asset:///images/ChatView/ic_textmessage.png"
                                }
                                Label {
                                    layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                                    text: groupBoardPage.creatorLabel(ListItemData.creatorId)
                                    verticalAlignment: VerticalAlignment.Center
                                    textStyle { fontWeight: FontWeight.Bold; fontSize: FontSize.Medium }
                                }
                                Label {
                                    text: ListItemData.boardType === "poll" ? "Poll"
                                        : ListItemData.boardType === "note" ? "Note"
                                        : "Pinned Message"
                                    verticalAlignment: VerticalAlignment.Center
                                    textStyle { color: Color.Gray; fontSize: FontSize.Small }
                                }
                            }

                            Container {
                                visible: ListItemData.boardType === "poll"
                                horizontalAlignment: HorizontalAlignment.Fill
                                leftPadding: ui.du(0.8); rightPadding: ui.du(0.8)
                                layout: StackLayout { orientation: LayoutOrientation.TopToBottom }

                                Label {
                                    text: ListItemData.question || ""
                                    multiline: true
                                    textStyle { fontWeight: FontWeight.Bold; fontSize: FontSize.Medium }
                                }
                                Label {
                                    text: ListItemData.allowMultiChoices ? "Choose multiple options" : "Choose one option"
                                    textStyle { color: Color.Gray; fontSize: FontSize.XSmall }
                                    topMargin: ui.du(0.2); bottomMargin: ui.du(0.6)
                                }
                                Label {
                                    text: (ListItemData.numVote || 0) + " member" + ((ListItemData.numVote === 1) ? "" : "s") + " voted"
                                    textStyle { color: Color.create("#2575fc"); fontSize: FontSize.XSmall }
                                    bottomMargin: ui.du(0.6)
                                }
                                Container {
                                    visible: !!(ListItemData.options && ListItemData.options.length > 0)
                                    horizontalAlignment: HorizontalAlignment.Fill
                                    background: (ListItemData.options && ListItemData.options[0] && ListItemData.options[0].voted) ? Color.create("#cfe3fa") : Color.create("#f0f0f0")
                                    topPadding: ui.du(0.8); bottomPadding: ui.du(0.8)
                                    leftPadding: ui.du(1); rightPadding: ui.du(1)
                                    bottomMargin: ui.du(0.5)
                                    layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                                    gestureHandlers: [
                                        TapHandler {
                                            onTapped: {
                                                if (ListItemData.closed) return;
                                                groupBoardPage.doVoteOption(ListItemData.id, ListItemData.options[0].optionId,
                                                    !!ListItemData.allowMultiChoices, ListItemData.options);
                                            }
                                        }
                                    ]
                                    Label {
                                        layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                                        text: (ListItemData.options && ListItemData.options[0]) ? (ListItemData.options[0].content || "") : ""
                                        multiline: true
                                    }
                                    Label {
                                        text: (ListItemData.options && ListItemData.options[0]) ? String(ListItemData.options[0].votes || 0) : ""
                                        textStyle { color: Color.Gray }
                                    }
                                }
                                Container {
                                    visible: !!(ListItemData.options && ListItemData.options.length > 1)
                                    horizontalAlignment: HorizontalAlignment.Fill
                                    background: (ListItemData.options && ListItemData.options[1] && ListItemData.options[1].voted) ? Color.create("#cfe3fa") : Color.create("#f0f0f0")
                                    topPadding: ui.du(0.8); bottomPadding: ui.du(0.8)
                                    leftPadding: ui.du(1); rightPadding: ui.du(1)
                                    bottomMargin: ui.du(0.5)
                                    layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                                    gestureHandlers: [
                                        TapHandler {
                                            onTapped: {
                                                if (ListItemData.closed) return;
                                                groupBoardPage.doVoteOption(ListItemData.id, ListItemData.options[1].optionId,
                                                    !!ListItemData.allowMultiChoices, ListItemData.options);
                                            }
                                        }
                                    ]
                                    Label {
                                        layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                                        text: (ListItemData.options && ListItemData.options[1]) ? (ListItemData.options[1].content || "") : ""
                                        multiline: true
                                    }
                                    Label {
                                        text: (ListItemData.options && ListItemData.options[1]) ? String(ListItemData.options[1].votes || 0) : ""
                                        textStyle { color: Color.Gray }
                                    }
                                }
                                Container {
                                    visible: !!(ListItemData.options && ListItemData.options.length > 2)
                                    horizontalAlignment: HorizontalAlignment.Fill
                                    background: (ListItemData.options && ListItemData.options[2] && ListItemData.options[2].voted) ? Color.create("#cfe3fa") : Color.create("#f0f0f0")
                                    topPadding: ui.du(0.8); bottomPadding: ui.du(0.8)
                                    leftPadding: ui.du(1); rightPadding: ui.du(1)
                                    bottomMargin: ui.du(0.5)
                                    layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                                    gestureHandlers: [
                                        TapHandler {
                                            onTapped: {
                                                if (ListItemData.closed) return;
                                                groupBoardPage.doVoteOption(ListItemData.id, ListItemData.options[2].optionId,
                                                    !!ListItemData.allowMultiChoices, ListItemData.options);
                                            }
                                        }
                                    ]
                                    Label {
                                        layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                                        text: (ListItemData.options && ListItemData.options[2]) ? (ListItemData.options[2].content || "") : ""
                                        multiline: true
                                    }
                                    Label {
                                        text: (ListItemData.options && ListItemData.options[2]) ? String(ListItemData.options[2].votes || 0) : ""
                                        textStyle { color: Color.Gray }
                                    }
                                }
                                Container {
                                    visible: !!(ListItemData.options && ListItemData.options.length > 3)
                                    horizontalAlignment: HorizontalAlignment.Fill
                                    background: (ListItemData.options && ListItemData.options[3] && ListItemData.options[3].voted) ? Color.create("#cfe3fa") : Color.create("#f0f0f0")
                                    topPadding: ui.du(0.8); bottomPadding: ui.du(0.8)
                                    leftPadding: ui.du(1); rightPadding: ui.du(1)
                                    bottomMargin: ui.du(0.5)
                                    layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                                    gestureHandlers: [
                                        TapHandler {
                                            onTapped: {
                                                if (ListItemData.closed) return;
                                                groupBoardPage.doVoteOption(ListItemData.id, ListItemData.options[3].optionId,
                                                    !!ListItemData.allowMultiChoices, ListItemData.options);
                                            }
                                        }
                                    ]
                                    Label {
                                        layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                                        text: (ListItemData.options && ListItemData.options[3]) ? (ListItemData.options[3].content || "") : ""
                                        multiline: true
                                    }
                                    Label {
                                        text: (ListItemData.options && ListItemData.options[3]) ? String(ListItemData.options[3].votes || 0) : ""
                                        textStyle { color: Color.Gray }
                                    }
                                }
                                Container {
                                    visible: !!(ListItemData.options && ListItemData.options.length > 4)
                                    horizontalAlignment: HorizontalAlignment.Fill
                                    background: (ListItemData.options && ListItemData.options[4] && ListItemData.options[4].voted) ? Color.create("#cfe3fa") : Color.create("#f0f0f0")
                                    topPadding: ui.du(0.8); bottomPadding: ui.du(0.8)
                                    leftPadding: ui.du(1); rightPadding: ui.du(1)
                                    bottomMargin: ui.du(0.5)
                                    layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                                    gestureHandlers: [
                                        TapHandler {
                                            onTapped: {
                                                if (ListItemData.closed) return;
                                                groupBoardPage.doVoteOption(ListItemData.id, ListItemData.options[4].optionId,
                                                    !!ListItemData.allowMultiChoices, ListItemData.options);
                                            }
                                        }
                                    ]
                                    Label {
                                        layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                                        text: (ListItemData.options && ListItemData.options[4]) ? (ListItemData.options[4].content || "") : ""
                                        multiline: true
                                    }
                                    Label {
                                        text: (ListItemData.options && ListItemData.options[4]) ? String(ListItemData.options[4].votes || 0) : ""
                                        textStyle { color: Color.Gray }
                                    }
                                }
                                Container {
                                    visible: !!(ListItemData.options && ListItemData.options.length > 5)
                                    horizontalAlignment: HorizontalAlignment.Fill
                                    background: (ListItemData.options && ListItemData.options[5] && ListItemData.options[5].voted) ? Color.create("#cfe3fa") : Color.create("#f0f0f0")
                                    topPadding: ui.du(0.8); bottomPadding: ui.du(0.8)
                                    leftPadding: ui.du(1); rightPadding: ui.du(1)
                                    bottomMargin: ui.du(0.5)
                                    layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                                    gestureHandlers: [
                                        TapHandler {
                                            onTapped: {
                                                if (ListItemData.closed) return;
                                                groupBoardPage.doVoteOption(ListItemData.id, ListItemData.options[5].optionId,
                                                    !!ListItemData.allowMultiChoices, ListItemData.options);
                                            }
                                        }
                                    ]
                                    Label {
                                        layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                                        text: (ListItemData.options && ListItemData.options[5]) ? (ListItemData.options[5].content || "") : ""
                                        multiline: true
                                    }
                                    Label {
                                        text: (ListItemData.options && ListItemData.options[5]) ? String(ListItemData.options[5].votes || 0) : ""
                                        textStyle { color: Color.Gray }
                                    }
                                }
                            }

                            Container {
                                visible: ListItemData.boardType === "note" || ListItemData.boardType === "pin"
                                horizontalAlignment: HorizontalAlignment.Fill
                                layout: StackLayout { orientation: LayoutOrientation.TopToBottom }

                                Label {
                                    text: ListItemData.title || ""
                                    multiline: true
                                    textStyle { fontSize: FontSize.Medium }
                                    topMargin: ui.du(0.2); bottomMargin: ui.du(0.4)
                                }
                                Label {
                                    text: ListItemData.boardType === "note" ? "View note" : "Jump to message"
                                    textStyle { color: Color.create("#2575fc"); fontSize: FontSize.Small }
                                    gestureHandlers: [
                                        TapHandler {
                                            onTapped: {
                                                if (ListItemData.boardType === "pin") {
                                                    groupBoardPage.groupsNavRef.pop();
                                                    if (groupBoardPage.groupsNavRef.currentPage) {
                                                        groupBoardPage.groupsNavRef.currentPage.jumpToMessage(ListItemData.id || "");
                                                    }
                                                } else if (ListItemData.boardType === "note") {
                                                    noteViewerSheet.noteContent = ListItemData.title || "";
                                                    noteViewerSheet.creatorLabel = groupBoardPage.creatorLabel(ListItemData.creatorId);
                                                    noteViewerSheet.open();
                                                }
                                            }
                                        }
                                    ]
                                }
                            }
                        }
                    }
                }
            ]
        }

        Label {
            id: boardEmpty
            visible: false
            horizontalAlignment: HorizontalAlignment.Center
            verticalAlignment:   VerticalAlignment.Center
            multiline: true
            textStyle { color: Color.Gray; textAlign: TextAlign.Center }
        }

        ActivityIndicator {
            id: boardLoadingSpinner
            horizontalAlignment: HorizontalAlignment.Center
            verticalAlignment: VerticalAlignment.Center
            running: groupBoardPage.loading
            visible: groupBoardPage.loading
        }
        }
    }

    actions: [
        ActionItem {
            title: "Create note"
            imageSource: "asset:///images/ChatView/ic_action_new_note.png"
            ActionBar.placement: ActionBarPlacement.OnBar
            onTriggered: { createNoteSheet.open(); }
        },
        ActionItem {
            title: "Create Poll"
            imageSource: "asset:///images/ChatView/ic_review_add.png"
            ActionBar.placement: ActionBarPlacement.OnBar
            onTriggered: { createPollSheet.open(); }
        }
    ]

    attachedObjects: [
        Connections {
            target: zService
            onGroupBoardReady: {
                if (groupId !== groupBoardPage.groupId) return;
                groupBoardPage.loading = false;
                groupBoardPage.loadError = error;
                groupBoardPage.allItems = items;
                groupBoardPage.applyFilter();
                if (error.length > 0) {
                    boardErrorToast.body = "Couldn't load group board: " + error;
                    boardErrorToast.show();
                }
            }
        },
        Connections {
            target: zService
            onVotePollDone: {
                if (!success) {
                    boardErrorToast.body = "Couldn't submit vote: " + error;
                    boardErrorToast.show();
                    return;
                }
                groupBoardPage.applyVoteUpdate(pollId, updatedOptions);
            }
        },
        Connections {
            target: zService
            onCreateNoteDone: {
                if (success) {
                    boardErrorToast.body = "Note created";
                    boardErrorToast.show();
                    groupBoardPage.reload();
                } else {
                    boardErrorToast.body = "Couldn't create note: " + error;
                    boardErrorToast.show();
                }
            }
        },
        Connections {
            target: zService
            onCreatePollDone: {
                if (success) {
                    boardErrorToast.body = "Poll created";
                    boardErrorToast.show();
                    groupBoardPage.reload();
                } else {
                    boardErrorToast.body = "Couldn't create poll: " + error;
                    boardErrorToast.show();
                }
            }
        },
        SystemToast {
            id: boardErrorToast
            body: ""
        },
        CreateNoteSheet {
            id: createNoteSheet
            groupId: groupBoardPage.groupId
        },
        CreatePollSheet {
            id: createPollSheet
            groupId: groupBoardPage.groupId
        },
        NoteViewerSheet {
            id: noteViewerSheet
        }
    ]
}
