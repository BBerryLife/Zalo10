import bb.cascades 1.4
import bb.system 1.0
import QtQuick 1.0

// Create Note dialog for the Group Board, opened from GroupBoardSheet.qml's
// "Create note" action-bar item. Layout follows the reference screenshot
// (Zalo's own web UI "Create note" dialog): title + close X, a "Content"
// labeled multiline text area with hint "Enter content or paste link", a
// "Pin conversation note" toggle, Cancel/Create note buttons at the bottom.
// Styled after QuickMessageEditSheet.qml's header/body conventions (same
// FreeForm titleBar-with-close-X pattern, same field-container styling).
Sheet {
    id: createNoteSheetRoot

    property string groupId: ""
    property bool isDark: app.getDarkTheme()

    function resetFields() {
        noteContentArea.text = "";
        pinNoteToggle.checked = false;
    }

    function doCreate() {
        var content = noteContentArea.text.trim();
        if (content.length === 0) {
            noteValidationToast.body = "Please enter some content for the note.";
            noteValidationToast.show();
            return;
        }
        zService.createGroupNote(createNoteSheetRoot.groupId, content, pinNoteToggle.checked);
        createNoteSheetRoot.close();
    }

    onOpened: { resetFields(); }

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
                        onClicked: { createNoteSheetRoot.close(); }
                    }

                    Label {
                        text: "Create note"
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
                title: "Create note"
                imageSource: "asset:///images/ChatView/ic_save.png"
                ActionBar.placement: ActionBarPlacement.OnBar
                onTriggered: { createNoteSheetRoot.doCreate(); }
            }
        ]

        ScrollView {
            Container {
                horizontalAlignment: HorizontalAlignment.Fill
                topPadding: 14

                Container {
                    horizontalAlignment: HorizontalAlignment.Fill
                    leftPadding: 20; rightPadding: 20; topPadding: 6

                    Label {
                        text: "Content"
                        textStyle { color: Color.Gray; fontWeight: FontWeight.Bold }
                        bottomMargin: 6
                    }

                    TextArea {
                        id: noteContentArea
                        hintText: "Enter content or paste link"
                        horizontalAlignment: HorizontalAlignment.Fill
                        minHeight: 220; preferredHeight: 240
                    }
                }

                Container {
                    layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                    horizontalAlignment: HorizontalAlignment.Fill
                    leftPadding: 20; rightPadding: 20; topPadding: 16; bottomPadding: 20

                    Label {
                        text: "Pin conversation note"
                        layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                        verticalAlignment: VerticalAlignment.Center
                    }
                    ToggleButton {
                        id: pinNoteToggle
                        checked: false
                    }
                }
            }
        }

        attachedObjects: [
            SystemToast { id: noteValidationToast }
        ]
    }
}
