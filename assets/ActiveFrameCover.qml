// coverImg is set from applicationui.cpp, already picked for the current screen size

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
