import bb.cascades 1.4

Page {
    id: page
    property variant sheet
    titleBar: SettingsTitleBar { heading: "Notifications" }

    ScrollView {
        Container {
            horizontalAlignment: HorizontalAlignment.Fill
            SettingsToggleRow {
                id: hubSheetRow
                title: "Reply Card in Hub"
                description: "\"Reply\" on a message in BlackBerry Hub opens a small chat card over the Hub, without going into the app: read the latest messages and answer, or press Open to go to the conversation. Tapping a message opens the card too. When this is off, tapping a message and \"Reply\" open the app. The \"Reply\" menu entry itself is registered at start-up, so it follows this setting after restarting Zalo10."
                onUserToggled: { zService.setHubSheetEnabled(value); }
            }
            SettingsToggleRow {
                id: feedsRow
                title: "Feeds Notifications"
                description: "Show a \"Zalo10 Feeds\" notification in BlackBerry Hub when a contact changes their profile picture, cover photo, bio or birthday. Changes still appear in the Feeds tab when this is off."
                onUserToggled: { zService.setFeedsNotifyEnabled(value); }
            }
        }
    }

    onCreationCompleted: {
        hubSheetRow.checked = zService.hubSheetEnabled();
        feedsRow.checked = zService.feedsNotifyEnabled();
    }
}
