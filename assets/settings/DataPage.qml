import bb.cascades 1.4
import bb.system 1.0
import bb.cascades.pickers 1.0
import QtQuick 1.0   // Connections lives here; without it this page fails to load

Page {
    id: page
    property variant sheet
    titleBar: SettingsTitleBar { heading: "Data & Backup" }

    // Conversations a phone sync could not match automatically (zService.phoneSyncUnmatched()).
    property int unmatchedCount: 0
    function refreshUnmatched() {
        page.unmatchedCount = zService.phoneSyncUnmatched().length;
    }
    function openMatch() {
        var p = matchDef.createObject();
        if (!p) return;
        p.dataPage = page;
        page.sheet.pushPage(p);
    }
    onCreationCompleted: { page.refreshUnmatched(); }

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
                    + "Text, photos, files, links, calls and stickers are imported; voice notes and locations appear as a short label."
                onClicked: { phoneSyncConfirm.show(); }
            }
            SettingsButtonRow {
                visible: page.unmatchedCount > 0
                text: "Match Conversations (" + page.unmatchedCount + ")"
                description: "Conversations from your phone that could not be matched automatically. Choose the friend or group each one belongs to and its messages are imported."
                onClicked: { page.openMatch(); }
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

        ComponentDefinition { id: matchDef; source: "asset:///settings/PhoneSyncMatchPage.qml" },

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
                phoneSyncProgressDialog.hiddenByUser = false;
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
            // System dialogs only repaint changed properties on show(); see onPhoneSyncProgress.
            property bool hiddenByUser: false
            property int  lastPct: -1
            property variant lastShownMs: 0
            onFinished: {
                if (result === SystemUiResult.CancelButtonSelection) zService.cancelPhoneSync();
                else if (result === SystemUiResult.ConfirmButtonSelection) phoneSyncProgressDialog.hiddenByUser = true;
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
                // A visible SystemProgressDialog does not repaint property changes until show() is called.
                // Progress can arrive every ~30 ms while importing: repaint only on a new percent or after 1 s.
                var now = new Date().getTime();
                var d = phoneSyncProgressDialog;
                if (percent === d.lastPct && now - d.lastShownMs < 1000) return;
                d.lastPct = percent;
                d.lastShownMs = now;
                d.progress = percent;
                d.statusMessage = percent + "% · " + text;
                if (!d.hiddenByUser) d.show();
            }
            onPhoneSyncFinished: {
                page.refreshUnmatched();
                phoneSyncProgressDialog.cancel();
                phoneSyncDoneToast.body = message;
                phoneSyncDoneToast.show();
            }
        }
    ]
}
