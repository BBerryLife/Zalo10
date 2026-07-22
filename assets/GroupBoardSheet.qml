import bb.cascades 1.4
import bb.system 1.0
import QtQuick 1.0

// Group Board — shows all pinned messages, notes, and polls for a group in
// one place, matching Zalo's own "Group board" screen (see reference
// screenshot: header "Group Board" with a back chevron, a 4-way segmented
// tab strip [All / Pinned Message / Note / Poll] right below it, and two
// "Create note" / "Create poll" buttons pinned at the bottom of the list).
//
// Pushed as a Page (not a Cascades Sheet) into groupsNav, same convention as
// QuickMessagesSheet.qml — gives the native back chevron for free and keeps
// every "extra screen opened from a chat" in this codebase behaving the same
// way. Only ever opened for group threads (see chatViewPage's "Group board"
// overflow ActionItem, enabled: chatViewPage.isGroup) — there's no 1-1
// equivalent server-side.
Page {
    id: groupBoardPage

    property string groupId: ""
    property string groupName: ""
    property variant allItems: []   // raw items from zService.groupBoardReady, unfiltered
    property bool    loading: true
    property string  loadError: ""
    // Reference to the owning NavigationPane (groupsNav from GroupsTab.qml),
    // assigned right after this page is created — same "can't reach the
    // parent Nav from a pushed Page's own QML" constraint QuickMessagesSheet
    // sidesteps by having its OWNER call pop()/push() instead. Here the page
    // pops itself (for its own back button and the pin-tap jump), so it
    // needs the pane handed to it explicitly.
    property variant groupsNavRef: null

    // scrollToMsgId: set by ChatView before pushing this page when it was
    // opened via "Jump to message" on an already-known pin (kept for parity
    // with how jumpToMessage() works in ChatView itself); currently unused
    // by the fetch itself, reserved for a future "open board scrolled to a
    // specific pin" entry point.
    property string scrollToMsgId: ""

    function reload() {
        if (groupBoardPage.groupId.length === 0) return;
        groupBoardPage.loading = true;
        groupBoardPage.loadError = "";
        zService.fetchGroupBoard(groupBoardPage.groupId, 1, 50);
    }

    function applyFilter() {
        var tab = tabStrip.selectedIndex; // 0=All 1=Pin 2=Note 3=Poll
        var src = groupBoardPage.allItems;
        var filtered = [];
        for (var i = 0; i < src.length; i++) {
            var it = src[i];
            if (tab === 0) { filtered.push(it); continue; }
            if (tab === 1 && it.boardType === "pin")  { filtered.push(it); continue; }
            if (tab === 2 && it.boardType === "note") { filtered.push(it); continue; }
            if (tab === 3 && it.boardType === "poll") { filtered.push(it); continue; }
        }
        // Newest first, matching the reference screenshot (poll → note → pin,
        // top to bottom == most-recently-created first).
        filtered.sort(function(a, b) {
            var ta = a.createTime || 0, tb = b.createTime || 0;
            return tb - ta;
        });
        boardModel.clear();
        boardModel.insertList(filtered);
        boardEmpty.visible = (filtered.length === 0 && !groupBoardPage.loading);
        boardEmpty.text = (src.length === 0 && !groupBoardPage.loading)
            ? "Nothing pinned, noted, or polled yet.\nUse the buttons below to create one."
            : "No items in this tab.";
    }

    function creatorLabel(creatorId) {
        if (creatorId && creatorId.length > 0 && creatorId === zService.selfUid) return "Me";
        // No group-member directory is wired up in this build yet (see
        // ZaloService — there's no uid→displayName lookup exposed to QML),
        // so anyone other than "me" shows as a short uid fragment rather
        // than silently mislabeling them with the wrong name (the same class
        // of bug the Reply feature's dName fix just fixed the hard way —
        // better to show something honestly unresolved than something that
        // LOOKS like a name but is wrong).
        if (!creatorId || creatorId.length === 0) return "Unknown";
        return "User " + creatorId.slice(-4);
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

                // Plain title label, no explicit back-arrow icon/button —
                // matches QuickMessagesSheet.qml's header style. The native
                // NavigationPane back chevron (from this page having been
                // pushed onto groupsNav) still provides the actual back
                // affordance; it was simply being duplicated by the custom
                // ImageButton this container used to draw here.
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

        // 4-way segmented tab strip: All / Pinned Message / Note / Poll.
        // Lives in its own plain-background Container below the blue title
        // bar rather than inside it — SegmentedControl is a system control
        // with its own chrome (background pill + selection highlight), and
        // nesting it directly inside the custom #2575fc-painted title
        // Container made that chrome show through as a stray thin blue/teal
        // bar under the header instead of a normal tab strip.
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
                            leftPadding: ui.du(2); rightPadding: ui.du(2)
                            topPadding: ui.du(1.2); bottomPadding: ui.du(1.2)
                            layout: StackLayout { orientation: LayoutOrientation.TopToBottom }

                            // Creator row: avatar-less compact header — icon +
                            // "You created a poll" / "You pinned a message" /
                            // etc. style line, mirroring the hub-notification
                            // phrasing already used elsewhere in this codebase
                            // (see chatViewPage's hub-notification bubbles for
                            // create/share/pin/vote events).
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

                            // Poll body: question + up to 3 option rows with vote
                            // counts, "N member(s) voted", and a Change vote /
                            // Vote button — matches the reference screenshot's
                            // poll card layout.
                            Container {
                                visible: ListItemData.boardType === "poll"
                                horizontalAlignment: HorizontalAlignment.Fill
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
                                // Up to 6 option rows, rendered as fixed indexed
                                // slots rather than a repeated/looped component:
                                // this codebase's QML runtime (bb.cascades 1.4 /
                                // QtQuick 1.0) has no Repeater item, and nesting a
                                // second ListView inside this ListView's own
                                // delegate isn't a supported pattern either — the
                                // GroupBoardSheet.qml load failure this replaces
                                // was exactly this kind of "component doesn't
                                // exist here" mistake (property var, same root
                                // cause). 6 covers every poll size seen in
                                // practice (Zalo's own poll UI shows a "show
                                // more" affordance past a handful); a slot with
                                // no corresponding option just stays invisible.
                                Container {
                                    visible: !!(ListItemData.options && ListItemData.options.length > 0)
                                    horizontalAlignment: HorizontalAlignment.Fill
                                    background: (ListItemData.options && ListItemData.options[0] && ListItemData.options[0].voted) ? Color.create("#cfe3fa") : Color.create("#f0f0f0")
                                    topPadding: ui.du(0.8); bottomPadding: ui.du(0.8)
                                    leftPadding: ui.du(1); rightPadding: ui.du(1)
                                    bottomMargin: ui.du(0.5)
                                    layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
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

                            // Note / Pinned Message body: title/snippet + a
                            // "View note" / "Jump to message" link, matching
                            // the reference screenshot's card layout.
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
                                                }
                                                // Note detail viewing isn't wired up yet — Note
                                                // creation/board listing is the scope of this
                                                // pass; a dedicated note-viewer is follow-up work.
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

    // Create Note / Create Poll: real BB10 action-bar items (matches the
    // convention every other page in this app uses — see ChatView.qml's
    // `actions:` array — rather than two custom Buttons stacked in their
    // own Container sitting just above the system action bar, which is
    // what drew both an extra unwanted Divider and a visually "floating"
    // pair of buttons instead of native action-bar chrome.
    actions: [
        ActionItem {
            title: "Create note"
            imageSource: "asset:///images/ChatView/ic_action_new_note.png"
            ActionBar.placement: ActionBarPlacement.OnBar
            onTriggered: { createNoteUnderDevDialog.show(); }
        },
        ActionItem {
            title: "Create Poll"
            imageSource: "asset:///images/ChatView/ic_review_add.png"
            ActionBar.placement: ActionBarPlacement.OnBar
            onTriggered: { createPollUnderDevDialog.show(); }
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
        SystemToast {
            id: boardErrorToast
            body: ""
        },
        // Create Note / Create Poll composer UIs aren't built yet in this
        // pass (board LISTING was the scope) — these are honest placeholders
        // so the buttons do something rather than nothing, same convention
        // as the rest of this codebase's not-yet-built features (see
        // voiceCallUnderDevDialog / createEventUnderDevDialog in ChatView.qml,
        // which use this same InfoDialog.qml wrapper).
        InfoDialog {
            id: createNoteUnderDevDialog
            title: "Create Note"
            body: "This feature is still under development."
        },
        InfoDialog {
            id: createPollUnderDevDialog
            title: "Create Poll"
            body: "This feature is still under development."
        }
    ]
}
