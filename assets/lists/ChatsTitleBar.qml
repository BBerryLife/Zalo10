import bb.cascades 1.4
import bb.system 1.0
import QtQuick 1.0

TitleBar {
    kind: TitleBarKind.FreeForm
    kindProperties: FreeFormTitleBarKindProperties {
        content: Container {
            background: Color.create("#2575fc")
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment: VerticalAlignment.Fill
            layout: DockLayout {}
            leftPadding: ui.du(2.5)
            rightPadding: ui.du(2.5)
            Label {
                text: "Zalo10"
                textStyle {
                    color: Color.White
                    base: SystemDefaults.TextStyles.TitleText
                    fontWeight: FontWeight.Bold
                }
                verticalAlignment: VerticalAlignment.Center
                horizontalAlignment: HorizontalAlignment.Left
                visible: !chatsNav.searchVisible
            }
            Container {
                // Header buttons: Search sits to the left of New chat (New chat is the outermost one).
                visible: !chatsNav.searchVisible
                horizontalAlignment: HorizontalAlignment.Right
                verticalAlignment: VerticalAlignment.Center
                layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                ImageButton {
                    defaultImageSource: "asset:///images/ChatsTab/action_icon_search.png"
                    preferredWidth: ui.du(7)
                    preferredHeight: ui.du(7)
                    verticalAlignment: VerticalAlignment.Center
                    onClicked: {
                        chatsNav.searchVisible = true;
                        chatsSearchField.requestFocus();
                    }
                }
                ImageButton {
                    defaultImageSource: "asset:///images/ChatsTab/ic_start_chat.png"
                    preferredWidth: ui.du(8)
                    preferredHeight: ui.du(8)
                    verticalAlignment: VerticalAlignment.Center
                    onClicked: newChatDialog.show()
                }
                attachedObjects: [
                    SystemDialog {
                        id: newChatDialog
                        title: "New Chat"
                        body: "This feature is still under development."
                        confirmButton.label: "OK"
                        cancelButton.label: ""
                        cancelButton.enabled: false
                    }
                ]
            }
            Container {
                visible: chatsNav.searchVisible
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment: VerticalAlignment.Center
                layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                TextField {
                    id: chatsSearchField
                    hintText: "Search chats..."
                    verticalAlignment: VerticalAlignment.Center
                    textStyle { color: Color.White }
                    layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                    onTextChanging: {
                        chatsNav.searchText = text;
                        chatsNav.filterList();
                    }
                    onCreationCompleted: {
                        inputMode.type = TextInputFlag.AutoCapitalizationOff | TextInputFlag.AutoCorrectionOff | TextInputFlag.SpellCheckOff | TextInputFlag.PredictionOff;
                    }
                }
                Button {
                    text: "Cancel"
                    preferredWidth: ui.du(14)
                    verticalAlignment: VerticalAlignment.Center
                    onClicked: {
                        chatsSearchField.text = "";
                        chatsNav.searchText = "";
                        chatsNav.searchVisible = false;
                        chatsNav.filterList();
                    }
                }
            }
        }
    }
}
