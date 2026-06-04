import bb.cascades 1.4
import bb.system 1.0
import QtQuick 1.0

Sheet {
    id: settingsSheetRoot

    Page {
        titleBar: TitleBar {
            title: "Settings"
            dismissAction: ActionItem {
                title: "Close"
                onTriggered: { settingsSheetRoot.close() }
            }
        }

        onCreationCompleted: {
            // Load saved value mỗi khi sheet được tạo/hiện
            darkToggle.checked = app.getDarkTheme();
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
                        // checked được set trong onCreationCompleted, không dùng binding tĩnh
                        onCheckedChanged: {
                            // Lưu ngay khi toggle — gọi setDarkTheme áp dụng + lưu cả hai
                            app.setDarkTheme(checked);
                        }
                    }
                }

                Label {
                    text: "Theme change requires app restart to take full effect."
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
