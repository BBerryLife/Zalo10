import bb.cascades 1.4

// A switch with its explanation under it. userToggled() fires on every change of the switch, also when a page
// sets `checked` while it opens (the receiving setters store the value they already have).
Container {
    id: row
    property string title: ""
    property string description: ""
    property alias  checked: toggle.checked
    signal userToggled(bool value)

    horizontalAlignment: HorizontalAlignment.Fill
    topPadding: ui.du(2); bottomPadding: ui.du(2)
    leftPadding: ui.du(2); rightPadding: ui.du(2)

    Container {
        horizontalAlignment: HorizontalAlignment.Fill
        layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
        Label {
            text: row.title
            layoutProperties: StackLayoutProperties { spaceQuota: 1 }
            verticalAlignment: VerticalAlignment.Center
            textStyle.base: SystemDefaults.TextStyles.PrimaryText
        }
        ToggleButton {
            id: toggle
            verticalAlignment: VerticalAlignment.Center
            onCheckedChanged: { row.userToggled(checked); }
        }
    }
    Label {
        visible: row.description.length > 0
        text: row.description
        multiline: true
        textStyle.color: Color.Gray
        textStyle.base: SystemDefaults.TextStyles.SmallText
        topMargin: ui.du(0.5)
    }
}
