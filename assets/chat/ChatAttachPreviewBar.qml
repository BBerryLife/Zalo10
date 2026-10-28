import bb.cascades 1.4
import bb.system 1.0
import QtQuick 1.0

Container {
    id: attachPreviewBar
    visible: chatViewPage.pendingAttachPath.length > 0
    horizontalAlignment: HorizontalAlignment.Fill
    background: chatViewPage.isDark ? Color.create("#1e2a38") : Color.create("#dce8f5")
    topPadding:    ui.du(1.0)
    bottomPadding: ui.du(1.0)
    leftPadding:   ui.du(1.0)
    rightPadding:  ui.du(1.5)
    layout: StackLayout { orientation: LayoutOrientation.LeftToRight }

    Container {
        verticalAlignment: VerticalAlignment.Center
        preferredWidth:  ui.du(6)
        preferredHeight: ui.du(6)
        layout: DockLayout {}
        gestureHandlers: [
            TapHandler {
                onTapped: {
                    chatViewPage.pendingAttachPath = "";
                    chatViewPage.pendingAttachName = "";
                    sendAction.enabled = (inputField.text.trim().length > 0);
                }
            }
        ]
        Label {
            text: "✕"
            horizontalAlignment: HorizontalAlignment.Center
            verticalAlignment:   VerticalAlignment.Center
            textStyle {
                fontSize: FontSize.Medium
                fontWeight: FontWeight.Bold
                color: chatViewPage.isDark ? Color.create("#aaaaaa") : Color.create("#555555")
            }
        }
    }

    Container {
        verticalAlignment: VerticalAlignment.Center
        preferredWidth:  ui.du(7)
        preferredHeight: ui.du(7)
        minWidth:        ui.du(7)
        minHeight:       ui.du(7)
        maxWidth:        ui.du(7)
        maxHeight:       ui.du(7)
        leftMargin:  ui.du(0.5)
        rightMargin: ui.du(1.0)
        background: chatViewPage.isDark ? Color.create("#3a3a3a") : Color.create("#cccccc")
        layout: DockLayout {}
        ImageView {
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment:   VerticalAlignment.Fill
            scalingMethod: ScalingMethod.AspectFill
            imageSource: chatViewPage.pendingAttachPath.length > 0
                         ? ("file://" + chatViewPage.pendingAttachPath) : ""
        }
    }

    Label {
        layoutProperties: StackLayoutProperties { spaceQuota: 1 }
        verticalAlignment: VerticalAlignment.Center
        text: chatViewPage.pendingAttachName.length > 0
              ? chatViewPage.pendingAttachName : "Photo"
        multiline: false
        textStyle {
            fontSize: FontSize.Small
            color: chatViewPage.isDark ? Color.create("#dddddd") : Color.create("#222222")
        }
    }
}
