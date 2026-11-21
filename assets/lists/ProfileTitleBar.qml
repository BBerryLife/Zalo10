import bb.cascades 1.4
import bb.system 1.0
import QtQuick 1.0

// Shared header of the four main tabs: the user's own avatar, name and a fixed "Available" line,
// styled like the ChatView header. Optional buttons on the right (search / add) and an optional
// header search mode (Chats and Invites keep their old search-in-header behaviour).
TitleBar {
    id: profileBar
    kind: TitleBarKind.FreeForm

    property string selfName: ""
    property string selfAvatar: ""
    property string searchHint: "Search..."
    property bool   searchMode: false          // true: the whole header becomes a search box
    property bool   showSearchButton: false    // magnifier on the right (opens header search mode)
    property string addIcon: ""                // asset path of the add button; empty = no button
    property string addDialogTitle: ""         // "under development" dialog shown by the add button
    property bool   addHandled: false          // true: the add button emits addClicked() instead of the dialog

    signal searchOpened()
    signal searchChanged(string text)
    signal searchClosed()
    signal addClicked()

    function focusSearch() { profileSearchField.requestFocus(); }
    function clearSearch()  { profileSearchField.text = ""; }

    kindProperties: FreeFormTitleBarKindProperties {
        content: Container {
            background: Color.create("#2575fc")
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment:   VerticalAlignment.Fill
            layout: DockLayout {}

            Container {
                visible: !profileBar.searchMode
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment:   VerticalAlignment.Fill
                layout: StackLayout { orientation: LayoutOrientation.LeftToRight }

                // Avatar + name + status: tapping them is "under development" like every other profile action.
                Container {
                    horizontalAlignment: HorizontalAlignment.Fill
                    verticalAlignment:   VerticalAlignment.Fill
                    layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                    layoutProperties: StackLayoutProperties { spaceQuota: 1 }

                    Container {
                        verticalAlignment: VerticalAlignment.Fill
                        layout: DockLayout {}
                        preferredWidth: profileLUH.layoutFrame.height > 0 ? profileLUH.layoutFrame.height : ui.du(7)
                        minWidth:       profileLUH.layoutFrame.height > 0 ? profileLUH.layoutFrame.height : ui.du(7)

                        ImageView {
                            horizontalAlignment: HorizontalAlignment.Fill
                            verticalAlignment:   VerticalAlignment.Fill
                            scalingMethod: ScalingMethod.AspectFill
                            imageSource: profileBar.selfAvatar
                            visible: profileBar.selfAvatar.length > 0
                        }
                        Container {
                            horizontalAlignment: HorizontalAlignment.Fill
                            verticalAlignment:   VerticalAlignment.Fill
                            background: Color.create("#1a5fc8")
                            layout: DockLayout {}
                            visible: profileBar.selfAvatar.length === 0
                            Label {
                                text: profileBar.selfName.length > 0 ? profileBar.selfName.charAt(0).toUpperCase() : "?"
                                horizontalAlignment: HorizontalAlignment.Center
                                verticalAlignment:   VerticalAlignment.Center
                                textStyle { color: Color.White; fontSize: FontSize.XXLarge; fontWeight: FontWeight.Bold }
                            }
                        }
                        attachedObjects: [ LayoutUpdateHandler { id: profileLUH } ]
                    }

                    Container {
                        verticalAlignment: VerticalAlignment.Center
                        leftPadding: ui.du(1.5)
                        layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                        Label {
                            text: profileBar.selfName.length > 0 ? profileBar.selfName : "..."
                            textStyle { color: Color.White; base: SystemDefaults.TextStyles.TitleText; fontWeight: FontWeight.Bold }
                            topMargin: 0; bottomMargin: 0
                        }
                        Label {
                            text: "Available"
                            textStyle { color: Color.create("#b3d4ff"); fontSize: FontSize.XSmall }
                            topMargin: ui.du(0.2); bottomMargin: 0
                        }
                    }

                    onTouch: {
                        if (event.isUp()) profileDevDialog.show();
                    }
                }

                ImageButton {
                    visible: profileBar.showSearchButton
                    verticalAlignment: VerticalAlignment.Center
                    defaultImageSource: "asset:///images/ChatsTab/action_icon_search.png"
                    preferredWidth: ui.du(7)
                    preferredHeight: ui.du(7)
                    onClicked: {
                        profileBar.searchOpened();
                    }
                }
                ImageButton {
                    visible: profileBar.addIcon.length > 0
                    verticalAlignment: VerticalAlignment.Center
                    defaultImageSource: profileBar.addIcon
                    preferredWidth: ui.du(8)
                    preferredHeight: ui.du(8)
                    onClicked: {
                        if (profileBar.addHandled) profileBar.addClicked();
                        else addDevDialog.show();
                    }
                }
                Container { preferredWidth: ui.du(1.2) }
            }

            Container {
                visible: profileBar.searchMode
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment:   VerticalAlignment.Center
                leftPadding: ui.du(1.5)
                rightPadding: ui.du(1.5)
                layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                TextField {
                    id: profileSearchField
                    hintText: profileBar.searchHint
                    verticalAlignment: VerticalAlignment.Center
                    textStyle { color: Color.White }
                    layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                    onTextChanging: profileBar.searchChanged(text)
                    onCreationCompleted: {
                        inputMode.type = TextInputFlag.AutoCapitalizationOff | TextInputFlag.AutoCorrectionOff | TextInputFlag.SpellCheckOff | TextInputFlag.PredictionOff;
                    }
                }
                Button {
                    text: "Cancel"
                    preferredWidth: ui.du(14)
                    verticalAlignment: VerticalAlignment.Center
                    onClicked: {
                        profileSearchField.text = "";
                        profileBar.searchClosed();
                    }
                }
            }

            attachedObjects: [
                SystemDialog {
                    id: profileDevDialog
                    title: "Profile"
                    body: "This feature is still under development."
                    confirmButton.label: "OK"
                    cancelButton.label: ""
                    cancelButton.enabled: false
                },
                SystemDialog {
                    id: addDevDialog
                    title: profileBar.addDialogTitle
                    body: "This feature is still under development."
                    confirmButton.label: "OK"
                    cancelButton.label: ""
                    cancelButton.enabled: false
                }
            ]
        }
    }
}
