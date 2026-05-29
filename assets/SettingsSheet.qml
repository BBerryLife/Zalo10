import bb.cascades 1.4
import bb.system 1.0
import bb.cascades.pickers 1.0
import QtQuick 1.0

Sheet {
    id: settingsSheetRoot

    Page {
        id: settingsPage
        titleBar: TitleBar {
            scrollBehavior: TitleBarScrollBehavior.Sticky
            kind: TitleBarKind.FreeForm
            kindProperties: FreeFormTitleBarKindProperties {
                content: Container {
                    background: Color.create("#2575fc")
                    horizontalAlignment: HorizontalAlignment.Fill
                    verticalAlignment:   VerticalAlignment.Fill
                    layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                    leftPadding: ui.du(1)

                    ImageButton {
                        verticalAlignment: VerticalAlignment.Center
                        preferredWidth:  ui.du(6); preferredHeight: ui.du(6)
                        defaultImageSource: "asset:///images/SettingsSheet/ic_close_white.png"
                        pressedImageSource: "asset:///images/SettingsSheet/ic_close_white.png"
                        rightMargin: ui.du(0.5)
                        onClicked: { settingsSheetRoot.close() }
                    }

                    Label {
                        text: "Settings"
                        layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                        verticalAlignment: VerticalAlignment.Center
                        textStyle { color: Color.White; base: SystemDefaults.TextStyles.TitleText; fontWeight: FontWeight.Bold }
                        topMargin: 0; bottomMargin: 0
                    }
                }
            }
        }

        onCreationCompleted: {
            darkToggle.checked = app.getDarkTheme();
            showRecalledToggle.checked = app.getShowRecalledMessages();
        }

        function doExport() {
            exportProgressToast.body = "Exporting data…";
            exportProgressToast.show();

            // exportData() runs synchronously but is fast, so a worker thread isn't
            // needed — the progress toast just covers the brief blocking window
            var result = zService.exportData("/accounts/1000/shared/documents");

            exportProgressToast.cancel();

            if (result && result.success) {
                exportDoneToast.body = "Exported " + result.messageCount + " message(s) to:\n" + result.path;
            } else {
                exportDoneToast.body = "Export failed" + (result && result.error ? (": " + result.error) : ".");
            }
            exportDoneToast.show();
        }

        ScrollView {
            Container {
                horizontalAlignment: HorizontalAlignment.Fill
                topPadding: 30; leftPadding: 30; rightPadding: 30; bottomPadding: 60

                Label {
                    text: "Appearance"
                    textStyle.base: SystemDefaults.TextStyles.TitleText
                }

                Container {
                    layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                    topMargin: 20
                    Label {
                        text: "Dark Theme"
                        layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                        verticalAlignment: VerticalAlignment.Center
                    }
                    ToggleButton {
                        id: darkToggle
                        onCheckedChanged: {
                            app.setDarkTheme(checked);
                        }
                    }
                }

                Label {
                    text: "Enabling dark mode at night reduces eye strain and saves battery on OLED displays."
                    multiline: true
                    textStyle.color: Color.Gray
                    topMargin: 6
                }

                Divider { topMargin: 30; bottomMargin: 20 }

                Label {
                    text: "Messages"
                    textStyle.base: SystemDefaults.TextStyles.TitleText
                }

                Container {
                    layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                    topMargin: 20
                    Label {
                        text: "Show Recalled Messages"
                        layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                        verticalAlignment: VerticalAlignment.Center
                    }
                    ToggleButton {
                        id: showRecalledToggle
                        onCheckedChanged: {
                            app.setShowRecalledMessages(checked);
                        }
                    }
                }

                Label {
                    text: "Zalo10 exclusive: when someone recalls a message, keep showing what they originally sent with a \"(This message was recalled)\" tag instead of hiding it."
                    multiline: true
                    textStyle.color: Color.Gray
                    topMargin: 6
                }

                Divider { topMargin: 30; bottomMargin: 20 }

                Label {
                    text: "Support"
                    textStyle.base: SystemDefaults.TextStyles.TitleText
                }

                Button {
                    text: "Export Log"
                    horizontalAlignment: HorizontalAlignment.Fill
                    topMargin: 20
                    onClicked: {
                        var path = app.exportLog();
                        exportToast.body = (path && path.length > 0)
                            ? "Log saved to " + path
                            : "Could not export log. Try again after using the app a bit.";
                        exportToast.show();
                    }
                }

                Label {
                    text: "If the app has a problem, please export the log and email it to us — don't worry, we don't care about your message data and couldn't do anything with it even if we wanted to."
                    multiline: true
                    textStyle.color: Color.Gray
                    topMargin: 6
                }

                Divider { topMargin: 30; bottomMargin: 20 }

                Label {
                    text: "Data"
                    textStyle.base: SystemDefaults.TextStyles.TitleText
                }

                Button {
                    text: "Export Data"
                    horizontalAlignment: HorizontalAlignment.Fill
                    topMargin: 20
                    onClicked: { settingsPage.doExport() }
                }

                Label {
                    text: "Saves your message history (text only — photos are not included) to " + "/accounts/1000/shared/documents/zalo10" + " as a JSON file you can keep as a backup or move to another device."
                    multiline: true
                    textStyle.color: Color.Gray
                    topMargin: 6
                }

                Button {
                    text: "Import Data"
                    horizontalAlignment: HorizontalAlignment.Fill
                    topMargin: 20
                    onClicked: { importFilePicker.open() }
                }

                Label {
                    text: "Restores message history from a previously exported file. Conversations you already have are left untouched — only new messages are added."
                    multiline: true
                    textStyle.color: Color.Gray
                    topMargin: 6
                }

                Button {
                    text: "Clear Cache"
                    horizontalAlignment: HorizontalAlignment.Fill
                    topMargin: 20
                    onClicked: { clearCacheDialog.show() }
                }

                Label {
                    text: "Deletes cached photos and your local message history to free up space. Conversations will simply re-download from Zalo's servers next time you open them, but anything already removed from the server (like recalled or expired media) can't come back."
                    multiline: true
                    textStyle.color: Color.Gray
                    topMargin: 6
                }

                Divider { topMargin: 30; bottomMargin: 20 }

                Label {
                    text: "Account"
                    textStyle.base: SystemDefaults.TextStyles.TitleText
                }

                Button {
                    text: "Log Out"
                    horizontalAlignment: HorizontalAlignment.Fill
                    topMargin: 20
                    onClicked: { logoutDialog.show() }
                }

                attachedObjects: [
                    SystemToast {
                        id: exportToast
                    },

                    // --- Export Data flow (3 system toasts total: progress while exporting,
                    // then a result toast with an OK button) -------------------------------
                    SystemProgressToast {
                        id: exportProgressToast
                        progress: -1 // indefinite — the work finishes well before any % would be meaningful
                        state: SystemUiProgressState.Active
                    },
                    SystemToast {
                        id: exportDoneToast
                        button.label: "OK"
                        button.enabled: true
                    },

                    // --- Import Data flow ---------------------------------------------------
                    FilePicker {
                        id: importFilePicker
                        type: FileType.Other
                        mode: FilePickerMode.Picker
                        title: "Select Zalo10 export (.json)"
                        filter: [ "*.json" ]
                        directories: [ "/accounts/1000/shared/documents/zalo10" ]
                        onFileSelected: {
                            var path = selectedFiles[0];
                            importProgressToast.body = "Importing data…";
                            importProgressToast.show();

                            var result = zService.importData(path);

                            importProgressToast.cancel();

                            if (result && result.success) {
                                importDoneToast.body =
                                    "Imported " + result.importedMessages + " message(s)"
                                    + (result.skippedMessages > 0 ? (" (" + result.skippedMessages + " already present, skipped)") : "")
                                    + ".";
                                if (result.importedQuickMessages > 0 || result.skippedQuickMessages > 0) {
                                    importDoneToast.body += "\nQuick messages: " + result.importedQuickMessages + " added"
                                        + (result.skippedQuickMessages > 0 ? (", " + result.skippedQuickMessages + " skipped (duplicate name)") : "")
                                        + ".";
                                }
                            } else {
                                importDoneToast.body = "Import failed" + (result && result.error ? (": " + result.error) : ".");
                            }
                            importDoneToast.show();
                        }
                    },
                    SystemProgressToast {
                        id: importProgressToast
                        progress: -1
                        state: SystemUiProgressState.Active
                    },
                    SystemToast {
                        id: importDoneToast
                        button.label: "OK"
                        button.enabled: true
                    },

                    // --- Clear Cache flow -----------------------------------------------------
                    SystemDialog {
                        id: clearCacheDialog
                        title: "Clear Cache"
                        body: "This deletes all cached photos and your local message history on this device. Already-sent messages aren't affected on Zalo's servers and conversations will reload normally — but anything no longer available there (recalled or expired media) will be gone for good. Continue?"
                        confirmButton.label: "Clear Cache"
                        cancelButton.label: "Cancel"
                        onFinished: {
                            if (result !== SystemUiResult.ConfirmButtonSelection) return;

                            clearCacheProgressToast.body = "Clearing cache…";
                            clearCacheProgressToast.show();

                            var deletedCount = zService.clearCache();

                            clearCacheProgressToast.cancel();

                            clearCacheDoneToast.body = "Cache cleared. Removed " + deletedCount + " cached file(s).";
                            clearCacheDoneToast.show();
                        }
                    },
                    SystemProgressToast {
                        id: clearCacheProgressToast
                        progress: -1
                        state: SystemUiProgressState.Active
                    },
                    SystemToast {
                        id: clearCacheDoneToast
                        button.label: "OK"
                        button.enabled: true
                    },

                    SystemDialog {
                        id: logoutDialog
                        title: "Log Out"
                        body: "Are you sure you want to log out?"
                        confirmButton.label: "Log Out"
                        cancelButton.label: "Cancel"
                        onFinished: {
                            if (result === SystemUiResult.ConfirmButtonSelection) {
                                zService.logout();
                                settingsSheetRoot.close();
                                loginSheet.open();
                            }
                        }
                    }
                ]
            }
        }
    }
}
