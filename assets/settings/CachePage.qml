import bb.cascades 1.4
import bb.system 1.0

Page {
    id: page
    property variant sheet
    titleBar: SettingsTitleBar { heading: qsTr("Cache Control") }

    ScrollView {
        Container {
            horizontalAlignment: HorizontalAlignment.Fill
            SettingsButtonRow {
                text: qsTr("Clear Cache")
                description: qsTr("Deletes cached photos and your local message history to free up space. Conversations will simply re-download from Zalo's servers next time you open them, but anything already removed from the server (like recalled or expired media) can't come back.")
                onClicked: { clearCacheDialog.show(); }
            }
        }
    }

    attachedObjects: [
        SystemDialog {
            id: clearCacheDialog
            title: qsTr("Clear Cache")
            body: qsTr("This deletes all cached photos and your local message history on this device. Already-sent messages aren't affected on Zalo's servers and conversations will reload normally — but anything no longer available there (recalled or expired media) will be gone for good. Continue?")
            confirmButton.label: qsTr("Clear Cache")
            cancelButton.label: qsTr("Cancel")
            onFinished: {
                if (result !== SystemUiResult.ConfirmButtonSelection) return;

                clearCacheProgressToast.body = qsTr("Clearing cache…");
                clearCacheProgressToast.show();

                var deletedCount = zService.clearCache();

                clearCacheProgressToast.cancel();

                clearCacheDoneToast.body = qsTr("Cache cleared. Removed ") + deletedCount + qsTr(" cached file(s).");
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
        }
    ]
}
