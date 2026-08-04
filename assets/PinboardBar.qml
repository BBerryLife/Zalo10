import bb.cascades 1.4
import QtQuick 1.0

// Strip of pinned group-board items shown under the chat header, above the message list
// Two states: collapsed (most recent item + "+N" to expand) and expanded
// (header + up to 3 rows, same 3-item cap Zalo's own client uses)
//
// Fed from chatViewPage.boardItems (same data GroupBoardSheet.qml gets from
// zService's groupBoardReady signal). Shows the 3 newest items of any type —
// there's no "explicitly pinned" flag available yet, so this is the closest match
//
// Read-only: no "unpin" API exists yet, so this only supports navigation
// (jump to message / view poll / view note). Actual pin/unpin happens in
// the full Group Board sheet.
Container {
    id: pinboardBar

    property variant items: []      // raw chatViewPage.boardItems
    property bool    isDark: false
    property bool    expanded: false

    // Newest-first, capped at 3. Recomputed on every change since QML variant
    // properties aren't deep-watched, so ChatView.qml assigns a fresh array
    // each time instead of mutating in place
    property variant topItems: []

    function recomputeTopItems() {
        var src = pinboardBar.items || [];
        var sorted = src.slice().sort(function (a, b) { return (b.createTime || 0) - (a.createTime || 0); });
        pinboardBar.topItems = sorted.slice(0, 3);
    }

    onItemsChanged: recomputeTopItems()
    Component.onCompleted: recomputeTopItems()

    // ChatView decides what "open" means per boardType (jump to message /
    // open GroupBoard filtered to poll / show note content)
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
    // fetchGroupBoard() already formats titles appropriately per type — reused as-is
    function previewText(it) { return (it && it.title) ? it.title : ""; }
    function itemAt(idx) { return (pinboardBar.topItems.length > idx) ? pinboardBar.topItems[idx] : null; }

    // ---- Collapsed row -----------------------------------------------
    Container {
        id: collapsedRow
        visible: !pinboardBar.expanded
        horizontalAlignment: HorizontalAlignment.Fill
        layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
        topPadding: ui.du(0.15); bottomPadding: ui.du(0.15)
        leftPadding: ui.du(0.9); rightPadding: ui.du(0.5)

        gestureHandlers: [
            TapHandler {
                onTapped: {
                    var it = pinboardBar.itemAt(0);
                    if (it) {
                    // Pins pass the chat message's msgId (for ChatView.jumpToMessage());
                    // notes/polls pass their own board-item id instead. Falls back to
                    // it.id if msgId isn't set.
                    var jumpId = (it.boardType === "pin" && it.msgId) ? it.msgId : (it.id || "");
                    pinboardBar.itemTapped(it.boardType || "", jumpId, it.title || "", it.creatorId || "");
                }
                }
            }
        ]

        ImageView {
            verticalAlignment: VerticalAlignment.Center
            rightMargin: ui.du(0.6)
            preferredWidth: ui.du(1.9); preferredHeight: ui.du(1.9)
            imageSource: pinboardBar.topItems.length > 0 ? pinboardBar.typeIcon(pinboardBar.itemAt(0).boardType) : ""
        }

        Container {
            layoutProperties: StackLayoutProperties { spaceQuota: 1 }
            layout: StackLayout { orientation: LayoutOrientation.TopToBottom }
            verticalAlignment: VerticalAlignment.Center
            rightMargin: ui.du(0.8)

            Label {
                text: pinboardBar.topItems.length > 0 ? pinboardBar.typeLabel(pinboardBar.itemAt(0).boardType) : ""
                textStyle { fontWeight: FontWeight.Bold; fontSize: FontSize.XSmall; color: pinboardBar.isDark ? Color.White : Color.Black }
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
            text: "+" + (pinboardBar.topItems.length - 1)
            preferredWidth: ui.du(5.0); preferredHeight: ui.du(2.6)
            rightMargin: ui.du(1.0)
            onClicked: { pinboardBar.expanded = true; }
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
            topPadding: ui.du(0.2); bottomPadding: ui.du(0.2)
            leftPadding: ui.du(0.9); rightPadding: ui.du(0.9)

            Label {
                text: "Pinboard (" + pinboardBar.topItems.length + ")"
                layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                verticalAlignment: VerticalAlignment.Center
                textStyle { fontWeight: FontWeight.Bold; fontSize: FontSize.XSmall; color: pinboardBar.isDark ? Color.White : Color.Black }
            }
            Label {
                text: "Collapse"
                verticalAlignment: VerticalAlignment.Center
                textStyle { color: Color.create("#2575fc"); fontSize: FontSize.XSmall }
                gestureHandlers: [ TapHandler { onTapped: { pinboardBar.expanded = false; } } ]
            }
        }

        // Three fixed row slots — same hardcoded-index pattern as GroupBoardSheet.qml's
        // poll options, since Cascades Containers can't use a Repeater
        Container {
            visible: pinboardBar.topItems.length > 0
            horizontalAlignment: HorizontalAlignment.Fill
            layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
            topPadding: ui.du(0.12); bottomPadding: ui.du(0.12)
            leftPadding: ui.du(0.9); rightPadding: ui.du(0.6)
            gestureHandlers: [ TapHandler { onTapped: {
                var it = pinboardBar.itemAt(0);
                if (it) {
                    // Pins pass the chat message's msgId (for ChatView.jumpToMessage());
                    // notes/polls pass their own board-item id instead. Falls back to
                    // it.id if msgId isn't set.
                    var jumpId = (it.boardType === "pin" && it.msgId) ? it.msgId : (it.id || "");
                    pinboardBar.itemTapped(it.boardType || "", jumpId, it.title || "", it.creatorId || "");
                }
            } } ]
            ImageView {
                verticalAlignment: VerticalAlignment.Center
                rightMargin: ui.du(0.5)
                preferredWidth: ui.du(1.6); preferredHeight: ui.du(1.6)
                imageSource: pinboardBar.topItems.length > 0 ? pinboardBar.typeIcon(pinboardBar.itemAt(0).boardType) : ""
            }
            Container {
                layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                layout: StackLayout { orientation: LayoutOrientation.TopToBottom }
                verticalAlignment: VerticalAlignment.Center
                Label {
                    text: pinboardBar.topItems.length > 0 ? pinboardBar.typeLabel(pinboardBar.itemAt(0).boardType) : ""
                    textStyle { fontWeight: FontWeight.Bold; fontSize: FontSize.XSmall; color: pinboardBar.isDark ? Color.White : Color.Black }
                }
                Label {
                    text: pinboardBar.topItems.length > 0 ? pinboardBar.previewText(pinboardBar.itemAt(0)) : ""
                    textStyle { fontSize: FontSize.XSmall; color: Color.Gray }
                    multiline: false
                }
            }
        }
        Divider { visible: pinboardBar.topItems.length > 1 }

        Container {
            visible: pinboardBar.topItems.length > 1
            horizontalAlignment: HorizontalAlignment.Fill
            layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
            topPadding: ui.du(0.12); bottomPadding: ui.du(0.12)
            leftPadding: ui.du(0.9); rightPadding: ui.du(0.6)
            gestureHandlers: [ TapHandler { onTapped: {
                var it = pinboardBar.itemAt(1);
                if (it) {
                    // Pins pass the chat message's msgId (for ChatView.jumpToMessage());
                    // notes/polls pass their own board-item id instead. Falls back to
                    // it.id if msgId isn't set.
                    var jumpId = (it.boardType === "pin" && it.msgId) ? it.msgId : (it.id || "");
                    pinboardBar.itemTapped(it.boardType || "", jumpId, it.title || "", it.creatorId || "");
                }
            } } ]
            ImageView {
                verticalAlignment: VerticalAlignment.Center
                rightMargin: ui.du(0.5)
                preferredWidth: ui.du(1.6); preferredHeight: ui.du(1.6)
                imageSource: pinboardBar.topItems.length > 1 ? pinboardBar.typeIcon(pinboardBar.itemAt(1).boardType) : ""
            }
            Container {
                layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                layout: StackLayout { orientation: LayoutOrientation.TopToBottom }
                verticalAlignment: VerticalAlignment.Center
                Label {
                    text: pinboardBar.topItems.length > 1 ? pinboardBar.typeLabel(pinboardBar.itemAt(1).boardType) : ""
                    textStyle { fontWeight: FontWeight.Bold; fontSize: FontSize.XSmall; color: pinboardBar.isDark ? Color.White : Color.Black }
                }
                Label {
                    text: pinboardBar.topItems.length > 1 ? pinboardBar.previewText(pinboardBar.itemAt(1)) : ""
                    textStyle { fontSize: FontSize.XSmall; color: Color.Gray }
                    multiline: false
                }
            }
        }
        Divider { visible: pinboardBar.topItems.length > 2 }

        Container {
            visible: pinboardBar.topItems.length > 2
            horizontalAlignment: HorizontalAlignment.Fill
            layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
            topPadding: ui.du(0.12); bottomPadding: ui.du(0.12)
            leftPadding: ui.du(0.9); rightPadding: ui.du(0.6)
            gestureHandlers: [ TapHandler { onTapped: {
                var it = pinboardBar.itemAt(2);
                if (it) {
                    // Pins pass the chat message's msgId (for ChatView.jumpToMessage());
                    // notes/polls pass their own board-item id instead. Falls back to
                    // it.id if msgId isn't set.
                    var jumpId = (it.boardType === "pin" && it.msgId) ? it.msgId : (it.id || "");
                    pinboardBar.itemTapped(it.boardType || "", jumpId, it.title || "", it.creatorId || "");
                }
            } } ]
            ImageView {
                verticalAlignment: VerticalAlignment.Center
                rightMargin: ui.du(0.5)
                preferredWidth: ui.du(1.6); preferredHeight: ui.du(1.6)
                imageSource: pinboardBar.topItems.length > 2 ? pinboardBar.typeIcon(pinboardBar.itemAt(2).boardType) : ""
            }
            Container {
                layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                layout: StackLayout { orientation: LayoutOrientation.TopToBottom }
                verticalAlignment: VerticalAlignment.Center
                Label {
                    text: pinboardBar.topItems.length > 2 ? pinboardBar.typeLabel(pinboardBar.itemAt(2).boardType) : ""
                    textStyle { fontWeight: FontWeight.Bold; fontSize: FontSize.XSmall; color: pinboardBar.isDark ? Color.White : Color.Black }
                }
                Label {
                    text: pinboardBar.topItems.length > 2 ? pinboardBar.previewText(pinboardBar.itemAt(2)) : ""
                    textStyle { fontSize: FontSize.XSmall; color: Color.Gray }
                    multiline: false
                }
            }
        }
    }
}
