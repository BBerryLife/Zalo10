import bb.cascades 1.4
import bb.system 1.0
import QtQuick 1.0

// Read-only note viewer, opened from GroupBoardSheet.qml's "View note" link
// Edit/delete not implemented yet, so this is display-only for now
Sheet {
    id: noteViewerRoot

    property string noteTitle: ""
    property string noteContent: ""
    property string creatorLabel: ""

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
                        onClicked: { noteViewerRoot.close(); }
                    }

                    Label {
                        text: "Note"
                        layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                        verticalAlignment: VerticalAlignment.Center
                        textStyle { color: Color.White; base: SystemDefaults.TextStyles.TitleText; fontWeight: FontWeight.Bold }
                        topMargin: 0; bottomMargin: 0
                    }
                }
            }
        }

        ScrollView {
            Container {
                horizontalAlignment: HorizontalAlignment.Fill
                topPadding: 20; leftPadding: 20; rightPadding: 20; bottomPadding: 20

                Label {
                    text: noteViewerRoot.creatorLabel.length > 0 ? ("Created by " + noteViewerRoot.creatorLabel) : ""
                    visible: noteViewerRoot.creatorLabel.length > 0
                    textStyle { color: Color.Gray; fontSize: FontSize.Small }
                    bottomMargin: 14
                }

                Label {
                    text: noteViewerRoot.noteContent
                    multiline: true
                    textStyle { fontSize: FontSize.Medium }
                }
            }
        }
    }
}
