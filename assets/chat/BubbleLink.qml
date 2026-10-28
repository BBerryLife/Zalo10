import bb.cascades 1.4
import bb.system 1.0
import QtQuick 1.0

Container {
    id: linkBubble
    visible: !rowRoot.recalled
             && (ListItemData.msgType === 6 || ListItemData.msgType === "6")
    horizontalAlignment: HorizontalAlignment.Fill
    topMargin: 2; bottomMargin: 2
    maxWidth: rowRoot.bubbleMaxW
    background: rowRoot.isDark ? Color.create("#2a2a2a") : Color.create("#f2f2f2")
    layout: StackLayout { orientation: LayoutOrientation.TopToBottom }

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
    // Label has no maxLines in Cascades QML 1.0 (it crashed the parser): cap the length manually so
    // long descriptions don't blow up the bubble height.
    function truncateDesc(s) {
        if (s.length <= 140) return s;
        return s.substring(0, 140) + "...";
    }
    property string lTitle: linkBubble.extractJsonStringField(ListItemData.content, "linkTitle")
    property string lDesc:  linkBubble.truncateDesc(linkBubble.extractJsonStringField(ListItemData.content, "linkDesc"))
    property string lHref:  linkBubble.extractJsonStringField(ListItemData.content, "linkHref")
    property string lThumb: linkBubble.extractJsonStringField(ListItemData.content, "linkThumb")

    // Cascades QML 1.0 ImageView can't load remote https:// URLs ("Unsupported scheme (https)").
    // ListItemData.localImage is filled asynchronously after downloadImageMessage() ->
    // imageMsgReady -> applyImageUpdate(); falling back to lThumb is a last resort that won't load.
    ImageView {
        visible: (ListItemData.localImage && ListItemData.localImage.length > 0)
                 || linkBubble.lThumb.length > 0
        imageSource: (ListItemData.localImage && ListItemData.localImage.length > 0)
                     ? ListItemData.localImage
                     : linkBubble.lThumb
        scalingMethod: ScalingMethod.AspectFill
        horizontalAlignment: HorizontalAlignment.Fill
        preferredHeight: 220
        minHeight: 0
    }

    Container {
        topPadding: ui.du(1.0); bottomPadding: ui.du(1.0)
        leftPadding: ui.du(1.0); rightPadding: ui.du(1.0)
        layout: StackLayout { orientation: LayoutOrientation.TopToBottom }

        Label {
            text: linkBubble.lTitle.length > 0 ? linkBubble.lTitle : linkBubble.lHref
            horizontalAlignment: HorizontalAlignment.Fill
            multiline: true
            textStyle {
                base: SystemDefaults.TextStyles.BodyText
                fontWeight: FontWeight.Bold
                color: rowRoot.isDark ? Color.create("#eeeeee") : Color.create("#111111")
            }
        }
        Label {
            visible: linkBubble.lDesc.length > 0
            text: linkBubble.lDesc
            horizontalAlignment: HorizontalAlignment.Fill
            multiline: true
            textStyle {
                fontSize: FontSize.Small
                color: rowRoot.isDark ? Color.create("#aaaaaa") : Color.create("#666666")
            }
            topMargin: 4
        }
        Label {
            visible: linkBubble.lHref.length > 0
            text: linkBubble.lHref
            horizontalAlignment: HorizontalAlignment.Fill
            multiline: false
            textStyle {
                fontSize: FontSize.XSmall
                color: Color.create("#4a90d9")
            }
            topMargin: 6
        }
    }

    gestureHandlers: [
        TapHandler {
            onTapped: {
                if (linkBubble.lHref.length > 0)
                    Qt.openUrlExternally(linkBubble.lHref);
            }
        }
    ]
}
