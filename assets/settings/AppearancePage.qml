import bb.cascades 1.4

Page {
    id: page
    property variant sheet
    titleBar: SettingsTitleBar { heading: "Appearance" }

    ScrollView {
        Container {
            horizontalAlignment: HorizontalAlignment.Fill
            SettingsToggleRow {
                id: darkRow
                title: "Dark Theme"
                description: "Enabling dark mode at night reduces eye strain and saves battery on OLED displays."
                onUserToggled: { app.setDarkTheme(value); }
            }
        }
    }

    onCreationCompleted: { darkRow.checked = app.getDarkTheme(); }
}
