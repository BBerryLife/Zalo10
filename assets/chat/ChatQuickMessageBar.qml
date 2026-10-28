import bb.cascades 1.4
import bb.system 1.0
import QtQuick 1.0

Container {
    id: qmSuggestBar
    visible: !!chatViewPage.qmMatch
    horizontalAlignment: HorizontalAlignment.Fill
    background: chatViewPage.isDark ? Color.create("#1e2a38") : Color.create("#dce8f5")
    topPadding:    ui.du(1.0)
    bottomPadding: ui.du(1.0)
    leftPadding:   ui.du(1.5)
    rightPadding:  ui.du(1.5)
    layout: StackLayout { orientation: LayoutOrientation.LeftToRight }

    gestureHandlers: [
        TapHandler {
            onTapped: {
                if (chatViewPage.qmMatch)
                    chatViewPage.applyQuickMessage(chatViewPage.qmMatch.content);
            }
        }
    ]

    Label {
        verticalAlignment: VerticalAlignment.Center
        text: chatViewPage.qmMatch ? ("/" + chatViewPage.qmMatch.name) : ""
        textStyle { color: Color.create("#2575fc"); fontWeight: FontWeight.Bold }
        multiline: false
        rightMargin: ui.du(1.5)
    }

    Label {
        layoutProperties: StackLayoutProperties { spaceQuota: 1 }
        verticalAlignment: VerticalAlignment.Center
        text: chatViewPage.qmMatch ? chatViewPage.qmMatch.content : ""
        textStyle {
            color: chatViewPage.isDark ? Color.create("#cfd8e3") : Color.create("#444444")
        }
        multiline: false
    }
}
