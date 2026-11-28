import bb.cascades 1.4

// Blue title bar of the Settings sheet. The first page (closable) carries the close button, the sub pages
// only the heading (their way back is the navigation pane's back button).
TitleBar {
    id: bar
    property string heading: qsTr("Settings")
    property bool   closable: false
    signal closeRequested()

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
                visible: bar.closable
                verticalAlignment: VerticalAlignment.Center
                preferredWidth:  ui.du(6); preferredHeight: ui.du(6)
                defaultImageSource: "asset:///images/SettingsSheet/ic_close_white.png"
                pressedImageSource: "asset:///images/SettingsSheet/ic_close_white.png"
                rightMargin: ui.du(0.5)
                onClicked: { bar.closeRequested() }
            }

            Label {
                text: bar.heading
                layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                verticalAlignment: VerticalAlignment.Center
                leftMargin: bar.closable ? 0 : ui.du(1.5)
                textStyle { color: Color.White; base: SystemDefaults.TextStyles.TitleText; fontWeight: FontWeight.Bold }
                topMargin: 0; bottomMargin: 0
            }
        }
    }
}
