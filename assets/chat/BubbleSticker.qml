import bb.cascades 1.4
import bb.system 1.0
import QtQuick 1.0

Container {
    id: stickerBubble
    visible: !rowRoot.recalled
             && (ListItemData.msgType === 5 || ListItemData.msgType === "5")
    horizontalAlignment: HorizontalAlignment.Left
    topMargin: 2; bottomMargin: 2
    preferredWidth: ui.du(30)
    preferredHeight: ui.du(30)
    maxWidth: ui.du(30)
    maxHeight: ui.du(30)
    layout: DockLayout {}

    function extractStickerId(content) {
        var c = content || "";
        var k = '"stickerId":';
        var si = c.indexOf(k);
        if (si < 0) return 0;
        si += k.length;
        var ei = si;
        while (ei < c.length && c.charAt(ei) >= '0' && c.charAt(ei) <= '9') ei++;
        var n = c.substring(si, ei);
        return n.length > 0 ? parseInt(n, 10) : 0;
    }
    property int sStickerId: stickerBubble.extractStickerId(ListItemData.content)
    // stickerLocalPath is patched by applyStickerUpdate() once the async download finishes; this
    // bubble only reads it. The download starts in C++ (WebSocket/Messages) because QML init
    // signals (Component.onCompleted, onVisibleChanged) never fired reliably for this nested delegate.
    property string sLocalPath: ListItemData.stickerLocalPath || ""

    ImageView {
        visible: stickerBubble.sLocalPath !== ""
        horizontalAlignment: HorizontalAlignment.Fill
        verticalAlignment:   VerticalAlignment.Fill
        scalingMethod: ScalingMethod.AspectFit
        imageSource: stickerBubble.sLocalPath
    }
    Label {
        visible: stickerBubble.sLocalPath === ""
        text: "..."
        horizontalAlignment: HorizontalAlignment.Center
        verticalAlignment:   VerticalAlignment.Center
        textStyle {
            fontSize: FontSize.Small
            color: rowRoot.isDark ? Color.create("#888888") : Color.create("#808080")
        }
    }
}
