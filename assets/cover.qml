import bb.cascades 1.4

SceneCover {
    content: Container {
        layout: DockLayout {}
        horizontalAlignment: HorizontalAlignment.Fill
        verticalAlignment:   VerticalAlignment.Fill
        background: Color.create("#2575fc")
        ImageView {
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment:   VerticalAlignment.Fill
            scalingMethod: ScalingMethod.AspectFill
            imageSource: "asset:///images/SceneCover/cover.png"
        }
    }
}
