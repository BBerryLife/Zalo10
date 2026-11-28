import bb.cascades 1.4

Page {
    id: page
    property variant sheet
    titleBar: SettingsTitleBar { heading: qsTr("Appearance") }

    ScrollView {
        Container {
            horizontalAlignment: HorizontalAlignment.Fill
            SettingsToggleRow {
                id: darkRow
                title: qsTr("Dark Theme")
                description: qsTr("Enabling dark mode at night reduces eye strain and saves battery on OLED displays.")
                onUserToggled: { app.setDarkTheme(value); }
            }
        }
    }

    onCreationCompleted: { darkRow.checked = app.getDarkTheme(); }
}
