// cover.qml — BB10 Active Frame (SceneCover)
import bb.cascades 1.4

SceneCover {
    id: rootCover

    content: Container {
        layout: DockLayout {}
        horizontalAlignment: HorizontalAlignment.Fill
        verticalAlignment:   VerticalAlignment.Fill
        background: Color.create("#2575fc")

        ImageView {
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment:   VerticalAlignment.Fill
            scalingMethod: ScalingMethod.AspectFill
            imageSource: app.coverImage()
        }
    }
}
