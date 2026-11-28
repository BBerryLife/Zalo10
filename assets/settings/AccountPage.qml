import bb.cascades 1.4
import bb.system 1.0

Page {
    id: page
    property variant sheet
    titleBar: SettingsTitleBar { heading: qsTr("Account") }

    ScrollView {
        Container {
            horizontalAlignment: HorizontalAlignment.Fill
            SettingsButtonRow {
                text: qsTr("Log Out")
                description: qsTr("Signs this device out of your Zalo account.")
                onClicked: { logoutDialog.show(); }
            }
        }
    }

    attachedObjects: [
        SystemDialog {
            id: logoutDialog
            title: qsTr("Log Out")
            body: qsTr("Are you sure you want to log out?")
            confirmButton.label: qsTr("Log Out")
            cancelButton.label: qsTr("Cancel")
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
