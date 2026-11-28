import bb.cascades 1.4
import bb.system 1.0
import QtQuick 1.0

// Full-screen editor for a screen capture (ChatView > Capture Screen). Only the picture, a black bar on top
// (cancel, 4 colour slots, colour picker, text box, bold / italic / underline, done) and the movable text boxes.
// Done hides the bar and the selection, takes the screen once more (the picture is shown 1:1, so that is the
// edited picture) and hands the file over through imageReady(). With no text box the original capture is used.
Sheet {
    id: capSheet

    signal imageReady(string path)

    property string sourcePath: ""
    property int    imgW: 0
    property int    imgH: 0
    // The area the picture is fitted in, and where the fitted picture sits inside it.
    property int    stageW: 0
    property int    stageH: 0
    property int    drawW: 0
    property int    drawH: 0
    property int    drawX: 0
    property int    drawY: 0

    property variant slotColors: ["#ffffff", "#000000", "#ff3b30", "#2575fc"]
    property int     activeSlot: 0
    property bool    curBold: false
    property bool    curItalic: false
    property bool    curUnderline: false
    property variant boxes: []
    property variant selectedBox: null

    property bool    showBar: true
    property bool    finalizing: false
    property bool    handedOver: false

    // Colour picker (HSV): hue in degrees, saturation and value 0..1.
    property bool    pickerOpen: false
    property real    pickH: 0
    property real    pickS: 0
    property real    pickV: 1
    property string  pickHex: "#ffffff"

    property int cellH:     app.screenWidthPx * 0.14
    property int iconPx:    app.screenWidthPx * 0.075
    property int dotPx:     app.screenWidthPx * 0.085
    property int wheelSize: app.screenWidthPx * 0.62
    property int ringPx:    app.screenWidthPx * 0.08
    property int barH:      app.screenWidthPx * 0.07
    property int barRingPx: app.screenWidthPx * 0.1

    function openWith(path, w, h) {
        clearBoxes();
        capSheet.sourcePath = path;
        capSheet.imgW = w;
        capSheet.imgH = h;
        capSheet.pickerOpen = false;
        capSheet.finalizing = false;
        capSheet.handedOver = false;
        capSheet.showBar = true;
        capSheet.curBold = false;
        capSheet.curItalic = false;
        capSheet.curUnderline = false;
        relayout();
        capSheet.open();
    }

    function relayout() {
        if (stageW <= 0 || stageH <= 0 || imgW <= 0 || imgH <= 0) return;
        var s = Math.min(stageW / imgW, stageH / imgH);
        drawW = Math.round(imgW * s);
        drawH = Math.round(imgH * s);
        drawX = Math.round((stageW - drawW) / 2);
        drawY = Math.round((stageH - drawH) / 2);
    }

    // ---- colours ----
    function hex2(n) {
        var s = Math.round(n * 255).toString(16);
        return s.length < 2 ? "0" + s : s;
    }

    function hsv2hex(h, s, v) {
        var c = v * s;
        var hp = h / 60;
        var x = c * (1 - Math.abs(hp % 2 - 1));
        var m = v - c;
        var r = 0, g = 0, b = 0;
        if (hp < 1)      { r = c; g = x; }
        else if (hp < 2) { r = x; g = c; }
        else if (hp < 3) { g = c; b = x; }
        else if (hp < 4) { g = x; b = c; }
        else if (hp < 5) { r = x; b = c; }
        else             { r = c; b = x; }
        return "#" + hex2(r + m) + hex2(g + m) + hex2(b + m);
    }

    function loadPick(hex) {
        var r = parseInt(hex.substring(1, 3), 16) / 255;
        var g = parseInt(hex.substring(3, 5), 16) / 255;
        var b = parseInt(hex.substring(5, 7), 16) / 255;
        var mx = Math.max(r, g, b);
        var mn = Math.min(r, g, b);
        var d = mx - mn;
        var h = 0;
        if (d > 0) {
            if (mx === r)      h = 60 * (((g - b) / d) % 6);
            else if (mx === g) h = 60 * ((b - r) / d + 2);
            else               h = 60 * ((r - g) / d + 4);
        }
        if (h < 0) h += 360;
        pickH = h;
        pickS = mx > 0 ? d / mx : 0;
        pickV = mx;
        pickHex = hex;
    }

    function pickWheel(x, y) {
        var R = wheelSize / 2;
        var dx = x - R;
        var dy = y - R;
        var a = Math.atan2(dy, dx) * 180 / Math.PI;
        if (a < 0) a += 360;
        pickH = a;
        pickS = Math.min(1, Math.sqrt(dx * dx + dy * dy) / R);
        pickHex = hsv2hex(pickH, pickS, pickV);
    }

    function pickBar(x) {
        pickV = 1 - Math.max(0, Math.min(1, x / wheelSize));
        pickHex = hsv2hex(pickH, pickS, pickV);
    }

    function currentColor() {
        return slotColors[activeSlot];
    }

    function selectSlot(i) {
        activeSlot = i;
        if (selectedBox) selectedBox.textColor = slotColors[i];
    }

    function openPicker() {
        loadPick(currentColor());
        pickerOpen = true;
    }

    // The picked colour replaces the active slot and recolours the selected text box.
    function applyPicker() {
        var a = [slotColors[0], slotColors[1], slotColors[2], slotColors[3]];
        a[activeSlot] = pickHex;
        slotColors = a;
        if (selectedBox) selectedBox.textColor = pickHex;
        pickerOpen = false;
    }

    // ---- text boxes ----
    function addTextBox() {
        var b = textBoxDef.createObject();
        if (!b) {
            console.log("[Capture] CaptureTextBox failed to load");
            return;
        }
        b.owner = capSheet;
        b.textColor = currentColor();
        b.bold = curBold;
        b.italic = curItalic;
        b.underline = curUnderline;
        b.maxX = Math.max(0, stageW - app.screenWidthPx * 0.6);
        b.maxY = Math.max(0, stageH - app.screenWidthPx * 0.2);
        b.posX = Math.round(stageW * 0.2);
        b.posY = Math.round(stageH * 0.25);
        stage.add(b);
        var a = boxes;
        a.push(b);
        boxes = a;
        selectBox(b);
        b.focusText();
    }

    function selectBox(b) {
        if (selectedBox && selectedBox !== b) selectedBox.selected = false;
        selectedBox = b;
        b.selected = true;
        // The bar mirrors the selected box.
        curBold = b.bold;
        curItalic = b.italic;
        curUnderline = b.underline;
    }

    function deselectAll() {
        for (var i = 0; i < boxes.length; i++) {
            boxes[i].selected = false;
            boxes[i].blurText();
        }
        selectedBox = null;
    }

    function removeBox(b) {
        var a = [];
        for (var i = 0; i < boxes.length; i++) {
            if (boxes[i] !== b) a.push(boxes[i]);
        }
        boxes = a;
        if (selectedBox === b) selectedBox = null;
        b.blurText();
        stage.remove(b);
    }

    function clearBoxes() {
        for (var i = 0; i < boxes.length; i++) stage.remove(boxes[i]);
        boxes = [];
        selectedBox = null;
    }

    function toggleBold() {
        curBold = !curBold;
        if (selectedBox) selectedBox.bold = curBold;
    }

    function toggleItalic() {
        curItalic = !curItalic;
        if (selectedBox) selectedBox.italic = curItalic;
    }

    function toggleUnderline() {
        curUnderline = !curUnderline;
        if (selectedBox) selectedBox.underline = curUnderline;
    }

    // ---- finish ----
    function cancelEdit() {
        if (finalizing) return;
        capSheet.close();
    }

    function finish() {
        if (finalizing) return;
        // Empty boxes would show their hint text on the final picture.
        var keep = [];
        for (var i = 0; i < boxes.length; i++) {
            if (boxes[i].text.replace(/\s+/g, "").length > 0) keep.push(boxes[i]);
            else stage.remove(boxes[i]);
        }
        boxes = keep;
        pickerOpen = false;
        if (keep.length === 0) {
            capSheet.handedOver = true;
            capSheet.imageReady(capSheet.sourcePath);
            capSheet.close();
            return;
        }
        deselectAll();
        showBar = false;
        finalizing = true;
        finishTimer.start();   // lets the bar, the selection and the keyboard disappear before the capture
    }

    onClosed: {
        if (!capSheet.handedOver && capSheet.sourcePath.length > 0) zService.deleteLocalFile(capSheet.sourcePath);
        clearBoxes();
        capSheet.sourcePath = "";
        capSheet.finalizing = false;
    }

    Page {
        attachedObjects: [
            ComponentDefinition {
                id: textBoxDef
                source: "CaptureTextBox.qml"
            },
            Timer {
                id: finishTimer
                interval: 500
                repeat: false
                onTriggered: { app.captureScreenRegion(capSheet.drawX, capSheet.drawY, capSheet.drawW, capSheet.drawH); }
            },
            // Only the second capture (Done) comes here; the first one is handled by ChatView.
            Connections {
                target: app
                onScreenCaptured: {
                    if (!capSheet.finalizing) return;
                    capSheet.finalizing = false;
                    capSheet.handedOver = true;
                    zService.deleteLocalFile(capSheet.sourcePath);
                    capSheet.imageReady(path);
                    capSheet.close();
                }
                onScreenCaptureFailed: {
                    if (!capSheet.finalizing) return;
                    capSheet.finalizing = false;
                    capSheet.showBar = true;
                    capErrorToast.body = qsTr("Could not capture the screen");
                    capErrorToast.show();
                }
            },
            SystemToast { id: capErrorToast }
        ]

        Container {
            layout: DockLayout {}
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment: VerticalAlignment.Fill
            background: Color.Black

            // The picture and the text boxes (absolute positions inside the stage).
            Container {
                id: stage
                layout: AbsoluteLayout {}
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment: VerticalAlignment.Fill
                attachedObjects: [
                    LayoutUpdateHandler {
                        onLayoutFrameChanged: {
                            // A keyboard that shrinks the page must not rescale the picture under the boxes.
                            if (capSheet.stageH > 0 && layoutFrame.height < capSheet.stageH * 0.9) return;
                            capSheet.stageW = layoutFrame.width;
                            capSheet.stageH = layoutFrame.height;
                            capSheet.relayout();
                        }
                    }
                ]

                ImageView {
                    imageSource: capSheet.sourcePath.length > 0 ? ("file://" + capSheet.sourcePath) : ""
                    scalingMethod: ScalingMethod.Fill
                    preferredWidth: capSheet.drawW
                    preferredHeight: capSheet.drawH
                    layoutProperties: AbsoluteLayoutProperties { positionX: capSheet.drawX; positionY: capSheet.drawY }
                }
            }

            // Black bar on top.
            Container {
                visible: capSheet.showBar
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment: VerticalAlignment.Top
                background: Color.create(0, 0, 0, 0.85)
                layout: StackLayout { orientation: LayoutOrientation.LeftToRight }

            // Cancel
            Container {
                layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                horizontalAlignment: HorizontalAlignment.Fill
                layout: DockLayout {}
                preferredHeight: capSheet.cellH
                ImageView {
                    horizontalAlignment: HorizontalAlignment.Center; verticalAlignment: VerticalAlignment.Center
                    preferredWidth: capSheet.iconPx; preferredHeight: capSheet.iconPx
                    scalingMethod: ScalingMethod.AspectFit
                    imageSource: "asset:///images/SettingsSheet/ic_close_white.png"
                }
                gestureHandlers: [
                    TapHandler { onTapped: { capSheet.cancelEdit(); } }
                ]
            }

            // Colour slot 1: tap to use it, the picker replaces the active slot's colour.
            Container {
                layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                horizontalAlignment: HorizontalAlignment.Fill
                layout: DockLayout {}
                preferredHeight: capSheet.cellH
                ImageView {
                    horizontalAlignment: HorizontalAlignment.Center; verticalAlignment: VerticalAlignment.Center
                    preferredWidth: capSheet.dotPx; preferredHeight: capSheet.dotPx
                    scalingMethod: ScalingMethod.AspectFit
                    imageSource: "asset:///images/ChatView/color_dot.png"
                    filterColor: Color.create(capSheet.slotColors[0])
                }
                ImageView {
                    horizontalAlignment: HorizontalAlignment.Center; verticalAlignment: VerticalAlignment.Center
                    preferredWidth: capSheet.dotPx; preferredHeight: capSheet.dotPx
                    scalingMethod: ScalingMethod.AspectFit
                    imageSource: "asset:///images/ChatView/color_dot_outline.png"
                }
                ImageView {
                    visible: capSheet.activeSlot === 0
                    horizontalAlignment: HorizontalAlignment.Center; verticalAlignment: VerticalAlignment.Center
                    preferredWidth: capSheet.dotPx; preferredHeight: capSheet.dotPx
                    scalingMethod: ScalingMethod.AspectFit
                    imageSource: "asset:///images/ChatView/color_dot_active.png"
                }
                gestureHandlers: [
                    TapHandler { onTapped: { capSheet.selectSlot(0); } }
                ]
            }

            // Colour slot 2: tap to use it, the picker replaces the active slot's colour.
            Container {
                layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                horizontalAlignment: HorizontalAlignment.Fill
                layout: DockLayout {}
                preferredHeight: capSheet.cellH
                ImageView {
                    horizontalAlignment: HorizontalAlignment.Center; verticalAlignment: VerticalAlignment.Center
                    preferredWidth: capSheet.dotPx; preferredHeight: capSheet.dotPx
                    scalingMethod: ScalingMethod.AspectFit
                    imageSource: "asset:///images/ChatView/color_dot.png"
                    filterColor: Color.create(capSheet.slotColors[1])
                }
                ImageView {
                    horizontalAlignment: HorizontalAlignment.Center; verticalAlignment: VerticalAlignment.Center
                    preferredWidth: capSheet.dotPx; preferredHeight: capSheet.dotPx
                    scalingMethod: ScalingMethod.AspectFit
                    imageSource: "asset:///images/ChatView/color_dot_outline.png"
                }
                ImageView {
                    visible: capSheet.activeSlot === 1
                    horizontalAlignment: HorizontalAlignment.Center; verticalAlignment: VerticalAlignment.Center
                    preferredWidth: capSheet.dotPx; preferredHeight: capSheet.dotPx
                    scalingMethod: ScalingMethod.AspectFit
                    imageSource: "asset:///images/ChatView/color_dot_active.png"
                }
                gestureHandlers: [
                    TapHandler { onTapped: { capSheet.selectSlot(1); } }
                ]
            }

            // Colour slot 3: tap to use it, the picker replaces the active slot's colour.
            Container {
                layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                horizontalAlignment: HorizontalAlignment.Fill
                layout: DockLayout {}
                preferredHeight: capSheet.cellH
                ImageView {
                    horizontalAlignment: HorizontalAlignment.Center; verticalAlignment: VerticalAlignment.Center
                    preferredWidth: capSheet.dotPx; preferredHeight: capSheet.dotPx
                    scalingMethod: ScalingMethod.AspectFit
                    imageSource: "asset:///images/ChatView/color_dot.png"
                    filterColor: Color.create(capSheet.slotColors[2])
                }
                ImageView {
                    horizontalAlignment: HorizontalAlignment.Center; verticalAlignment: VerticalAlignment.Center
                    preferredWidth: capSheet.dotPx; preferredHeight: capSheet.dotPx
                    scalingMethod: ScalingMethod.AspectFit
                    imageSource: "asset:///images/ChatView/color_dot_outline.png"
                }
                ImageView {
                    visible: capSheet.activeSlot === 2
                    horizontalAlignment: HorizontalAlignment.Center; verticalAlignment: VerticalAlignment.Center
                    preferredWidth: capSheet.dotPx; preferredHeight: capSheet.dotPx
                    scalingMethod: ScalingMethod.AspectFit
                    imageSource: "asset:///images/ChatView/color_dot_active.png"
                }
                gestureHandlers: [
                    TapHandler { onTapped: { capSheet.selectSlot(2); } }
                ]
            }

            // Colour slot 4: tap to use it, the picker replaces the active slot's colour.
            Container {
                layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                horizontalAlignment: HorizontalAlignment.Fill
                layout: DockLayout {}
                preferredHeight: capSheet.cellH
                ImageView {
                    horizontalAlignment: HorizontalAlignment.Center; verticalAlignment: VerticalAlignment.Center
                    preferredWidth: capSheet.dotPx; preferredHeight: capSheet.dotPx
                    scalingMethod: ScalingMethod.AspectFit
                    imageSource: "asset:///images/ChatView/color_dot.png"
                    filterColor: Color.create(capSheet.slotColors[3])
                }
                ImageView {
                    horizontalAlignment: HorizontalAlignment.Center; verticalAlignment: VerticalAlignment.Center
                    preferredWidth: capSheet.dotPx; preferredHeight: capSheet.dotPx
                    scalingMethod: ScalingMethod.AspectFit
                    imageSource: "asset:///images/ChatView/color_dot_outline.png"
                }
                ImageView {
                    visible: capSheet.activeSlot === 3
                    horizontalAlignment: HorizontalAlignment.Center; verticalAlignment: VerticalAlignment.Center
                    preferredWidth: capSheet.dotPx; preferredHeight: capSheet.dotPx
                    scalingMethod: ScalingMethod.AspectFit
                    imageSource: "asset:///images/ChatView/color_dot_active.png"
                }
                gestureHandlers: [
                    TapHandler { onTapped: { capSheet.selectSlot(3); } }
                ]
            }

            // Colour picker
            Container {
                layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                horizontalAlignment: HorizontalAlignment.Fill
                layout: DockLayout {}
                preferredHeight: capSheet.cellH
                ImageView {
                    horizontalAlignment: HorizontalAlignment.Center; verticalAlignment: VerticalAlignment.Center
                    preferredWidth: capSheet.iconPx; preferredHeight: capSheet.iconPx
                    scalingMethod: ScalingMethod.AspectFit
                    imageSource: "asset:///images/ChatView/ic_color_picker.png"
                }
                gestureHandlers: [
                    TapHandler { onTapped: { capSheet.openPicker(); } }
                ]
            }

            // New text box
            Container {
                layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                horizontalAlignment: HorizontalAlignment.Fill
                layout: DockLayout {}
                preferredHeight: capSheet.cellH
                ImageView {
                    horizontalAlignment: HorizontalAlignment.Center; verticalAlignment: VerticalAlignment.Center
                    preferredWidth: capSheet.iconPx; preferredHeight: capSheet.iconPx
                    scalingMethod: ScalingMethod.AspectFit
                    imageSource: "asset:///images/ChatView/toolbar_italic5.png"
                }
                gestureHandlers: [
                    TapHandler { onTapped: { capSheet.addTextBox(); } }
                ]
            }

            Container {
                layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                horizontalAlignment: HorizontalAlignment.Fill
                layout: DockLayout {}
                preferredHeight: capSheet.cellH
                ImageView {
                    horizontalAlignment: HorizontalAlignment.Center; verticalAlignment: VerticalAlignment.Center
                    preferredWidth: capSheet.iconPx; preferredHeight: capSheet.iconPx
                    scalingMethod: ScalingMethod.AspectFit
                    imageSource: "asset:///images/ChatView/toolbar_bold.png"
                opacity: capSheet.curBold ? 1.0 : 0.4
                }
                gestureHandlers: [
                    TapHandler { onTapped: { capSheet.toggleBold(); } }
                ]
            }

            Container {
                layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                horizontalAlignment: HorizontalAlignment.Fill
                layout: DockLayout {}
                preferredHeight: capSheet.cellH
                ImageView {
                    horizontalAlignment: HorizontalAlignment.Center; verticalAlignment: VerticalAlignment.Center
                    preferredWidth: capSheet.iconPx; preferredHeight: capSheet.iconPx
                    scalingMethod: ScalingMethod.AspectFit
                    imageSource: "asset:///images/ChatView/toolbar_italic.png"
                opacity: capSheet.curItalic ? 1.0 : 0.4
                }
                gestureHandlers: [
                    TapHandler { onTapped: { capSheet.toggleItalic(); } }
                ]
            }

            Container {
                layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                horizontalAlignment: HorizontalAlignment.Fill
                layout: DockLayout {}
                preferredHeight: capSheet.cellH
                ImageView {
                    horizontalAlignment: HorizontalAlignment.Center; verticalAlignment: VerticalAlignment.Center
                    preferredWidth: capSheet.iconPx; preferredHeight: capSheet.iconPx
                    scalingMethod: ScalingMethod.AspectFit
                    imageSource: "asset:///images/ChatView/toolbar_underline.png"
                opacity: capSheet.curUnderline ? 1.0 : 0.4
                }
                gestureHandlers: [
                    TapHandler { onTapped: { capSheet.toggleUnderline(); } }
                ]
            }

            // Done: use the picture in the chat
            Container {
                layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                horizontalAlignment: HorizontalAlignment.Fill
                layout: DockLayout {}
                preferredHeight: capSheet.cellH
                ImageView {
                    horizontalAlignment: HorizontalAlignment.Center; verticalAlignment: VerticalAlignment.Center
                    preferredWidth: capSheet.iconPx; preferredHeight: capSheet.iconPx
                    scalingMethod: ScalingMethod.AspectFit
                    imageSource: "asset:///images/ChatView/ic_done_white.png"
                }
                gestureHandlers: [
                    TapHandler { onTapped: { capSheet.finish(); } }
                ]
            }
            }

            // Colour picker: black panel in the middle of the screen over a dimmed picture; tap outside to cancel.
            Container {
                visible: capSheet.pickerOpen
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment: VerticalAlignment.Fill
                layout: DockLayout {}

                Container {
                    horizontalAlignment: HorizontalAlignment.Fill
                    verticalAlignment: VerticalAlignment.Fill
                    background: Color.create(0, 0, 0, 0.5)
                    gestureHandlers: [
                        TapHandler { onTapped: { capSheet.pickerOpen = false; } }
                    ]
                }

                Container {
                    horizontalAlignment: HorizontalAlignment.Center
                    verticalAlignment: VerticalAlignment.Center
                    background: Color.Black
                    layout: StackLayout {}
                    leftPadding: capSheet.wheelSize * 0.06; rightPadding: capSheet.wheelSize * 0.06
                    topPadding: capSheet.wheelSize * 0.04; bottomPadding: capSheet.wheelSize * 0.06

                    // Apply (the colour goes into the active slot).
                    Container {
                        horizontalAlignment: HorizontalAlignment.Right
                        layout: DockLayout {}
                        preferredWidth: capSheet.cellH
                        preferredHeight: capSheet.cellH
                        ImageView {
                            horizontalAlignment: HorizontalAlignment.Center; verticalAlignment: VerticalAlignment.Center
                            preferredWidth: capSheet.iconPx; preferredHeight: capSheet.iconPx
                            scalingMethod: ScalingMethod.AspectFit
                            imageSource: "asset:///images/ChatView/ic_done_white.png"
                        }
                        gestureHandlers: [
                            TapHandler { onTapped: { capSheet.applyPicker(); } }
                        ]
                    }

                    // Colour wheel with the hollow marker.
                    Container {
                        layout: AbsoluteLayout {}
                        horizontalAlignment: HorizontalAlignment.Center
                        preferredWidth: capSheet.wheelSize
                        preferredHeight: capSheet.wheelSize
                        ImageView {
                            preferredWidth: capSheet.wheelSize
                            preferredHeight: capSheet.wheelSize
                            scalingMethod: ScalingMethod.Fill
                            imageSource: "asset:///images/ChatView/color_wheel.png"
                            layoutProperties: AbsoluteLayoutProperties { positionX: 0; positionY: 0 }
                            onTouch: {
                                if (event.isDown() || (!event.isUp() && !event.isCancel()))
                                    capSheet.pickWheel(event.localX, event.localY);
                            }
                        }
                        ImageView {
                            preferredWidth: capSheet.ringPx
                            preferredHeight: capSheet.ringPx
                            scalingMethod: ScalingMethod.Fill
                            imageSource: "asset:///images/ChatView/picker_ring.png"
                            touchPropagationMode: TouchPropagationMode.None
                            layoutProperties: AbsoluteLayoutProperties {
                                positionX: capSheet.wheelSize / 2 + Math.cos(capSheet.pickH * Math.PI / 180) * capSheet.pickS * capSheet.wheelSize / 2 - capSheet.ringPx / 2
                                positionY: capSheet.wheelSize / 2 + Math.sin(capSheet.pickH * Math.PI / 180) * capSheet.pickS * capSheet.wheelSize / 2 - capSheet.ringPx / 2
                            }
                        }
                    }

                    // Brightness bar (white = bright, black = dark).
                    Container {
                        layout: AbsoluteLayout {}
                        horizontalAlignment: HorizontalAlignment.Center
                        topMargin: capSheet.wheelSize * 0.04
                        preferredWidth: capSheet.wheelSize
                        preferredHeight: capSheet.barRingPx
                        ImageView {
                            preferredWidth: capSheet.wheelSize
                            preferredHeight: capSheet.barH
                            scalingMethod: ScalingMethod.Fill
                            imageSource: "asset:///images/ChatView/brightness_bar.png"
                            layoutProperties: AbsoluteLayoutProperties { positionX: 0; positionY: (capSheet.barRingPx - capSheet.barH) / 2 }
                            onTouch: {
                                if (event.isDown() || (!event.isUp() && !event.isCancel()))
                                    capSheet.pickBar(event.localX);
                            }
                        }
                        ImageView {
                            preferredWidth: capSheet.barRingPx
                            preferredHeight: capSheet.barRingPx
                            scalingMethod: ScalingMethod.Fill
                            imageSource: "asset:///images/ChatView/picker_ring.png"
                            touchPropagationMode: TouchPropagationMode.None
                            layoutProperties: AbsoluteLayoutProperties {
                                positionX: (1 - capSheet.pickV) * capSheet.wheelSize - capSheet.barRingPx / 2
                                positionY: 0
                            }
                        }
                    }

                    // The colour that is picked.
                    Container {
                        horizontalAlignment: HorizontalAlignment.Center
                        topMargin: capSheet.wheelSize * 0.04
                        preferredWidth: capSheet.wheelSize
                        preferredHeight: capSheet.barH
                        background: Color.create(capSheet.pickHex)
                    }
                }
            }
        }
    }
}
