import bb.cascades 1.4
import bb.system 1.0

Page {
    id: donatePage

    titleBar: TitleBar {
        kind: TitleBarKind.FreeForm
        kindProperties: FreeFormTitleBarKindProperties {
            content: Container {
                background: Color.create("#2575fc")
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment:   VerticalAlignment.Fill
                layout: DockLayout {}
                leftPadding: ui.du(2.5)
                Label {
                    text: "Support"
                    textStyle {
                        color:      Color.White
                        base:       SystemDefaults.TextStyles.TitleText
                        fontWeight: FontWeight.Bold
                    }
                    verticalAlignment:   VerticalAlignment.Center
                    horizontalAlignment: HorizontalAlignment.Left
                }
            }
        }
    }

    ScrollView {
        horizontalAlignment: HorizontalAlignment.Fill
        verticalAlignment:   VerticalAlignment.Fill
        scrollViewProperties.scrollMode: ScrollMode.Vertical

        Container {
            horizontalAlignment: HorizontalAlignment.Fill
            topPadding:    ui.du(3)
            bottomPadding: ui.du(3)
            leftPadding:   ui.du(3)
            rightPadding:  ui.du(3)

            // App logo
            Container {
                horizontalAlignment: HorizontalAlignment.Center
                bottomMargin: ui.du(2)
                ImageView {
                    horizontalAlignment: HorizontalAlignment.Center
                    imageSource: "asset:///images/berrylife.png"
                    preferredWidth:  ui.du(16)
                    preferredHeight: ui.du(16)
                    scalingMethod: ScalingMethod.AspectFit
                }
            }

            // Title
            Label {
                text: "Support Zalo10"
                horizontalAlignment: HorizontalAlignment.Center
                textStyle {
                    base:       SystemDefaults.TextStyles.TitleText
                    fontWeight: FontWeight.Bold
                    color:      Color.create("#2575fc")
                }
                bottomMargin: ui.du(1)
            }

            // Subtitle
            Label {
                text: "Zalo10 is a free, open-source Zalo client for BlackBerry 10, developed and maintained by BerryLife."
                horizontalAlignment: HorizontalAlignment.Center
                multiline: true
                textStyle {
                    base:  SystemDefaults.TextStyles.BodyText
                    color: Color.create("#444444")
                }
                bottomMargin: ui.du(3)
            }

            // Divider
            Container {
                horizontalAlignment: HorizontalAlignment.Fill
                preferredHeight: 1
                background: Color.create("#DDDDDD")
                bottomMargin: ui.du(3)
            }

            // Support info
            Label {
                text: "If you find Zalo10 useful, consider supporting the project:"
                multiline: true
                horizontalAlignment: HorizontalAlignment.Left
                textStyle {
                    base:  SystemDefaults.TextStyles.BodyText
                    color: Color.create("#333333")
                }
                bottomMargin: ui.du(2)
            }

            // GitHub row
            Container {
                layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                horizontalAlignment: HorizontalAlignment.Fill
                bottomMargin: ui.du(1.5)
                ImageView {
                    imageSource: "asset:///images/ic_share.png"
                    preferredWidth:  ui.du(5)
                    preferredHeight: ui.du(5)
                    verticalAlignment: VerticalAlignment.Center
                    rightMargin: ui.du(1.5)
                    scalingMethod: ScalingMethod.AspectFit
                }
                Label {
                    text: "GitHub: github.com/BBerryLife/Zalo10"
                    verticalAlignment: VerticalAlignment.Center
                    textStyle {
                        base:  SystemDefaults.TextStyles.BodyText
                        color: Color.create("#2575fc")
                    }
                    layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                }
            }

            // Report bugs row
            Container {
                layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                horizontalAlignment: HorizontalAlignment.Fill
                bottomMargin: ui.du(3)
                ImageView {
                    imageSource: "asset:///images/ic_mail.png"
                    preferredWidth:  ui.du(5)
                    preferredHeight: ui.du(5)
                    verticalAlignment: VerticalAlignment.Center
                    rightMargin: ui.du(1.5)
                    scalingMethod: ScalingMethod.AspectFit
                }
                Label {
                    text: "Report issues on GitHub Issues"
                    verticalAlignment: VerticalAlignment.Center
                    textStyle {
                        base:  SystemDefaults.TextStyles.BodyText
                        color: Color.create("#333333")
                    }
                    layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                }
            }

            // Divider
            Container {
                horizontalAlignment: HorizontalAlignment.Fill
                preferredHeight: 1
                background: Color.create("#DDDDDD")
                bottomMargin: ui.du(2)
            }

            Label {
                text: "Version " + app.appVersion()
                horizontalAlignment: HorizontalAlignment.Center
                textStyle {
                    base:  SystemDefaults.TextStyles.SubtitleText
                    color: Color.create("#999999")
                }
            }
        }
    }
}
