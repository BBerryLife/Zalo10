import bb.cascades 1.4
import bb.system 1.0
import QtQuick 1.0

Container {
    id: replyPreviewBar
    visible: chatViewPage.pendingReplyMsgId.length > 0
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
                    chatViewPage.pendingReplyMsgId      = "";
                    chatViewPage.pendingReplyCliMsgId   = "";
                    chatViewPage.pendingReplyOwnerId    = "";
                    chatViewPage.pendingReplySenderName = "";
                    chatViewPage.pendingReplyContent    = "";
                    chatViewPage.pendingReplyMsgType    = 0;
                    chatViewPage.pendingReplyTs         = "";
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
        verticalAlignment: VerticalAlignment.Fill
        preferredWidth: ui.du(0.5)
        minWidth: ui.du(0.5); maxWidth: ui.du(0.5)
        leftMargin: ui.du(0.5); rightMargin: ui.du(1.0)
        background: Color.create("#2575fc")
    }

    Container {
        layoutProperties: StackLayoutProperties { spaceQuota: 1 }
        verticalAlignment: VerticalAlignment.Center

        Label {
            text: chatViewPage.pendingReplySenderName
            multiline: false
            textStyle {
                fontSize: FontSize.Small
                fontWeight: FontWeight.Bold
                color: Color.create("#2575fc")
            }
            topMargin: 0; bottomMargin: 0
        }
        Label {
            text: (chatViewPage.pendingReplyMsgType === 2 || chatViewPage.pendingReplyMsgType === "2")
                  ? "[Photo]" : chatViewPage.pendingReplyContent
            multiline: false
            textStyle {
                fontSize: FontSize.Small
                color: chatViewPage.isDark ? Color.create("#cfd8e3") : Color.create("#444444")
            }
            topMargin: 0; bottomMargin: 0
        }
    }
}
