import bb.cascades 1.4
import QtQuick 1.0

Container {
    id: pinboardBar

    property variant items: []
    property bool    isDark: false
    property bool    expanded: false

    property variant topItems: []

    function recomputeTopItems() {
        var src = pinboardBar.items || [];
        var sorted = src.slice().sort(function (a, b) { return (b.createTime || 0) - (a.createTime || 0); });
        pinboardBar.topItems = sorted.slice(0, 3);
    }

    onItemsChanged: recomputeTopItems()
    Component.onCompleted: recomputeTopItems()

    signal itemTapped(string boardType, string itemId, string title, string creatorId)
    signal moreRequested()

    visible: pinboardBar.topItems.length > 0
    horizontalAlignment: HorizontalAlignment.Fill
    layout: StackLayout { orientation: LayoutOrientation.TopToBottom }
    background: pinboardBar.isDark ? Color.create("#242424") : Color.White

    function typeLabel(boardType) {
        if (boardType === "poll") return "Poll";
        if (boardType === "note") return "Note";
        return "Message";
    }
    function previewText(it) { return (it && it.title) ? it.title : ""; }
    function itemAt(idx) { return (pinboardBar.topItems.length > idx) ? pinboardBar.topItems[idx] : null; }

    Container {
        id: collapsedRow
        visible: !pinboardBar.expanded
        horizontalAlignment: HorizontalAlignment.Fill
        layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
        topPadding: ui.du(0.6); bottomPadding: ui.du(0.6)
        leftPadding: ui.du(2.0); rightPadding: ui.du(1.8)

        gestureHandlers: [
            TapHandler {
                onTapped: {
                    var it = pinboardBar.itemAt(0);
                    if (it) {
                    var jumpId = (it.boardType === "pin" && it.msgId) ? it.msgId : (it.id || "");
                    pinboardBar.itemTapped(it.boardType || "", jumpId, it.title || "", it.creatorId || "");
                }
                }
            }
        ]

        Container {
            verticalAlignment: VerticalAlignment.Fill
            preferredWidth: ui.du(0.4); minWidth: ui.du(0.4); maxWidth: ui.du(0.4)
            background: Color.create("#2575fc")
        }

        Container {
            layoutProperties: StackLayoutProperties { spaceQuota: 1 }
            layout: StackLayout { orientation: LayoutOrientation.TopToBottom }
            verticalAlignment: VerticalAlignment.Center
            leftPadding: ui.du(0.8)
            rightMargin: ui.du(0.8)

            Container {
                horizontalAlignment: HorizontalAlignment.Fill
                preferredHeight: ui.du(3.2); minHeight: ui.du(3.2); maxHeight: ui.du(3.2)
                layout: DockLayout {}
                Label {
                    verticalAlignment: VerticalAlignment.Center
                    text: pinboardBar.topItems.length > 0 ? pinboardBar.typeLabel(pinboardBar.itemAt(0).boardType) : ""
                    textStyle { fontWeight: FontWeight.Bold; fontSize: FontSize.XSmall; color: pinboardBar.isDark ? Color.White : Color.Black }
                }
            }
            Container {
                horizontalAlignment: HorizontalAlignment.Fill
                preferredHeight: ui.du(3.2); minHeight: ui.du(3.2); maxHeight: ui.du(3.2)
                layout: DockLayout {}
                Label {
                    verticalAlignment: VerticalAlignment.Center
                    text: pinboardBar.topItems.length > 0 ? pinboardBar.previewText(pinboardBar.itemAt(0)) : ""
                    textStyle { fontSize: FontSize.XSmall; color: Color.Gray }
                    multiline: false
                }
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

        // Three fixed row slots: Cascades Containers have no Repeater.
        Container {
            visible: pinboardBar.topItems.length > 0
            horizontalAlignment: HorizontalAlignment.Fill
            layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
            topPadding: ui.du(0.8); bottomPadding: ui.du(0.8)
            leftPadding: ui.du(0.9); rightPadding: ui.du(0.6)
            gestureHandlers: [ TapHandler { onTapped: {
                var it = pinboardBar.itemAt(0);
                if (it) {
                    var jumpId = (it.boardType === "pin" && it.msgId) ? it.msgId : (it.id || "");
                    pinboardBar.itemTapped(it.boardType || "", jumpId, it.title || "", it.creatorId || "");
                }
            } } ]
            Container {
                layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                layout: StackLayout { orientation: LayoutOrientation.TopToBottom }
                verticalAlignment: VerticalAlignment.Center
                Container {
                    horizontalAlignment: HorizontalAlignment.Fill
                    preferredHeight: ui.du(3.2); minHeight: ui.du(3.2); maxHeight: ui.du(3.2)
                    layout: DockLayout {}
                    Label {
                        verticalAlignment: VerticalAlignment.Center
                        text: pinboardBar.topItems.length > 0 ? pinboardBar.typeLabel(pinboardBar.itemAt(0).boardType) : ""
                        textStyle { fontWeight: FontWeight.Bold; fontSize: FontSize.XSmall; color: pinboardBar.isDark ? Color.White : Color.Black }
                    }
                }
                Container {
                    horizontalAlignment: HorizontalAlignment.Fill
                    preferredHeight: ui.du(3.2); minHeight: ui.du(3.2); maxHeight: ui.du(3.2)
                    layout: DockLayout {}
                    Label {
                        verticalAlignment: VerticalAlignment.Center
                        text: pinboardBar.topItems.length > 0 ? pinboardBar.previewText(pinboardBar.itemAt(0)) : ""
                        textStyle { fontSize: FontSize.XSmall; color: Color.Gray }
                        multiline: false
                    }
                }
            }
        }
        Container {
            visible: pinboardBar.topItems.length > 1
            horizontalAlignment: HorizontalAlignment.Fill
            preferredHeight: 1; minHeight: 1; maxHeight: 1
            background: pinboardBar.isDark ? Color.create("#3a3a3a") : Color.create("#e3e3e3")
        }

        Container {
            visible: pinboardBar.topItems.length > 1
            horizontalAlignment: HorizontalAlignment.Fill
            layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
            topPadding: ui.du(0.8); bottomPadding: ui.du(0.8)
            leftPadding: ui.du(0.9); rightPadding: ui.du(0.6)
            gestureHandlers: [ TapHandler { onTapped: {
                var it = pinboardBar.itemAt(1);
                if (it) {
                    var jumpId = (it.boardType === "pin" && it.msgId) ? it.msgId : (it.id || "");
                    pinboardBar.itemTapped(it.boardType || "", jumpId, it.title || "", it.creatorId || "");
                }
            } } ]
            Container {
                layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                layout: StackLayout { orientation: LayoutOrientation.TopToBottom }
                verticalAlignment: VerticalAlignment.Center
                Container {
                    horizontalAlignment: HorizontalAlignment.Fill
                    preferredHeight: ui.du(3.2); minHeight: ui.du(3.2); maxHeight: ui.du(3.2)
                    layout: DockLayout {}
                    Label {
                        verticalAlignment: VerticalAlignment.Center
                        text: pinboardBar.topItems.length > 1 ? pinboardBar.typeLabel(pinboardBar.itemAt(1).boardType) : ""
                        textStyle { fontWeight: FontWeight.Bold; fontSize: FontSize.XSmall; color: pinboardBar.isDark ? Color.White : Color.Black }
                    }
                }
                Container {
                    horizontalAlignment: HorizontalAlignment.Fill
                    preferredHeight: ui.du(3.2); minHeight: ui.du(3.2); maxHeight: ui.du(3.2)
                    layout: DockLayout {}
                    Label {
                        verticalAlignment: VerticalAlignment.Center
                        text: pinboardBar.topItems.length > 1 ? pinboardBar.previewText(pinboardBar.itemAt(1)) : ""
                        textStyle { fontSize: FontSize.XSmall; color: Color.Gray }
                        multiline: false
                    }
                }
            }
        }
        Container {
            visible: pinboardBar.topItems.length > 2
            horizontalAlignment: HorizontalAlignment.Fill
            preferredHeight: 1; minHeight: 1; maxHeight: 1
            background: pinboardBar.isDark ? Color.create("#3a3a3a") : Color.create("#e3e3e3")
        }

        Container {
            visible: pinboardBar.topItems.length > 2
            horizontalAlignment: HorizontalAlignment.Fill
            layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
            topPadding: ui.du(0.8); bottomPadding: ui.du(0.8)
            leftPadding: ui.du(0.9); rightPadding: ui.du(0.6)
            gestureHandlers: [ TapHandler { onTapped: {
                var it = pinboardBar.itemAt(2);
                if (it) {
                    var jumpId = (it.boardType === "pin" && it.msgId) ? it.msgId : (it.id || "");
                    pinboardBar.itemTapped(it.boardType || "", jumpId, it.title || "", it.creatorId || "");
                }
            } } ]
            Container {
                layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                layout: StackLayout { orientation: LayoutOrientation.TopToBottom }
                verticalAlignment: VerticalAlignment.Center
                Container {
                    horizontalAlignment: HorizontalAlignment.Fill
                    preferredHeight: ui.du(3.2); minHeight: ui.du(3.2); maxHeight: ui.du(3.2)
                    layout: DockLayout {}
                    Label {
                        verticalAlignment: VerticalAlignment.Center
                        text: pinboardBar.topItems.length > 2 ? pinboardBar.typeLabel(pinboardBar.itemAt(2).boardType) : ""
                        textStyle { fontWeight: FontWeight.Bold; fontSize: FontSize.XSmall; color: pinboardBar.isDark ? Color.White : Color.Black }
                    }
                }
                Container {
                    horizontalAlignment: HorizontalAlignment.Fill
                    preferredHeight: ui.du(3.2); minHeight: ui.du(3.2); maxHeight: ui.du(3.2)
                    layout: DockLayout {}
                    Label {
                        verticalAlignment: VerticalAlignment.Center
                        text: pinboardBar.topItems.length > 2 ? pinboardBar.previewText(pinboardBar.itemAt(2)) : ""
                        textStyle { fontSize: FontSize.XSmall; color: Color.Gray }
                        multiline: false
                    }
                }
            }
        }
    }
}
