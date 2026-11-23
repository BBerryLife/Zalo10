import bb.cascades 1.4
import bb.system 1.0

Page {
    id: page
    property variant sheet
    titleBar: SettingsTitleBar { heading: "Account" }

    ScrollView {
        Container {
            horizontalAlignment: HorizontalAlignment.Fill
            SettingsButtonRow {
                text: "Log Out"
                description: "Signs this device out of your Zalo account."
                onClicked: { logoutDialog.show(); }
            }
        }
    }

    attachedObjects: [
        SystemDialog {
            id: logoutDialog
            title: "Log Out"
            body: "Are you sure you want to log out?"
            confirmButton.label: "Log Out"
            cancelButton.label: "Cancel"
            onFinished: {
                if (result === SystemUiResult.ConfirmButtonSelection) {
                    zService.logout();
                    if (page.sheet) page.sheet.close();
                    loginSheet.open();
                }
            }
        }
    ]
}
