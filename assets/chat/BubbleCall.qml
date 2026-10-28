import bb.cascades 1.4
import bb.system 1.0
import QtQuick 1.0

Container {
    id: callBubble
    visible: !rowRoot.recalled
             && (ListItemData.msgType === 4 || ListItemData.msgType === "4")
    horizontalAlignment: HorizontalAlignment.Fill
    topMargin: 2; bottomMargin: 2
    layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
    maxWidth: rowRoot.bubbleMaxW

    function extractJsonStringField(content, key) {
        var c = content || "";
        if (c.length === 0 || c.charCodeAt(0) !== 123) return "";
        var k = '"' + key + '":"';
        var si = c.indexOf(k);
        if (si < 0) return "";
        si += k.length;
        var ei = si;
        while (ei < c.length) {
            var code = c.charCodeAt(ei);
            if (code === 92) { ei += 2; continue; }
            if (code === 34) break;
            ei++;
        }
        return c.substring(si, ei);
    }
    function extractJsonIntField(content, key) {
        var c = content || "";
        var k = '"' + key + '":';
        var si = c.indexOf(k);
        if (si < 0) return 0;
        si += k.length;
        var ei = si;
        while (ei < c.length && c.charAt(ei) >= '0' && c.charAt(ei) <= '9') ei++;
        var n = c.substring(si, ei);
        return n.length > 0 ? parseInt(n, 10) : 0;
    }
    property string cResult:   callBubble.extractJsonStringField(ListItemData.content, "callResult") || "ended"
    property string cKind:     callBubble.extractJsonStringField(ListItemData.content, "callKind") || "voice"
    property int    cDuration: callBubble.extractJsonIntField(ListItemData.content, "duration")
    property bool   cMissed:   callBubble.cResult === "missed"
    property bool   cVideo:    callBubble.cKind === "video"

    function iconForCall(missed, mine, video) {
        var kind = video ? "video" : "voice";
        var dir  = missed ? "missed" : (mine ? "outgoing" : "incoming");
        return "asset:///images/ChatView/bubble/video_voice/ca_" + kind + "_chat_" + dir + ".png";
    }
    function formatDuration(sec) {
        var s = sec || 0;
        var m = Math.floor(s / 60);
        var r = s % 60;
        var rStr = r < 10 ? ("0" + r) : ("" + r);
        return m + ":" + rStr;
    }
    function titleForCall(missed, video) {
        if (missed) return video ? "Missed video call" : "Missed call";
        return video ? "Video call" : "Voice call";
    }
    property string cIconSource: callBubble.iconForCall(callBubble.cMissed, rowRoot.mine, callBubble.cVideo)
    property string cTitle:      callBubble.titleForCall(callBubble.cMissed, callBubble.cVideo)
    property string cSubtitle:   callBubble.cMissed
                                  ? "No answer"
                                  : ("Call ended \u2022 " + callBubble.formatDuration(callBubble.cDuration))

    ImageView {
        imageSource: callBubble.cIconSource
        scalingMethod: ScalingMethod.AspectFit
        verticalAlignment: VerticalAlignment.Center
        preferredWidth: 56; preferredHeight: 56
        minWidth: 56
        rightMargin: 10
    }

    Container {
        layoutProperties: StackLayoutProperties { spaceQuota: 1 }
        verticalAlignment: VerticalAlignment.Center

        Label {
            text: callBubble.cTitle
            horizontalAlignment: HorizontalAlignment.Fill
            multiline: true
            textStyle {
                base:  SystemDefaults.TextStyles.BodyText
                color: callBubble.cMissed
                       ? Color.create("#e02020")
                       : (rowRoot.isDark ? Color.create("#eeeeee") : Color.create("#111111"))
            }
        }
        Label {
            text: callBubble.cSubtitle
            textStyle {
                fontSize: FontSize.XSmall
                fontStyle: FontStyle.Italic
                color: Color.create("#888888")
            }
            topMargin: 2
        }
    }

    // Informational only: no in-app calling, so tapping doesn't re-dial.
}
