import bb.cascades 1.4

// Incoming-call slider, laid out like the stock Phone app's call screen: a dark pill spanning most of the
// screen width, a green answer end on the left, a red decline end on the right, a grey handle in the
// middle and chevrons between the handle and each end. Drag left to answer, right to decline. Released
// short of the threshold, the handle returns to the middle.
//
// Every size is a fraction of the screen width (app.screenWidthPx), so the layout is the same on square
// and portrait displays. The drag is tracked on the whole bar, not only on the handle, so the handle keeps
// following the finger.
//
// `progress` runs from -1 (fully on the answer end) to +1 (fully on the decline end); CallScreen reads it
// to tint the screen green or red while the slide is in progress.
Container {
    id: slider

    signal answered()
    signal declined()

    property real screenW: app.screenWidthPx > 0 ? app.screenWidthPx : 720
    property real plateW: screenW * 0.92
    property real plateH: plateW * 152 / 666
    property real handleD: plateH * 0.74
    property real glyphD: plateH * 0.62
    property real chevH: screenW * 0.045
    property real chevW: chevH * 0.6
    // Distance from the middle to the centre of either end.
    property real travel: plateW / 2 - plateH / 2
    // Fraction of the travel that counts as a completed slide.
    property real releaseFraction: 0.5

    property real handleX: 0
    property real progress: travel > 0 ? handleX / travel : 0
    property bool dragging: false
    property real anchorX: 0
    property real anchorHandle: 0

    horizontalAlignment: HorizontalAlignment.Fill
    layout: DockLayout {}
    preferredHeight: plateH * 1.25
    // The bar consumes the drag (Full), so it reaches its handler and never bubbles up to the page sheet behind,
    // which is what made the sheet peek out. The icons above pass their touches up to this bar (PassThrough).
    touchPropagationMode: TouchPropagationMode.Full

    // Drag surface: the whole bar. Positions come from windowX (window coordinates, not affected by the handle
    // moving), as a delta from where the drag was anchored. The anchor is the touch-down, or the first move if the
    // touch-down was lost. A light drag over the travel is enough; a short tap does nothing.
    onTouch: {
        if (event.isDown()) {
            slider.dragging = true;
            slider.anchorX = event.windowX;
            slider.anchorHandle = slider.handleX;
        } else if (event.isMove()) {
            if (!slider.dragging) {
                slider.dragging = true;
                slider.anchorX = event.windowX;
                slider.anchorHandle = slider.handleX;
            }
            slider.handleX = Math.max(-slider.travel, Math.min(slider.travel,
                                       slider.anchorHandle + (event.windowX - slider.anchorX)));
        } else if (event.isUp() || event.isCancel()) {
            if (slider.dragging) {
                slider.dragging = false;
                var reached = Math.abs(slider.handleX) >= slider.travel * slider.releaseFraction;
                var towardsDecline = slider.handleX > 0;
                slider.handleX = 0;                       // back to the middle
                if (reached) {
                    if (towardsDecline) slider.declined();
                    else slider.answered();
                }
            }
        }
    }

    // Plate.
    ImageView {
        imageSource: "asset:///images/ChatView/call/slider_plate.png"
        horizontalAlignment: HorizontalAlignment.Center
        verticalAlignment: VerticalAlignment.Center
        preferredWidth: slider.plateW
        preferredHeight: slider.plateH
        scalingMethod: ScalingMethod.AspectFit
        touchPropagationMode: TouchPropagationMode.PassThrough
    }

    // Answer glyph (left end).
    ImageView {
        imageSource: "asset:///images/ChatView/call/ic_voice_answer_gn.png"
        horizontalAlignment: HorizontalAlignment.Center
        verticalAlignment: VerticalAlignment.Center
        translationX: -slider.travel
        preferredWidth: slider.glyphD
        preferredHeight: slider.glyphD
        scalingMethod: ScalingMethod.AspectFit
        touchPropagationMode: TouchPropagationMode.PassThrough
    }

    // Decline glyph (right end).
    ImageView {
        imageSource: "asset:///images/ChatView/call/ic_decline_rd.png"
        horizontalAlignment: HorizontalAlignment.Center
        verticalAlignment: VerticalAlignment.Center
        translationX: slider.travel
        preferredWidth: slider.glyphD
        preferredHeight: slider.glyphD
        scalingMethod: ScalingMethod.AspectFit
        touchPropagationMode: TouchPropagationMode.PassThrough
    }

    // Chevrons towards answer (left): dim at rest, bright as the handle moves left.
    ImageView {
        imageSource: "asset:///images/ChatView/call/slider_arrow_gn.png"
        horizontalAlignment: HorizontalAlignment.Center
        verticalAlignment: VerticalAlignment.Center
        translationX: -slider.screenW * 0.105
        preferredWidth: slider.chevW
        preferredHeight: slider.chevH
        scalingMethod: ScalingMethod.AspectFit
        touchPropagationMode: TouchPropagationMode.PassThrough
        opacity: 0.25 + 0.75 * Math.max(0, -slider.progress)
    }
    ImageView {
        imageSource: "asset:///images/ChatView/call/slider_arrow_gn.png"
        horizontalAlignment: HorizontalAlignment.Center
        verticalAlignment: VerticalAlignment.Center
        translationX: -slider.screenW * 0.13
        preferredWidth: slider.chevW
        preferredHeight: slider.chevH
        scalingMethod: ScalingMethod.AspectFit
        touchPropagationMode: TouchPropagationMode.PassThrough
        opacity: 0.25 + 0.75 * Math.max(0, -slider.progress)
    }
    ImageView {
        imageSource: "asset:///images/ChatView/call/slider_arrow_gn.png"
        horizontalAlignment: HorizontalAlignment.Center
        verticalAlignment: VerticalAlignment.Center
        translationX: -slider.screenW * 0.155
        preferredWidth: slider.chevW
        preferredHeight: slider.chevH
        scalingMethod: ScalingMethod.AspectFit
        touchPropagationMode: TouchPropagationMode.PassThrough
        opacity: 0.25 + 0.75 * Math.max(0, -slider.progress)
    }

    // Chevrons towards decline (right): dim at rest, bright as the handle moves right.
    ImageView {
        imageSource: "asset:///images/ChatView/call/slider_arrow_rd.png"
        horizontalAlignment: HorizontalAlignment.Center
        verticalAlignment: VerticalAlignment.Center
        translationX: slider.screenW * 0.105
        preferredWidth: slider.chevW
        preferredHeight: slider.chevH
        scalingMethod: ScalingMethod.AspectFit
        touchPropagationMode: TouchPropagationMode.PassThrough
        opacity: 0.25 + 0.75 * Math.max(0, slider.progress)
    }
    ImageView {
        imageSource: "asset:///images/ChatView/call/slider_arrow_rd.png"
        horizontalAlignment: HorizontalAlignment.Center
        verticalAlignment: VerticalAlignment.Center
        translationX: slider.screenW * 0.13
        preferredWidth: slider.chevW
        preferredHeight: slider.chevH
        scalingMethod: ScalingMethod.AspectFit
        touchPropagationMode: TouchPropagationMode.PassThrough
        opacity: 0.25 + 0.75 * Math.max(0, slider.progress)
    }
    ImageView {
        imageSource: "asset:///images/ChatView/call/slider_arrow_rd.png"
        horizontalAlignment: HorizontalAlignment.Center
        verticalAlignment: VerticalAlignment.Center
        translationX: slider.screenW * 0.155
        preferredWidth: slider.chevW
        preferredHeight: slider.chevH
        scalingMethod: ScalingMethod.AspectFit
        touchPropagationMode: TouchPropagationMode.PassThrough
        opacity: 0.25 + 0.75 * Math.max(0, slider.progress)
    }

    // Handle: moves with the drag.
    ImageView {
        imageSource: "asset:///images/ChatView/call/slider_handle.png"
        horizontalAlignment: HorizontalAlignment.Center
        verticalAlignment: VerticalAlignment.Center
        translationX: slider.handleX
        preferredWidth: slider.handleD
        preferredHeight: slider.handleD
        scalingMethod: ScalingMethod.AspectFit
        touchPropagationMode: TouchPropagationMode.PassThrough
    }
}
