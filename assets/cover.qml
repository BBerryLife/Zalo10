// cover.qml — BB10 Active Frame (SceneCover)
// Works on ALL BB10 devices: Z10/Z30/Z3/Leap (portrait) and Q10/Q20/Passport (square).
// Does NOT depend on coverImage() or screen detection — draws itself via QML.
import bb.cascades 1.4

SceneCover {
    id: rootCover

    content: Container {
        layout: DockLayout {}
        horizontalAlignment: HorizontalAlignment.Fill
        verticalAlignment:   VerticalAlignment.Fill
        background: Color.create("#2575fc")  // Zalo blue

        // Logo text — centered, always visible regardless of screen shape
        Container {
            horizontalAlignment: HorizontalAlignment.Center
            verticalAlignment:   VerticalAlignment.Center
            layout: StackLayout { orientation: LayoutOrientation.TopToBottom }

            Label {
                text: "Zalo10"
                horizontalAlignment: HorizontalAlignment.Center
                textStyle {
                    base: SystemDefaults.TextStyles.TitleText
                    color: Color.White
                    fontWeight: FontWeight.Bold
                }
            }
        }
    }
}
