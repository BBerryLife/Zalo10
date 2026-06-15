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
