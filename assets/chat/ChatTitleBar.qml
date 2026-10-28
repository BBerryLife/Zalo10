import bb.cascades 1.4
import bb.system 1.0
import QtQuick 1.0

TitleBar {
    scrollBehavior: TitleBarScrollBehavior.Sticky
    kind: TitleBarKind.FreeForm
    kindProperties: FreeFormTitleBarKindProperties {
        content: Container {
            background: Color.create("#2575fc")
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment:   VerticalAlignment.Fill
            layout: DockLayout {}

            Container {
                visible: !chatViewPage.searchVisible
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment:   VerticalAlignment.Fill
                layout: StackLayout { orientation: LayoutOrientation.LeftToRight }

                Container {
                    id: avatarBlock
                    verticalAlignment: VerticalAlignment.Fill
                    layout: DockLayout {}
                    preferredWidth: titleBarLUH.layoutFrame.height > 0 ? titleBarLUH.layoutFrame.height : ui.du(7)
                    minWidth:       titleBarLUH.layoutFrame.height > 0 ? titleBarLUH.layoutFrame.height : ui.du(7)

                    ImageView {
                        horizontalAlignment: HorizontalAlignment.Fill
                        verticalAlignment:   VerticalAlignment.Fill
                        scalingMethod: ScalingMethod.AspectFill
                        imageSource: chatViewPage.avatarUrl
                        visible: chatViewPage.avatarUrl.length > 0
                    }
                    Container {
                        horizontalAlignment: HorizontalAlignment.Fill
                        verticalAlignment:   VerticalAlignment.Fill
                        background: Color.create("#1a5fc8")
                        layout: DockLayout {}
                        visible: chatViewPage.avatarUrl.length === 0
                        Label {
                            text: chatViewPage.threadName.length > 0 ? chatViewPage.threadName.charAt(0).toUpperCase() : "?"
                            horizontalAlignment: HorizontalAlignment.Center
                            verticalAlignment:   VerticalAlignment.Center
                            textStyle { color: Color.White; fontSize: FontSize.XXLarge; fontWeight: FontWeight.Bold }
                        }
                    }
                    attachedObjects: [ LayoutUpdateHandler { id: titleBarLUH } ]
                }

                Container {
                    verticalAlignment: VerticalAlignment.Center
                    leftPadding: ui.du(1.5)
                    layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                    Label {
                        text: chatViewPage.threadName.length > 0 ? chatViewPage.threadName : "..."
                        textStyle { color: Color.White; base: SystemDefaults.TextStyles.TitleText; fontWeight: FontWeight.Bold }
                        topMargin: 0; bottomMargin: 0
                    }
                    Label {
                        text: chatViewPage.isGroup ? "Group" : "Zalo Contact"
                        textStyle { color: Color.create("#b3d4ff"); fontSize: FontSize.XSmall }
                        topMargin: ui.du(0.2); bottomMargin: 0
                    }
                }

                ImageButton {
                    verticalAlignment: VerticalAlignment.Center
                    preferredWidth: ui.du(8); preferredHeight: ui.du(8)
                    defaultImageSource: "asset:///images/ChatView/ic_bbm_voice_answer.png"
                    pressedImageSource: "asset:///images/ChatView/ic_bbm_voice_answer.png"
                    rightMargin: ui.du(0.8)
                    onClicked: { voiceCallUnderDevDialog.show() }
                }
                ImageButton {
                    verticalAlignment: VerticalAlignment.Center
                    preferredWidth: ui.du(8); preferredHeight: ui.du(8)
                    defaultImageSource: "asset:///images/ChatView/ic_bbm_video_answer.png"
                    pressedImageSource: "asset:///images/ChatView/ic_bbm_video_answer.png"
                    onClicked: { videoCallUnderDevDialog.show() }
                }
                Container {
                    preferredWidth: ui.du(0.2)
                }
            }

            Container {
                visible: chatViewPage.searchVisible
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment:   VerticalAlignment.Center
                leftPadding: ui.du(1.5)
                rightPadding: ui.du(1.5)
                layout: StackLayout { orientation: LayoutOrientation.LeftToRight }

                TextField {
                    id: chatSearchField
                    hintText: "Search in this chat..."
                    verticalAlignment: VerticalAlignment.Center
                    textStyle { color: Color.create("#FFFFFF") }
                    layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                    onTextChanging: {
                        chatViewPage.searchText = text;
                        chatViewPage.updateSearchMatches(true);
                    }
                    onCreationCompleted: {
                        inputMode.type = TextInputFlag.AutoCapitalizationOff | TextInputFlag.AutoCorrectionOff | TextInputFlag.SpellCheckOff | TextInputFlag.PredictionOff;
                    }
                }
                Label {
                    text: chatViewPage.searchMatches.length > 0
                          ? (chatViewPage.searchMatchPos + 1) + "/" + chatViewPage.searchMatches.length
                          : (chatViewPage.searchText.length > 0 ? "0/0" : "")
                    verticalAlignment: VerticalAlignment.Center
                    rightMargin: ui.du(0.8)
                    textStyle { color: Color.create("#cfe2ff"); fontSize: FontSize.Small }
                }
                Button {
                    text: "\u25B2"
                    preferredWidth: ui.du(7)
                    verticalAlignment: VerticalAlignment.Center
                    enabled: chatViewPage.searchMatches.length > 0
                    onClicked: chatViewPage.gotoSearchMatch(-1)
                }
                Button {
                    text: "\u25BC"
                    preferredWidth: ui.du(7)
                    verticalAlignment: VerticalAlignment.Center
                    rightMargin: ui.du(0.8)
                    enabled: chatViewPage.searchMatches.length > 0
                    onClicked: chatViewPage.gotoSearchMatch(1)
                }
                Button {
                    text: "Cancel"
                    preferredWidth: ui.du(12)
                    verticalAlignment: VerticalAlignment.Center
                    onClicked: {
                        chatSearchField.text = "";
                        chatViewPage.searchText = "";
                        chatViewPage.searchVisible = false;
                        chatViewPage.searchMatches = [];
                        chatViewPage.searchMatchPos = -1;
                    }
                }
            }
        }
    }
}
