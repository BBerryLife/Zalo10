import bb.cascades 1.4
import bb.system 1.0
import QtQuick 1.0

// Create Poll dialog for the Group Board, opened from GroupBoardSheet.qml's
// "Create Poll" action-bar item. Layout follows the reference screenshot
// (Zalo's own web UI "Poll" creation dialog): title + close X, question
// field, "Choose multiple options" toggle line, a growing list of option
// fields with "+ Add option" beneath, a settings gear opening extra poll
// options, Cancel/Confirm at the bottom.
//
// Up to 10 option fields are declared as FIXED indexed TextFields rather
// than a repeated/looped component — this codebase's QML runtime (bb.cascades
// 1.4 / QtQuick 1.0) has no Repeater item (see GroupBoardSheet.qml's poll
// option rendering for the same constraint/workaround). optionVisibleCount
// controls how many of the 10 slots are shown; "+ Add option" just reveals
// one more.
Sheet {
    id: createPollSheetRoot

    property string groupId: ""
    property bool isDark: app.getDarkTheme()
    property int optionVisibleCount: 2
    property bool allowMultiChoices: false
    property bool allowAddNewOption: false
    property bool hideVotePreview: false
    property bool isAnonymous: false

    property int maxOptions: 10

    function resetFields() {
        questionField.text = "";
        opt0.text = ""; opt1.text = ""; opt2.text = ""; opt3.text = ""; opt4.text = "";
        opt5.text = ""; opt6.text = ""; opt7.text = ""; opt8.text = ""; opt9.text = "";
        createPollSheetRoot.optionVisibleCount = 2;
        createPollSheetRoot.allowMultiChoices = false;
        createPollSheetRoot.allowAddNewOption = false;
        createPollSheetRoot.hideVotePreview = false;
        createPollSheetRoot.isAnonymous = false;
    }

    function collectOptions() {
        var fields = [opt0, opt1, opt2, opt3, opt4, opt5, opt6, opt7, opt8, opt9];
        var result = [];
        for (var i = 0; i < createPollSheetRoot.optionVisibleCount && i < fields.length; i++) {
            var t = fields[i].text.trim();
            if (t.length > 0) result.push(t);
        }
        return result;
    }

    function doAddOption() {
        if (createPollSheetRoot.optionVisibleCount < createPollSheetRoot.maxOptions) {
            createPollSheetRoot.optionVisibleCount += 1;
        }
    }

    function doConfirm() {
        var question = questionField.text.trim();
        var options = collectOptions();
        if (question.length === 0) {
            pollValidationToast.body = "Please enter a poll question.";
            pollValidationToast.show();
            return;
        }
        if (options.length < 2) {
            pollValidationToast.body = "Please enter at least 2 options.";
            pollValidationToast.show();
            return;
        }
        zService.createGroupPoll(createPollSheetRoot.groupId, question, options,
            createPollSheetRoot.allowMultiChoices, createPollSheetRoot.allowAddNewOption,
            createPollSheetRoot.hideVotePreview, createPollSheetRoot.isAnonymous);
        createPollSheetRoot.close();
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
                        onClicked: { createPollSheetRoot.close(); }
                    }

                    Label {
                        text: "Poll"
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
                title: "Settings"
                imageSource: "asset:///images/ic_settings.png"
                ActionBar.placement: ActionBarPlacement.OnBar
                onTriggered: { pollSettingsSheet.open(); }
            },
            ActionItem {
                title: "Confirm"
                imageSource: "asset:///images/ChatView/ic_save.png"
                ActionBar.placement: ActionBarPlacement.OnBar
                onTriggered: { createPollSheetRoot.doConfirm(); }
            }
        ]

        ScrollView {
            Container {
                horizontalAlignment: HorizontalAlignment.Fill
                topPadding: 14

                Container {
                    horizontalAlignment: HorizontalAlignment.Fill
                    leftPadding: 20; rightPadding: 20

                    TextField {
                        id: questionField
                        hintText: "Ask something"
                        horizontalAlignment: HorizontalAlignment.Fill
                        textStyle { fontWeight: FontWeight.Bold; fontSize: FontSize.Medium }
                    }
                }

                Divider { topMargin: 10; bottomMargin: 10; leftMargin: 20; rightMargin: 20 }

                Container {
                    layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                    horizontalAlignment: HorizontalAlignment.Fill
                    leftPadding: 20; rightPadding: 20; bottomPadding: 6

                    Label {
                        text: "Choose multiple options"
                        layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                        verticalAlignment: VerticalAlignment.Center
                        textStyle { color: Color.Gray; fontSize: FontSize.Small }
                    }
                    ToggleButton {
                        checked: createPollSheetRoot.allowMultiChoices
                        onCheckedChanged: { createPollSheetRoot.allowMultiChoices = checked; }
                    }
                }

                // Option fields — fixed 10 slots, only optionVisibleCount shown.
                // Each row mirrors the reference screenshot's option pill:
                // a plain text field with hint "Option N".
                Container {
                    horizontalAlignment: HorizontalAlignment.Fill
                    leftPadding: 20; rightPadding: 20; topPadding: 8

                    TextField { id: opt0; visible: createPollSheetRoot.optionVisibleCount > 0; hintText: "Option 1"; bottomMargin: 10 }
                    TextField { id: opt1; visible: createPollSheetRoot.optionVisibleCount > 1; hintText: "Option 2"; bottomMargin: 10 }
                    TextField { id: opt2; visible: createPollSheetRoot.optionVisibleCount > 2; hintText: "Option 3"; bottomMargin: 10 }
                    TextField { id: opt3; visible: createPollSheetRoot.optionVisibleCount > 3; hintText: "Option 4"; bottomMargin: 10 }
                    TextField { id: opt4; visible: createPollSheetRoot.optionVisibleCount > 4; hintText: "Option 5"; bottomMargin: 10 }
                    TextField { id: opt5; visible: createPollSheetRoot.optionVisibleCount > 5; hintText: "Option 6"; bottomMargin: 10 }
                    TextField { id: opt6; visible: createPollSheetRoot.optionVisibleCount > 6; hintText: "Option 7"; bottomMargin: 10 }
                    TextField { id: opt7; visible: createPollSheetRoot.optionVisibleCount > 7; hintText: "Option 8"; bottomMargin: 10 }
                    TextField { id: opt8; visible: createPollSheetRoot.optionVisibleCount > 8; hintText: "Option 9"; bottomMargin: 10 }
                    TextField { id: opt9; visible: createPollSheetRoot.optionVisibleCount > 9; hintText: "Option 10"; bottomMargin: 10 }

                    Label {
                        text: "+ Add option"
                        visible: createPollSheetRoot.optionVisibleCount < createPollSheetRoot.maxOptions
                        textStyle { color: Color.create("#2575fc"); fontWeight: FontWeight.Bold }
                        topMargin: 4; bottomMargin: 20
                        gestureHandlers: [
                            TapHandler { onTapped: { createPollSheetRoot.doAddOption(); } }
                        ]
                    }
                }
            }
        }

        attachedObjects: [
            SystemToast { id: pollValidationToast },
            Sheet {
                id: pollSettingsSheet
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
                                    onClicked: { pollSettingsSheet.close(); }
                                }
                                Label {
                                    text: "Poll settings"
                                    layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                                    verticalAlignment: VerticalAlignment.Center
                                    textStyle { color: Color.White; base: SystemDefaults.TextStyles.TitleText; fontWeight: FontWeight.Bold }
                                }
                            }
                        }
                    }

                    Container {
                        horizontalAlignment: HorizontalAlignment.Fill
                        topPadding: 20; leftPadding: 20; rightPadding: 20

                        Container {
                            layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                            horizontalAlignment: HorizontalAlignment.Fill
                            bottomMargin: 20
                            Label {
                                text: "Let people add new options"
                                layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                                verticalAlignment: VerticalAlignment.Center
                            }
                            ToggleButton {
                                checked: createPollSheetRoot.allowAddNewOption
                                onCheckedChanged: { createPollSheetRoot.allowAddNewOption = checked; }
                            }
                        }

                        Container {
                            layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                            horizontalAlignment: HorizontalAlignment.Fill
                            bottomMargin: 20
                            Label {
                                text: "Hide vote results until poll ends"
                                layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                                verticalAlignment: VerticalAlignment.Center
                                multiline: true
                            }
                            ToggleButton {
                                checked: createPollSheetRoot.hideVotePreview
                                onCheckedChanged: { createPollSheetRoot.hideVotePreview = checked; }
                            }
                        }

                        Container {
                            layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                            horizontalAlignment: HorizontalAlignment.Fill
                            bottomMargin: 20
                            Label {
                                text: "Anonymous poll"
                                layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                                verticalAlignment: VerticalAlignment.Center
                            }
                            ToggleButton {
                                checked: createPollSheetRoot.isAnonymous
                                onCheckedChanged: { createPollSheetRoot.isAnonymous = checked; }
                            }
                        }
                    }
                }
            }
        ]
    }
}
