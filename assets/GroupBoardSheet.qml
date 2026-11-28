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
        boardEmpty.visible = false;
        boardLoadTimeout.restart();
        zService.fetchGroupBoard(groupBoardPage.groupId, 1, 50);
    }

    // The tabs create this page with createObject() and set groupId AFTER it. onCreationCompleted
    // therefore ran reload() with an empty id, returned early and left the spinner on forever. Load
    // as soon as the id arrives instead.
    onGroupIdChanged: {
        if (groupBoardPage.groupId.length > 0) groupBoardPage.reload();
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

    function openPollVote(pollId) {
        if (!pollId || pollId.length === 0) return;
        for (var i = 0; i < groupBoardPage.allItems.length; i++) {
            var it = groupBoardPage.allItems[i];
            if (it.boardType === "poll" && it.id === pollId) {
                pollVoteSheet.isDark = app.getDarkTheme();
                pollVoteSheet.openFor({
                    pollId: it.id,
                    pollQuestion: it.question || it.title || "",
                    pollOptions: it.options || [],
                    pollAllowMulti: !!it.allowMultiChoices,
                    pollCreatorId: it.creatorId || "",
                    pollCreatedTs: it.createTime || 0,
                    pollNumVote: it.numVote || 0
                }, groupBoardPage.groupId);
                return;
            }
        }
    }

    function openPollVoters(pollId) {
        if (!pollId || pollId.length === 0) return;
        pollVotersSheet.isDark = app.getDarkTheme();
        pollVotersSheet.openFor(pollId);
    }

    // Bumped when member names arrive from the background lookup, so the labels re-evaluate.
    property int namesVersion: 0

    function creatorLabel(creatorId) {
        var nv = groupBoardPage.namesVersion;
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

    onCreationCompleted: { if (groupBoardPage.groupId.length > 0) reload(); }

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
                Option { text: qsTr("All") }
                Option { text: qsTr("Messages") }
                Option { text: qsTr("Note") }
                Option { text: qsTr("Poll") }
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

                // Row delegates cannot resolve ids outside their ListItemComponent (ReferenceError),
                // which made the Vote button and the other taps do nothing. Rows call these through
                // ListItem.view instead.
                property bool isDark: app.getDarkTheme()
                property int namesVersion: groupBoardPage.namesVersion
                function creatorLabel(creatorId)   { var nv = boardListView.namesVersion; return groupBoardPage.creatorLabel(creatorId); }
                function openPollVote(pollId)      { groupBoardPage.openPollVote(pollId); }
                function openPollVoters(pollId)    { groupBoardPage.openPollVoters(pollId); }
                function doVoteOption(pollId, optionId, allowMulti, options) {
                    groupBoardPage.doVoteOption(pollId, optionId, allowMulti, options);
                }
                function openNote(content, creatorId) {
                    noteViewerSheet.noteContent = content;
                    noteViewerSheet.creatorLabel = groupBoardPage.creatorLabel(creatorId);
                    noteViewerSheet.open();
                }
                function jumpToPinned(msgId) {
                    var nav = groupBoardPage.groupsNavRef;
                    if (!nav) return;
                    nav.pop();
                    var chat = nav.currentPage ? nav.currentPage : nav.activeChatPage;
                    if (chat) chat.jumpToMessage(msgId);
                }

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
            title: qsTr("Create note")
            imageSource: "asset:///images/ChatView/ic_action_new_note.png"
            ActionBar.placement: ActionBarPlacement.OnBar
            onTriggered: { createNoteSheet.open(); }
        },
        ActionItem {
            title: qsTr("Create Poll")
            imageSource: "asset:///images/ChatView/ic_review_add.png"
            ActionBar.placement: ActionBarPlacement.OnBar
            onTriggered: { createPollSheet.open(); }
        },
        ActionItem {
            title: qsTr("Reload")
            imageSource: "asset:///images/ChatsTab/ic_sync.png"
            ActionBar.placement: ActionBarPlacement.InOverflow
            onTriggered: { groupBoardPage.reload(); }
        }
    ]

    attachedObjects: [
        Connections {
            target: app
            onDarkThemeChanged: { boardListView.isDark = dark; }
        },
        Connections {
            target: zService
            onMemberNamesResolved: { groupBoardPage.namesVersion = groupBoardPage.namesVersion + 1; }
        },
        Connections {
            target: zService
            onGroupBoardReady: {
                if (groupId !== groupBoardPage.groupId) return;
                boardLoadTimeout.stop();
                groupBoardPage.loading = false;
                groupBoardPage.loadError = error;
                groupBoardPage.allItems = items;
                groupBoardPage.applyFilter();
                if (error.length > 0) {
                    boardErrorToast.body = qsTr("Couldn't load group board: ") + error;
                    boardErrorToast.show();
                }
            }
        },
        Connections {
            target: zService
            onVotePollDone: {
                if (!success) {
                    boardErrorToast.body = qsTr("Couldn't submit vote: ") + error;
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
                    boardErrorToast.body = qsTr("Note created");
                    boardErrorToast.show();
                    groupBoardPage.reload();
                } else {
                    boardErrorToast.body = qsTr("Couldn't create note: ") + error;
                    boardErrorToast.show();
                }
            }
        },
        Connections {
            target: zService
            onCreatePollDone: {
                if (success) {
                    boardErrorToast.body = qsTr("Poll created");
                    boardErrorToast.show();
                    groupBoardPage.reload();
                } else {
                    boardErrorToast.body = qsTr("Couldn't create poll: ") + error;
                    boardErrorToast.show();
                }
            }
        },
        Timer {
            // Safety net: if the board request never answers, stop the spinner and say so.
            id: boardLoadTimeout
            interval: 25000
            repeat: false
            onTriggered: {
                if (!groupBoardPage.loading) return;
                groupBoardPage.loading = false;
                groupBoardPage.loadError = "Timed out";
                groupBoardPage.applyFilter();
                boardErrorToast.body = qsTr("Couldn't load group board: timed out. Use Reload to try again.");
                boardErrorToast.show();
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
        },
        PollVoteSheet { id: pollVoteSheet },
        PollVotersSheet { id: pollVotersSheet }
    ]
}
