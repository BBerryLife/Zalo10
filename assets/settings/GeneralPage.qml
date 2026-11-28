import bb.cascades 1.4

Page {
    id: page
    property variant sheet
    titleBar: SettingsTitleBar { heading: qsTr("General") }

    ScrollView {
        Container {
            horizontalAlignment: HorizontalAlignment.Fill
            // Language names stay in their own language; only "System default" is translated.
            DropDown {
                id: languageDrop
                title: qsTr("Language")
                property bool ready: false
                Option { text: qsTr("System default"); value: "system" }
                Option { text: "English"; value: "en" }
                Option { text: "Ti\u1EBFng Vi\u1EC7t"; value: "vi" }
                onSelectedOptionChanged: {
                    if (ready && selectedOption) app.setLanguage(selectedOption.value);
                }
            }
            SettingsToggleRow {
                id: recalledRow
                title: qsTr("Show Recalled Messages")
                description: qsTr("Zalo10 exclusive: when someone recalls a message, keep showing what they originally sent with a \"(This message was recalled)\" tag instead of hiding it.")
                onUserToggled: { app.setShowRecalledMessages(value); }
            }
        }
    }

    onCreationCompleted: {
        recalledRow.checked = app.getShowRecalledMessages();
        var lang = app.getLanguage();
        for (var i = 0; i < languageDrop.count(); i++) {
            if (languageDrop.at(i).value === lang) { languageDrop.selectedIndex = i; break; }
        }
        languageDrop.ready = true;
    }
}
