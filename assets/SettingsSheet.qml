import bb.cascades 1.4
import bb.system 1.0
import QtQuick 1.0

Sheet {
    id: settingsSheetRoot

    Page {
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
