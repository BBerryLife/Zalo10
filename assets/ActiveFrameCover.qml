// ActiveFrameCover.qml
// Context property from applicationui.cpp:
//   coverImg : QString — full asset URL for the correct cover image
//              (already selected in C++ based on screen size)

import bb.cascades 1.0

SceneCover {
    content: Container {
        layout: DockLayout {}
        horizontalAlignment: HorizontalAlignment.Fill
        verticalAlignment:   VerticalAlignment.Fill
        background: Color.Black

        ImageView {
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment:   VerticalAlignment.Fill
            scalingMethod: ScalingMethod.AspectFill
            imageSource: coverImg
        }
    }
}
