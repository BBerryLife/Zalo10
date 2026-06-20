import bb.cascades 1.4
import bb.system 1.0
import QtQuick 1.0

// Add / Edit form for a single Quick Message. Opened on top of
// QuickMessagesSheet.qml. editId < 0 means "creating a new one";
// editId >= 0 means "editing the quick message with that id".
//
// Header styled after AboutSheet.qml: ImageButton close on left, title label.
// Save action moved to action bar with ic_save.png icon.
Sheet {
    id: qmEditSheetRoot

    property int  editId: -1
    property bool isDark: app.getDarkTheme()

    function resetFields() {
        qmNameField.text = "";
        qmContentArea.text = "";
    }

    function prefillFromId(qid) {
        var list = zService.getQuickMessages();
        for (var i = 0; i < list.length; i++) {
            if (list[i].id === qid) {
                qmNameField.text = list[i].name;
                qmContentArea.text = list[i].content;
                return;
            }
        }
    }

    function doSave() {
        var nm = qmNameField.text.trim().replace(/^\/+/, "").replace(/\s+/g, "_");
        var ct = qmContentArea.text.trim();

        if (nm.length === 0 || ct.length === 0) {
            qmValidationToast.body = "Please enter both a shortcut and a message.";
            qmValidationToast.show();
            return;
        }

        var ok;
        if (qmEditSheetRoot.editId < 0) {
            ok = zService.addQuickMessage(nm, ct) >= 0;
        } else {
            ok = zService.updateQuickMessage(qmEditSheetRoot.editId, nm, ct);
        }

        if (!ok) {
            qmValidationToast.body = "A quick message with that shortcut already exists.";
            qmValidationToast.show();
            return;
        }

        qmEditSheetRoot.close();
    }

    onOpened: {
        if (qmEditSheetRoot.editId < 0) resetFields();
    }

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
                        defaultImageSource: "asset:///images/AboutSheet/ic_close_white.png"
                        pressedImageSource: "asset:///images/AboutSheet/ic_close_white.png"
                        rightMargin: ui.du(0.5)
                        onClicked: { qmEditSheetRoot.close(); }
                    }

                    Label {
                        text: qmEditSheetRoot.editId < 0 ? "New Quick Message" : "Edit Quick Message"
                        layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                        verticalAlignment: VerticalAlignment.Center
                        textStyle { color: Color.White; base: SystemDefaults.TextStyles.TitleText; fontWeight: FontWeight.Bold }
                        topMargin: 0; bottomMargin: 0
                    }
                }
            }
        }

        actions: [
            ActionItem {
                title: "Save"
                imageSource: "asset:///images/ChatView/ic_save.png"
                ActionBar.placement: ActionBarPlacement.OnBar
                onTriggered: { qmEditSheetRoot.doSave(); }
            }
        ]

        ScrollView {
            Container {
                horizontalAlignment: HorizontalAlignment.Fill
                topPadding: 14

                Label {
                    text: "Create a QM by filling out the forms below"
                    multiline: true; textStyle.color: Color.Gray
                    horizontalAlignment: HorizontalAlignment.Center
                    textStyle.textAlign: TextAlign.Center
                    leftMargin: 20; rightMargin: 20
                }

                Container { preferredHeight: 12 }

                Container {
                    horizontalAlignment: HorizontalAlignment.Fill
                    leftPadding: 20; rightPadding: 20; bottomPadding: 10

                    Container {
                        layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                        horizontalAlignment: HorizontalAlignment.Fill
                        background: qmEditSheetRoot.isDark ? Color.create("#272727") : Color.create("#f0f0f0")

                        Container {
                            preferredWidth: ui.du(7); minWidth: ui.du(7)
                            preferredHeight: ui.du(7); minHeight: ui.du(7)
                            topMargin: 6; bottomMargin: 6; leftMargin: 6
                            background: qmEditSheetRoot.isDark ? Color.create("#3a3a3a") : Color.create("#e2e2e2")
                            layout: DockLayout {}
                            Label {
                                text: "/"
                                horizontalAlignment: HorizontalAlignment.Center
                                verticalAlignment: VerticalAlignment.Center
                                textStyle { color: Color.Gray; fontWeight: FontWeight.Bold }
                            }
                        }

                        TextField {
                            id: qmNameField
                            hintText: "Shortcut (Eg: hello)"
                            backgroundVisible: false
                            verticalAlignment: VerticalAlignment.Center
                            layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                            input {
                                flags: TextInputFlag.AutoCapitalizationOff | TextInputFlag.SpellCheckOff | TextInputFlag.PredictionOff
                            }
                        }
                    }
                }

                Container {
                    horizontalAlignment: HorizontalAlignment.Fill
                    leftPadding: 20; rightPadding: 20; topPadding: 10; bottomPadding: 20

                    TextArea {
                        id: qmContentArea
                        hintText: "Enter your message"
                        horizontalAlignment: HorizontalAlignment.Fill
                        minHeight: 200; preferredHeight: 220
                    }
                }
            }
        }

        attachedObjects: [
            SystemToast { id: qmValidationToast }
        ]
    }
}
