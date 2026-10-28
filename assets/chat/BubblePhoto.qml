import bb.cascades 1.4
import bb.system 1.0
import QtQuick 1.0

Container {
    id: photoBubble
    visible: !rowRoot.recalled
             && (ListItemData.msgType !== 3 && ListItemData.msgType !== "3")
             && (ListItemData.msgType !== 5 && ListItemData.msgType !== "5")
             && ((ListItemData.msgType === 2 || ListItemData.msgType === "2")
                 || (typeof ListItemData.content === "string"
                     && ListItemData.content.length > 1
                     && ListItemData.content.charAt(0) === "{"
                     && (ListItemData.content.indexOf("normalUrl") >= 0
                         || ListItemData.content.indexOf("thumbUrl") >= 0
                         || ListItemData.content.indexOf("thumb") >= 0
                         || ListItemData.content.indexOf("href") >= 0)))
    horizontalAlignment: HorizontalAlignment.Fill
    topMargin: 2; bottomMargin: 2

    Label {
        id: photoCaptionLbl
        horizontalAlignment: HorizontalAlignment.Fill
        visible: rowRoot.hasCaption(ListItemData.content)
        text: rowRoot.extractCaption(ListItemData.content)
        textStyle {
            base:  SystemDefaults.TextStyles.BodyText
            color: rowRoot.isDark ? Color.create("#eeeeee") : Color.create("#111111")
        }
        multiline: true
        topMargin: 0; bottomMargin: 4
    }

    Container {
        visible: photoCaptionLbl.visible
        horizontalAlignment: HorizontalAlignment.Fill
        preferredHeight: 1
        background: rowRoot.isDark ? Color.create("#555555") : Color.create("#cccccc")
        bottomMargin: 6
    }

    Container {
        id: photoFrame
        horizontalAlignment: HorizontalAlignment.Left
        property real photoMaxW: Math.max(1, rowRoot.bubbleMaxW - 28)
        property bool hasDims: !!(ListItemData.imgWidth  && ListItemData.imgWidth  > 0
                                  && ListItemData.imgHeight && ListItemData.imgHeight > 0)
        property bool hasImage: !!(ListItemData.localImage && ListItemData.localImage !== "")
        property real actualW: photoFrameLUH.layoutFrame.width
        property real ratio: hasDims ? (ListItemData.imgHeight / ListItemData.imgWidth) : 1
        preferredWidth:  hasDims ? Math.min(photoMaxW, ListItemData.imgWidth)
                                 : Math.min(photoMaxW, ui.du(30))
        preferredHeight: hasDims
                         ? ((actualW > 0 ? Math.min(actualW, photoMaxW)
                                         : Math.min(photoMaxW, ListItemData.imgWidth)) * ratio)
                         : ui.du(30)
        background: hasImage ? Color.Transparent
                             : (rowRoot.isDark ? Color.create("#3a3a3a") : Color.create("#e0e0e0"))
        layout: DockLayout {}
        attachedObjects: [ LayoutUpdateHandler { id: photoFrameLUH } ]

        ImageView {
            visible: !!(ListItemData.localImage && ListItemData.localImage !== "")
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment:   VerticalAlignment.Fill
            scalingMethod: ScalingMethod.AspectFit
            imageSource: ListItemData.localImage || ""
        }
        Label {
            visible: !(ListItemData.localImage && ListItemData.localImage !== "")
            text: "..."
            horizontalAlignment: HorizontalAlignment.Center
            verticalAlignment:   VerticalAlignment.Center
            textStyle {
                fontSize: FontSize.Small
                color: rowRoot.isDark ? Color.create("#888888") : Color.create("#808080")
            }
        }
    }

    Label {
        text: rowRoot.photoStatusText(rowRoot.mine, ListItemData.msgId, rowRoot.sendFailed)
        textStyle {
            fontSize: FontSize.XSmall
            fontStyle: FontStyle.Italic
            color: Color.create("#888888")
        }
        topMargin: 4; bottomMargin: 0
    }
}
