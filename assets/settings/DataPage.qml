import bb.cascades 1.4
import bb.system 1.0
import bb.cascades.pickers 1.0
import QtQuick 1.0   // Connections lives here; without it this page fails to load

Page {
    id: page
    property variant sheet
    titleBar: SettingsTitleBar { heading: "Data & Backup" }

    function doExport() {
        exportProgressToast.body = "Exporting data…";
        exportProgressToast.show();

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

            SettingsButtonRow {
                text: "Export Data"
                description: "Saves your message history (text only — photos are not included) to /accounts/1000/shared/documents/zalo10 as a JSON file you can keep as a backup or move to another device."
                onClicked: { page.doExport(); }
            }
            SettingsButtonRow {
                text: "Import Data"
                description: "Restores message history from a previously exported file. Conversations you already have are left untouched — only new messages are added."
                onClicked: { importFilePicker.open(); }
            }
            SettingsButtonRow {
                text: "Sync Messages from Phone"
                description: "Brings older messages from the Zalo app on your phone into Zalo10. Your phone will ask you to confirm. "
                    + "Conversations are matched using messages Zalo10 has already received, so run it again after a few days of use to bring in more. "
                    + "Only text messages are imported (photos, stickers and files are not)."
                onClicked: { phoneSyncConfirm.show(); }
            }
        }
    }

    attachedObjects: [
        SystemProgressToast {
            id: exportProgressToast
            progress: -1
            state: SystemUiProgressState.Active
        },
        SystemToast {
            id: exportDoneToast
            button.label: "OK"
            button.enabled: true
        },

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

        SystemDialog {
            id: phoneSyncConfirm
            title: "Sync Messages from Phone"
            body: "Your phone's Zalo app will ask you to confirm. Keep Zalo10 open until the sync finishes."
            confirmButton.label: "Start"
            cancelButton.label: "Cancel"
            onFinished: {
                if (result !== SystemUiResult.ConfirmButtonSelection) return;
                phoneSyncProgressDialog.progress = 0;
                phoneSyncProgressDialog.statusMessage = "Starting…";
                phoneSyncProgressDialog.show();
                zService.startPhoneSync();
            }
        },
        SystemProgressDialog {
            id: phoneSyncProgressDialog
            title: "Syncing from phone"
            progress: 0
            state: SystemUiProgressState.Active
            confirmButton.label: "Hide"
            cancelButton.label: "Stop"
            onFinished: {
                if (result === SystemUiResult.CancelButtonSelection) zService.cancelPhoneSync();
            }
        },
        SystemToast {
            id: phoneSyncDoneToast
            button.label: "OK"
            button.enabled: true
        },
        Connections {
            target: zService
            onPhoneSyncProgress: {
                phoneSyncProgressDialog.progress = percent;
                phoneSyncProgressDialog.statusMessage = text;
            }
            onPhoneSyncFinished: {
                phoneSyncProgressDialog.cancel();
                phoneSyncDoneToast.body = message;
                phoneSyncDoneToast.show();
            }
        }
    ]
}
