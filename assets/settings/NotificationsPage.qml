import bb.cascades 1.4

Page {
    id: page
    property variant sheet
    titleBar: SettingsTitleBar { heading: "Notifications" }

    ScrollView {
        Container {
            horizontalAlignment: HorizontalAlignment.Fill
            SettingsToggleRow {
                id: feedsRow
                title: "Feeds Notifications"
                description: "Show a \"Zalo10 Feeds\" notification in BlackBerry Hub when a contact changes their profile picture, cover photo, bio or birthday. Changes still appear in the Feeds tab when this is off."
                onUserToggled: { zService.setFeedsNotifyEnabled(value); }
            }
        }
    }

    onCreationCompleted: {
        feedsRow.checked = zService.feedsNotifyEnabled();
    }
}
