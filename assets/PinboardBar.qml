import bb.cascades 1.4
import QtQuick 1.0

// PinboardBar — the strip of currently-pinned group-board items shown right
// under the chat header, above the message list. Two states, matching the
// reference screenshots from the real Zalo app pixel-for-pixel in spirit:
//
//   collapsed (default): one row — the single most-recently-pinned item's
//   type icon + label + preview text, plus a "+N pin" button and a "..."
//   overflow button on the right. Tapping either expands the bar.
//
//   expanded: a "Pinboard (N)" header with a "Collapse" link, followed by
//   up to 3 full rows (Zalo itself never shows more than 3 pinned items at
//   once, regardless of how many exist — same cap applied here).
//
// Data: fed from chatViewPage.boardItems, the same raw item list
// GroupBoardSheet.qml already gets from zService's groupBoardReady signal
// (each item tagged with a "boardType" of "pin"/"note"/"poll" — see that
// file's header comment and ZaloService_Contacts.cpp's fetchGroupBoard()
// for the exact field mapping). This bar shows the 3 newest board items of
// ANY type, which is what the reference screenshots' "Message / Message /
// Poll" pinboard actually contains — Zalo's own client has a separate
// "pin a poll/note to the top" action that isn't exposed anywhere in this
// codebase's C++ yet (no public zca-js endpoint to port it from either, see
// ChatView.qml's PinboardBar wiring comment), so there's no way for this
// bar to distinguish "explicitly pinned" board items from ordinary ones.
// Using "3 newest of any type" is the closest achievable match with the
// data actually available.
//
// Read-only by design for the same reason: no "unpin" call exists to wire
// the "..." button's obvious action to, so it currently only offers
// navigation (jump to message / view poll / view note), not mutation.
Container {
    id: pinboardBar

    property variant items: []      // raw chatViewPage.boardItems
    property bool    isDark: false
    property bool    expanded: false

    // Newest-first, capped at 3 — recomputed whenever the source list or
    // sheet's contents change since QML variant properties aren't deep-
    // watched, chatViewPage always assigns a fresh array rather than
    // mutating in place (see onGroupBoardReady's wiring in ChatView.qml).
    property variant topItems: {
        var src = pinboardBar.items || [];
        var sorted = src.slice().sort(function (a, b) { return (b.createTime || 0) - (a.createTime || 0); });
        return sorted.slice(0, 3);
    }

    // boardType, id, title, creatorId of whichever row was tapped — ChatView
    // decides what "open" means per type (jump to message / open GroupBoard
    // filtered to the poll / show note content).
    signal itemTapped(string boardType, string itemId, string title, string creatorId)
    signal moreRequested()   // "..." tapped — ChatView opens the full Group Board

    visible: pinboardBar.topItems.length > 0
    horizontalAlignment: HorizontalAlignment.Fill
    layout: StackLayout { orientation: LayoutOrientation.TopToBottom }
    background: pinboardBar.isDark ? Color.create("#242424") : Color.White

    function typeLabel(boardType) {
        if (boardType === "poll") return "Poll";
        if (boardType === "note") return "Note";
        return "Message";
    }
    function typeIcon(boardType) {
        if (boardType === "poll") return "asset:///images/ChatView/ic_list.png";
        if (boardType === "note") return "asset:///images/ChatView/ic_action_new_note.png";
        return "asset:///images/ChatView/ic_textmessage.png";
    }
    // fetchGroupBoard() already formats pin/note titles as "Sender: text" /
    // plain note text, and poll titles as the poll question — reused as-is.
    function previewText(it) { return (it && it.title) ? it.title : ""; }
    function itemAt(idx) { return (pinboardBar.topItems.length > idx) ? pinboardBar.topItems[idx] : null; }

    divider: true

    // ---- Collapsed row -----------------------------------------------
    Container {
        id: collapsedRow
        visible: !pinboardBar.expanded
        horizontalAlignment: HorizontalAlignment.Fill
        layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
        topPadding: ui.du(1.0); bottomPadding: ui.du(1.0)
        leftPadding: ui.du(1.5); rightPadding: ui.du(1.0)

        gestureHandlers: [
            TapHandler {
                onTapped: {
                    var it = pinboardBar.itemAt(0);
                    if (it) pinboardBar.itemTapped(it.boardType || "", it.id || "", it.title || "", it.creatorId || "");
                }
            }
        ]

        ImageView {
            verticalAlignment: VerticalAlignment.Top
            topMargin: ui.du(0.3); rightMargin: ui.du(1.0)
            preferredWidth: ui.du(3); preferredHeight: ui.du(3)
            imageSource: pinboardBar.topItems.length > 0 ? pinboardBar.typeIcon(pinboardBar.itemAt(0).boardType) : ""
        }

        Container {
            layoutProperties: StackLayoutProperties { spaceQuota: 1 }
            layout: StackLayout { orientation: LayoutOrientation.TopToBottom }
            verticalAlignment: VerticalAlignment.Center

            Label {
                text: pinboardBar.topItems.length > 0 ? pinboardBar.typeLabel(pinboardBar.itemAt(0).boardType) : ""
                textStyle { fontWeight: FontWeight.Bold; fontSize: FontSize.Small; color: pinboardBar.isDark ? Color.White : Color.Black }
            }
            Label {
                text: pinboardBar.topItems.length > 0 ? pinboardBar.previewText(pinboardBar.itemAt(0)) : ""
                textStyle { fontSize: FontSize.XSmall; color: Color.Gray }
                multiline: false
            }
        }

        Button {
            visible: pinboardBar.topItems.length > 1
            verticalAlignment: VerticalAlignment.Center
            text: "+" + (pinboardBar.topItems.length - 1) + " pin"
            rightMargin: ui.du(0.5)
            onClicked: { pinboardBar.expanded = true; }
        }
        ImageButton {
            verticalAlignment: VerticalAlignment.Center
            preferredWidth: ui.du(5); preferredHeight: ui.du(5)
            defaultImageSource: "asset:///images/ChatView/ic_select_more.png"
            onClicked: { pinboardBar.moreRequested(); }
        }
    }

    // ---- Expanded panel -------------------------------------------------
    Container {
        id: expandedPanel
        visible: pinboardBar.expanded
        horizontalAlignment: HorizontalAlignment.Fill
        layout: StackLayout { orientation: LayoutOrientation.TopToBottom }

        Container {
            horizontalAlignment: HorizontalAlignment.Fill
            layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
            background: pinboardBar.isDark ? Color.create("#2a2a2a") : Color.create("#f2f2f2")
            topPadding: ui.du(0.8); bottomPadding: ui.du(0.8)
            leftPadding: ui.du(1.5); rightPadding: ui.du(1.5)

            Label {
                text: "Pinboard (" + pinboardBar.topItems.length + ")"
                layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                verticalAlignment: VerticalAlignment.Center
                textStyle { fontWeight: FontWeight.Bold; fontSize: FontSize.Small; color: pinboardBar.isDark ? Color.White : Color.Black }
            }
            Label {
                text: "Collapse"
                verticalAlignment: VerticalAlignment.Center
                textStyle { color: Color.create("#2575fc"); fontSize: FontSize.Small }
                gestureHandlers: [ TapHandler { onTapped: { pinboardBar.expanded = false; } } ]
            }
        }

        // Three fixed row slots (this codebase avoids QML Repeater inside
        // Cascades Containers — see GroupBoardSheet.qml's poll-option rows
        // for the same "hardcoded index 0..N-1, toggle visible" pattern —
        // so the 3-item pin cap is expressed the same way here).
        Container {
            visible: pinboardBar.topItems.length > 0
            horizontalAlignment: HorizontalAlignment.Fill
            layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
            topPadding: ui.du(1.0); bottomPadding: ui.du(1.0)
            leftPadding: ui.du(1.5); rightPadding: ui.du(1.0)
            gestureHandlers: [ TapHandler { onTapped: {
                var it = pinboardBar.itemAt(0);
                if (it) pinboardBar.itemTapped(it.boardType || "", it.id || "", it.title || "", it.creatorId || "");
            } } ]
            ImageView {
                verticalAlignment: VerticalAlignment.Top
                topMargin: ui.du(0.2); rightMargin: ui.du(1.0)
                preferredWidth: ui.du(3); preferredHeight: ui.du(3)
                imageSource: pinboardBar.topItems.length > 0 ? pinboardBar.typeIcon(pinboardBar.itemAt(0).boardType) : ""
            }
            Container {
                layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                layout: StackLayout { orientation: LayoutOrientation.TopToBottom }
                Label {
                    text: pinboardBar.topItems.length > 0 ? pinboardBar.typeLabel(pinboardBar.itemAt(0).boardType) : ""
                    textStyle { fontWeight: FontWeight.Bold; fontSize: FontSize.Small; color: pinboardBar.isDark ? Color.White : Color.Black }
                }
                Label {
                    text: pinboardBar.topItems.length > 0 ? pinboardBar.previewText(pinboardBar.itemAt(0)) : ""
                    textStyle { fontSize: FontSize.XSmall; color: Color.Gray }
                    multiline: false
                }
            }
            Label {
                text: "\u22EF"
                verticalAlignment: VerticalAlignment.Center
                textStyle { color: Color.Gray; fontSize: FontSize.Large }
            }
        }
        Divider { visible: pinboardBar.topItems.length > 1 }

        Container {
            visible: pinboardBar.topItems.length > 1
            horizontalAlignment: HorizontalAlignment.Fill
            layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
            topPadding: ui.du(1.0); bottomPadding: ui.du(1.0)
            leftPadding: ui.du(1.5); rightPadding: ui.du(1.0)
            gestureHandlers: [ TapHandler { onTapped: {
                var it = pinboardBar.itemAt(1);
                if (it) pinboardBar.itemTapped(it.boardType || "", it.id || "", it.title || "", it.creatorId || "");
            } } ]
            ImageView {
                verticalAlignment: VerticalAlignment.Top
                topMargin: ui.du(0.2); rightMargin: ui.du(1.0)
                preferredWidth: ui.du(3); preferredHeight: ui.du(3)
                imageSource: pinboardBar.topItems.length > 1 ? pinboardBar.typeIcon(pinboardBar.itemAt(1).boardType) : ""
            }
            Container {
                layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                layout: StackLayout { orientation: LayoutOrientation.TopToBottom }
                Label {
                    text: pinboardBar.topItems.length > 1 ? pinboardBar.typeLabel(pinboardBar.itemAt(1).boardType) : ""
                    textStyle { fontWeight: FontWeight.Bold; fontSize: FontSize.Small; color: pinboardBar.isDark ? Color.White : Color.Black }
                }
                Label {
                    text: pinboardBar.topItems.length > 1 ? pinboardBar.previewText(pinboardBar.itemAt(1)) : ""
                    textStyle { fontSize: FontSize.XSmall; color: Color.Gray }
                    multiline: false
                }
            }
            Label {
                text: "\u22EF"
                verticalAlignment: VerticalAlignment.Center
                textStyle { color: Color.Gray; fontSize: FontSize.Large }
            }
        }
        Divider { visible: pinboardBar.topItems.length > 2 }

        Container {
            visible: pinboardBar.topItems.length > 2
            horizontalAlignment: HorizontalAlignment.Fill
            layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
            topPadding: ui.du(1.0); bottomPadding: ui.du(1.0)
            leftPadding: ui.du(1.5); rightPadding: ui.du(1.0)
            gestureHandlers: [ TapHandler { onTapped: {
                var it = pinboardBar.itemAt(2);
                if (it) pinboardBar.itemTapped(it.boardType || "", it.id || "", it.title || "", it.creatorId || "");
            } } ]
            ImageView {
                verticalAlignment: VerticalAlignment.Top
                topMargin: ui.du(0.2); rightMargin: ui.du(1.0)
                preferredWidth: ui.du(3); preferredHeight: ui.du(3)
                imageSource: pinboardBar.topItems.length > 2 ? pinboardBar.typeIcon(pinboardBar.itemAt(2).boardType) : ""
            }
            Container {
                layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                layout: StackLayout { orientation: LayoutOrientation.TopToBottom }
                Label {
                    text: pinboardBar.topItems.length > 2 ? pinboardBar.typeLabel(pinboardBar.itemAt(2).boardType) : ""
                    textStyle { fontWeight: FontWeight.Bold; fontSize: FontSize.Small; color: pinboardBar.isDark ? Color.White : Color.Black }
                }
                Label {
                    text: pinboardBar.topItems.length > 2 ? pinboardBar.previewText(pinboardBar.itemAt(2)) : ""
                    textStyle { fontSize: FontSize.XSmall; color: Color.Gray }
                    multiline: false
                }
            }
            Label {
                text: "\u22EF"
                verticalAlignment: VerticalAlignment.Center
                textStyle { color: Color.Gray; fontSize: FontSize.Large }
            }
        }
    }
}
