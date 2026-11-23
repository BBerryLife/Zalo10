import bb.cascades 1.4

// A full-width button with its explanation under it.
Container {
    id: row
    property string text: ""
    property string description: ""
    signal clicked()

    horizontalAlignment: HorizontalAlignment.Fill
    topPadding: ui.du(2); bottomPadding: ui.du(2)
    leftPadding: ui.du(2); rightPadding: ui.du(2)

    Button {
        text: row.text
        horizontalAlignment: HorizontalAlignment.Fill
        onClicked: { row.clicked(); }
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
