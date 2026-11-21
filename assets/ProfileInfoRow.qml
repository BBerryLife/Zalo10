import bb.cascades 1.4

// One "label / value" line of a profile; hidden while the value is empty.
Container {
    property string label: ""
    property string value: ""

    visible: value.length > 0
    leftPadding: ui.du(3)
    rightPadding: ui.du(3)
    topPadding: ui.du(1)

    Label {
        text: label
        textStyle { base: SystemDefaults.TextStyles.SmallText; color: Color.Gray }
    }
    Label {
        text: value
        multiline: true
        textStyle { base: SystemDefaults.TextStyles.BodyText }
        topMargin: ui.du(0.3)
        bottomMargin: ui.du(1)
    }
}
