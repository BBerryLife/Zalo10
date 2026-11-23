import bb.cascades 1.4

Page {
    id: page
    property variant sheet
    titleBar: SettingsTitleBar { heading: "General" }

    ScrollView {
        Container {
            horizontalAlignment: HorizontalAlignment.Fill
            SettingsToggleRow {
                id: recalledRow
                title: "Show Recalled Messages"
                description: "Zalo10 exclusive: when someone recalls a message, keep showing what they originally sent with a \"(This message was recalled)\" tag instead of hiding it."
                onUserToggled: { app.setShowRecalledMessages(value); }
            }
        }
    }

    onCreationCompleted: { recalledRow.checked = app.getShowRecalledMessages(); }
}
