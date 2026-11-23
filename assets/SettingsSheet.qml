import bb.cascades 1.4
import QtQuick 1.0
import "settings"

// Settings, laid out like CrackBerry's: a list of categories (icon, title, one line), each opening a page with its
// own options inside the sheet's navigation pane. About stays where it was (the Info entry), not in here.
Sheet {
    id: settingsSheetRoot

    // Opens a category page (one fresh page per visit, so its switches always show the stored values).
    function openCategory(def) {
        var p = def.createObject();
        if (!p) return;
        p.sheet = settingsSheetRoot;
        settingsNav.push(p);
    }

    NavigationPane {
        id: settingsNav

        Page {
            id: settingsPage
            titleBar: SettingsTitleBar {
                heading: "Settings"
                closable: true
                onCloseRequested: { settingsSheetRoot.close(); }
            }

            ScrollView {
                scrollViewProperties.scrollMode: ScrollMode.Vertical
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment: VerticalAlignment.Fill

                Container {
                    horizontalAlignment: HorizontalAlignment.Fill

                    StandardListItem {
                        imageSource: "asset:///images/SettingsSheet/ic_sb_settings_active.png"
                        title: "General"
                        description: "Recalled messages"
                        gestureHandlers: TapHandler { onTapped: { settingsSheetRoot.openCategory(generalDef); } }
                    }
                    StandardListItem {
                        imageSource: "asset:///images/SettingsSheet/ic_picture.png"
                        title: "Appearance"
                        description: "Dark theme"
                        gestureHandlers: TapHandler { onTapped: { settingsSheetRoot.openCategory(appearanceDef); } }
                    }
                    StandardListItem {
                        imageSource: "asset:///images/SettingsSheet/ca_notification.png"
                        title: "Notifications"
                        description: "Feeds notifications in BlackBerry Hub"
                        gestureHandlers: TapHandler { onTapped: { settingsSheetRoot.openCategory(notificationsDef); } }
                    }
                    StandardListItem {
                        imageSource: "asset:///images/SettingsSheet/ca_data_management.png"
                        title: "Data & Backup"
                        description: "Export, import and sync messages"
                        gestureHandlers: TapHandler { onTapped: { settingsSheetRoot.openCategory(dataDef); } }
                    }
                    StandardListItem {
                        imageSource: "asset:///images/SettingsSheet/ca_storage_access.png"
                        title: "Cache Control"
                        description: "Cached photos and local message history"
                        gestureHandlers: TapHandler { onTapped: { settingsSheetRoot.openCategory(cacheDef); } }
                    }
                    StandardListItem {
                        imageSource: "asset:///images/SettingsSheet/ca_softwarelogtool.png"
                        title: "Support"
                        description: "Export the log file"
                        gestureHandlers: TapHandler { onTapped: { settingsSheetRoot.openCategory(supportDef); } }
                    }
                    StandardListItem {
                        imageSource: "asset:///images/SettingsSheet/ca_accounts.png"
                        title: "Account"
                        description: "Log out"
                        gestureHandlers: TapHandler { onTapped: { settingsSheetRoot.openCategory(accountDef); } }
                    }
                }
            }

            attachedObjects: [
                ComponentDefinition { id: generalDef;       source: "asset:///settings/GeneralPage.qml" },
                ComponentDefinition { id: appearanceDef;    source: "asset:///settings/AppearancePage.qml" },
                ComponentDefinition { id: notificationsDef; source: "asset:///settings/NotificationsPage.qml" },
                ComponentDefinition { id: dataDef;          source: "asset:///settings/DataPage.qml" },
                ComponentDefinition { id: cacheDef;         source: "asset:///settings/CachePage.qml" },
                ComponentDefinition { id: supportDef;       source: "asset:///settings/SupportPage.qml" },
                ComponentDefinition { id: accountDef;       source: "asset:///settings/AccountPage.qml" }
            ]
        }
    }
}
