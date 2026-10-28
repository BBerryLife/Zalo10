import bb.cascades 1.4
import bb.system 1.0
import QtQuick 1.0
import "board"

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

    titleBar: BoardTitleBar {}

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
                    BoardListItem {}
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
