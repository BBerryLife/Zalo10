// SceneCover.qml — BB10 Active Frame (shown when app is minimized)
import bb.cascades 1.4

SceneCover {
    // BB10 Active Frame: displayed in the multitasking tray when app is minimized.
    // Uses cover.png as the background image.
    content: Container {
        layout: DockLayout {}
        horizontalAlignment: HorizontalAlignment.Fill
        verticalAlignment:   VerticalAlignment.Fill
        background: Color.create("#2575fc")

        // Cover image fills the frame
        ImageView {
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment:   VerticalAlignment.Fill
            scalingMethod: ScalingMethod.AspectFill
            imageSource: "asset:///images/cover.png"
        }

        // Subtle overlay label at bottom
        Container {
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment:   VerticalAlignment.Bottom
            background: Color.create(0.0, 0.0, 0.0, 0.45)
            topPadding:    8
            bottomPadding: 8
            leftPadding:   12
            rightPadding:  12

            Label {
                text: "Zalo10"
                horizontalAlignment: HorizontalAlignment.Left
                textStyle {
                    color:      Color.White
                    fontWeight: FontWeight.Bold
                    fontSize:   FontSize.Small
                }
                topMargin:    0
                bottomMargin: 0
            }
        }
    }
}
