import bb.cascades 1.4
import bb.system 1.0

Page {
    id: page
    property variant sheet
    titleBar: SettingsTitleBar { heading: "Support" }

    ScrollView {
        Container {
            horizontalAlignment: HorizontalAlignment.Fill
            SettingsButtonRow {
                text: "Export Log"
                description: "If the app has a problem, please export the log and email it to us — don't worry, we don't care about your message data and couldn't do anything with it even if we wanted to."
                onClicked: {
                    var path = app.exportLog();
                    exportToast.body = (path && path.length > 0)
                        ? "Log saved to " + path
                        : "Could not export log. Try again after using the app a bit.";
                    exportToast.show();
                }
            }
        }
    }

    attachedObjects: [
        SystemToast { id: exportToast }
    ]
}
