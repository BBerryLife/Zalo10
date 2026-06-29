import bb.cascades 1.4

Sheet {
    id: photoViewerSheet

    property string imagePath: ""

    Page {
        titleBar: TitleBar {
            title: "Photo"
            dismissAction: ActionItem {
                title: "Close"
                onTriggered: { photoViewerSheet.close() }
            }
        }

        Container {
            background: Color.Black
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment:   VerticalAlignment.Fill
            layout: DockLayout {}

            ScrollView {
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment:   VerticalAlignment.Fill
                scrollViewProperties {
                    scrollMode: ScrollMode.Both
                    minContentScale: 0.25
                    maxContentScale: 4.0
                    initialScalingMethod: ScalingMethod.AspectFit
                }
                ImageView {
                    scalingMethod: ScalingMethod.AspectFit
                    imageSource: photoViewerSheet.imagePath
                }
            }
        }
    }
}
