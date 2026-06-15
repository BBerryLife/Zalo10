import bb.cascades 1.4
import QtQuick 1.0

Sheet {
    id: aboutSheetRoot

    NavigationPane {
        id: aboutNav

        Page {
            titleBar: TitleBar {
                scrollBehavior: TitleBarScrollBehavior.Sticky
                kind: TitleBarKind.FreeForm
                kindProperties: FreeFormTitleBarKindProperties {
                    content: Container {
                        background: Color.create("#2575fc")
                        horizontalAlignment: HorizontalAlignment.Fill
                        verticalAlignment:   VerticalAlignment.Fill
                        layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                        leftPadding: ui.du(1)

                        ImageButton {
                            verticalAlignment: VerticalAlignment.Center
                            preferredWidth:  ui.du(6); preferredHeight: ui.du(6)
                            defaultImageSource: "asset:///images/ic_close_white.png"
                            pressedImageSource: "asset:///images/ic_close_white.png"
                            rightMargin: ui.du(0.5)
                            onClicked: { aboutSheetRoot.close() }
                        }

                        Label {
                            text: "About"
                            layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                            verticalAlignment: VerticalAlignment.Center
                            textStyle { color: Color.White; base: SystemDefaults.TextStyles.TitleText; fontWeight: FontWeight.Bold }
                            topMargin: 0; bottomMargin: 0
                        }
                    }
                }
            }

            actions: [
                ActionItem {
                    title: "Facebook"
                    imageSource: "asset:///images/ic_facebook.png"
                    ActionBar.placement: ActionBarPlacement.OnBar
                    onTriggered: { Qt.openUrlExternally("https://www.facebook.com/BBerrylife") }
                },
                ActionItem {
                    title: "Donate"
                    imageSource: "asset:///images/ic_scan_barcode.png"
                    ActionBar.placement: ActionBarPlacement.Signature
                    onTriggered: {
                        var donatePage = donatePageDef.createObject();
                        aboutNav.push(donatePage);
                    }
                },
                ActionItem {
                    title: "Website"
                    imageSource: "asset:///images/ic_sb_network.png"
                    ActionBar.placement: ActionBarPlacement.OnBar
                    onTriggered: { Qt.openUrlExternally("https://BBerryLife.github.io") }
                }
            ]

            ScrollView {
                Container {
                    horizontalAlignment: HorizontalAlignment.Fill
                    leftPadding: 50; rightPadding: 50; topPadding: 50; bottomPadding: 80

                    Container {
                        layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                        horizontalAlignment: HorizontalAlignment.Fill

                        Container {
                            layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                            verticalAlignment: VerticalAlignment.Center
                            Label { text: "Zalo10"; textStyle.base: SystemDefaults.TextStyles.BigText }
                            Label { text: "Version: " + app.appVersion(); textStyle.color: Color.Gray; topMargin: 4 }
                            Label { text: "Developed by BerryLife© 2026"; textStyle.color: Color.Gray; topMargin: 4 }
                        }

                        ImageView {
                            imageSource: "asset:///images/berrylife.png"
                            scalingMethod: ScalingMethod.AspectFit
                            preferredWidth:  160
                            preferredHeight: 160
                            verticalAlignment: VerticalAlignment.Center
                            horizontalAlignment: HorizontalAlignment.Right
                        }
                    }

                    Divider { topMargin: 30; bottomMargin: 20 }

                    Label {
                        text: "Zalo10 is a native Zalo client for BlackBerry 10.\nNo NodeJS, no browser required."
                        multiline: true
                        textStyle.color: Color.Gray
                        horizontalAlignment: HorizontalAlignment.Center
                        textStyle.textAlign: TextAlign.Center
                    }
                }
            }
        }

        attachedObjects: [
            ComponentDefinition {
                id: donatePageDef
                Page {
                    titleBar: TitleBar {
                        kind: TitleBarKind.FreeForm
                        kindProperties: FreeFormTitleBarKindProperties {
                            content: Container {
                                background: Color.create("#2575fc")
                                horizontalAlignment: HorizontalAlignment.Fill
                                verticalAlignment: VerticalAlignment.Fill
                                leftPadding: ui.du(2.5)
                                layout: DockLayout {}
                                Label {
                                    text: "Donate & Support"
                                    horizontalAlignment: HorizontalAlignment.Left
                                    verticalAlignment: VerticalAlignment.Center
                                    textStyle {
                                        color: Color.White
                                        fontWeight: FontWeight.Bold
                                        fontSize: FontSize.Large
                                    }
                                }
                            }
                        }
                    }

                    ScrollView {
                        Container {
                            horizontalAlignment: HorizontalAlignment.Fill
                            leftPadding: 30; rightPadding: 30; topPadding: 50; bottomPadding: 50

                            Label {
                                text: "If you find the app useful and are feeling generous, you can donate 10,000 VND to help me fund the purchase of BB10/BBOS devices for app testing, and... to support the app developer."
                                multiline: true
                                horizontalAlignment: HorizontalAlignment.Center
                                textStyle {
                                    base: SystemDefaults.TextStyles.BodyText
                                    textAlign: TextAlign.Center
                                    lineHeight: 1.3
                                }
                            }

                            Container {
                                topMargin: 50
                                horizontalAlignment: HorizontalAlignment.Center
                                preferredWidth: ui.du(40)
                                preferredHeight: ui.du(40)
                                background: Color.White
                                leftPadding: 10; rightPadding: 10; topPadding: 10; bottomPadding: 10

                                ImageView {
                                    imageSource: "asset:///images/barcode.png"
                                    scalingMethod: ScalingMethod.AspectFit
                                    horizontalAlignment: HorizontalAlignment.Fill
                                    verticalAlignment: VerticalAlignment.Fill
                                }
                            }

                            Label {
                                topMargin: 30
                                text: "Thank you for your support!"
                                horizontalAlignment: HorizontalAlignment.Center
                                textStyle {
                                    color: Color.DarkGray
                                    fontWeight: FontWeight.Bold
                                }
                            }
                        }
                    }
                }
            }
        ]
    }
}
