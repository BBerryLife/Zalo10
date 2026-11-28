import bb.cascades 1.4

// One text box on a screen capture (created by ScreenCaptureSheet). While it is selected a small bar on top lets
// the user drag it (move handle) or delete it. The style properties are set by the sheet; `owner` is the sheet.
// Created dynamically, so it calls back into the owner instead of emitting signals.
Container {
    id: box

    property variant owner
    property string  textColor: "#ffffff"
    property bool    bold: false
    property bool    italic: false
    property bool    underline: false
    property bool    selected: false
    property real    maxX: 100000
    property real    maxY: 100000
    property alias   posX: boxPos.positionX
    property alias   posY: boxPos.positionY
    property alias   text: field.text

    property real dragStartX: 0
    property real dragStartY: 0
    property real dragBaseX: 0
    property real dragBaseY: 0

    preferredWidth: app.screenWidthPx * 0.6
    layout: StackLayout {}
    layoutProperties: AbsoluteLayoutProperties { id: boxPos; positionX: 0; positionY: 0 }

    function focusText() { field.requestFocus(); }
    function blurText()  { field.loseFocus(); }

    // TextDecoration is applied from script: if this OS version lacks it, only underline is lost and the box still loads.
    function applyUnderline() {
        try {
            field.textStyle.textDecoration = box.underline ? TextDecoration.Underline : TextDecoration.None;
        } catch (e) {
            console.log("[Capture] underline not supported");
        }
    }
    onUnderlineChanged: { applyUnderline(); }
    onCreationCompleted: { applyUnderline(); }

    Container {
        id: handleBar
        visible: box.selected
        horizontalAlignment: HorizontalAlignment.Fill
        background: Color.create(0, 0, 0, 0.6)
        layout: StackLayout { orientation: LayoutOrientation.LeftToRight }

        Container {
            layoutProperties: StackLayoutProperties { spaceQuota: 1 }
            horizontalAlignment: HorizontalAlignment.Fill
            layout: DockLayout {}
            preferredHeight: app.screenWidthPx * 0.1
            ImageView {
                horizontalAlignment: HorizontalAlignment.Center
                verticalAlignment: VerticalAlignment.Center
                preferredWidth: app.screenWidthPx * 0.07
                preferredHeight: app.screenWidthPx * 0.07
                scalingMethod: ScalingMethod.AspectFit
                imageSource: "asset:///images/ChatView/ic_reorder.png"
                touchPropagationMode: TouchPropagationMode.None
            }
            onTouch: {
                if (event.isDown()) {
                    box.owner.selectBox(box);
                    box.dragStartX = event.windowX;
                    box.dragStartY = event.windowY;
                    box.dragBaseX  = box.posX;
                    box.dragBaseY  = box.posY;
                } else if (!event.isUp() && !event.isCancel()) {
                    box.posX = Math.max(0, Math.min(box.maxX, box.dragBaseX + (event.windowX - box.dragStartX)));
                    box.posY = Math.max(0, Math.min(box.maxY, box.dragBaseY + (event.windowY - box.dragStartY)));
                }
            }
        }

        Container {
            layout: DockLayout {}
            preferredWidth: app.screenWidthPx * 0.12
            preferredHeight: app.screenWidthPx * 0.1
            ImageView {
                horizontalAlignment: HorizontalAlignment.Center
                verticalAlignment: VerticalAlignment.Center
                preferredWidth: app.screenWidthPx * 0.055
                preferredHeight: app.screenWidthPx * 0.055
                scalingMethod: ScalingMethod.AspectFit
                imageSource: "asset:///images/ChatView/action_delete.png"
            }
            gestureHandlers: [
                TapHandler { onTapped: { box.owner.removeBox(box); } }
            ]
        }
    }

    TextArea {
        id: field
        horizontalAlignment: HorizontalAlignment.Fill
        hintText: qsTr("Text")
        backgroundVisible: false
        textStyle.color: Color.create(box.textColor)
        textStyle.fontSize: FontSize.Large
        textStyle.fontWeight: box.bold ? FontWeight.Bold : FontWeight.Normal
        textStyle.fontStyle: box.italic ? FontStyle.Italic : FontStyle.Normal
        onTouch: {
            if (event.isDown()) box.owner.selectBox(box);
        }
    }
}
