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
            layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
            leftPadding: ui.du(2)

            Label {
                text: "Group Board"
                layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment: VerticalAlignment.Center
                textStyle { color: Color.White; base: SystemDefaults.TextStyles.TitleText; fontWeight: FontWeight.Bold }
            }
        }
    }
}
