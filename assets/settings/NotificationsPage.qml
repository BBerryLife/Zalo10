import bb.cascades 1.4

Page {
    id: page
    property variant sheet
    titleBar: SettingsTitleBar { heading: qsTr("Notifications") }

    ScrollView {
        Container {
            horizontalAlignment: HorizontalAlignment.Fill
            SettingsToggleRow {
                id: feedsRow
                title: qsTr("Feeds Notifications")
                description: qsTr("Show a \"Zalo10 Feeds\" notification in BlackBerry Hub when a contact changes their profile picture, cover photo, bio or birthday. Changes still appear in the Feeds tab when this is off.")
                onUserToggled: { zService.setFeedsNotifyEnabled(value); }
            }
        }
    }

    onCreationCompleted: {
        feedsRow.checked = zService.feedsNotifyEnabled();
    }
}
