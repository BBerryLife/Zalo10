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
                text: "Groups"
                textStyle {
                    color: Color.White
                    base: SystemDefaults.TextStyles.TitleText
                    fontWeight: FontWeight.Bold
                }
                verticalAlignment: VerticalAlignment.Center
                horizontalAlignment: HorizontalAlignment.Left
                visible: !groupsNav.searchVisible
            }
            Container {
                visible: groupsNav.searchVisible
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment: VerticalAlignment.Center
                layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                TextField {
                    id: groupsSearchField
                    hintText: "Search groups..."
                    verticalAlignment: VerticalAlignment.Center
                    textStyle { color: Color.White }
                    layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                    onTextChanging: {
                        groupsNav.searchText = text;
                        groupsNav.filterList();
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
                        groupsSearchField.text = "";
                        groupsNav.searchText = "";
                        groupsNav.searchVisible = false;
                        groupsNav.filterList();
                    }
                }
            }
        }
    }
}
