import bb.cascades 1.4
import QtQuick 1.0

// Pushed into aboutNav from AboutSheet.qml when the user taps "Change List"
// Rows are set via setRows() right after creation
//
// Uses ListView + ArrayDataModel (same as ChatsTab/GroupsTab/InvitesTab) instead of a
// WebView, since the WebView couldn't reliably report its own content height on device
Page {
    id: changelogPage

    function setRows(rows) {
        rowModel.clear();
        rowModel.append(rows);
        changelogList.visible = true;
        changelogEmpty.visible = (rows.length === 0);
        changelogLoading.visible = false;
    }

    titleBar: TitleBar {
        kind: TitleBarKind.FreeForm
        kindProperties: FreeFormTitleBarKindProperties {
            content: Container {
                background: Color.create("#2575fc")
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment: VerticalAlignment.Fill
                leftPadding: ui.du(2.5)
                layout: DockLayout {}
                Label {
                    text: "Change List"
                    horizontalAlignment: HorizontalAlignment.Left
                    verticalAlignment: VerticalAlignment.Center
                    textStyle {
                        color: Color.White
                        fontWeight: FontWeight.Bold
                        fontSize: FontSize.Large
                    }
                }
            }
        }
    }

    Container {
        horizontalAlignment: HorizontalAlignment.Fill
        verticalAlignment: VerticalAlignment.Fill
        layout: DockLayout {}

        ListView {
            id: changelogList
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment: VerticalAlignment.Fill
            dataModel: rowModel
            visible: false

            function itemType(data, indexPath) {
                return data.rowType === "header" ? "header" : "item";
            }

            listItemComponents: [
                ListItemComponent {
                    type: "header"
                    Container {
                        horizontalAlignment: HorizontalAlignment.Fill
                        leftPadding: ui.du(4); rightPadding: ui.du(4)
                        topPadding: ListItemData.isFirstHeader ? ui.du(3) : ui.du(5)
                        bottomPadding: ui.du(2)

                        Label {
                            text: "Version " + ListItemData.version + ".x"
                            textStyle {
                                base: SystemDefaults.TextStyles.TitleText
                                fontWeight: FontWeight.Bold
                            }
                        }

                        Divider {
                            topMargin: ui.du(1)
                        }
                    }
                },
                ListItemComponent {
                    type: "item"
                    Container {
                        horizontalAlignment: HorizontalAlignment.Fill
                        leftPadding: ui.du(4); rightPadding: ui.du(4)
                        topPadding: ui.du(1.2); bottomPadding: ui.du(1.2)
                        layout: StackLayout { orientation: LayoutOrientation.LeftToRight }

                        Label {
                            text: "•"
                            verticalAlignment: VerticalAlignment.Top
                            rightMargin: ui.du(1)
                            textStyle.color: Color.create("#2575fc")
                        }

                        Container {
                            horizontalAlignment: HorizontalAlignment.Fill
                            layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                            layoutProperties: StackLayoutProperties { spaceQuota: 1 }

                            Label {
                                text: ListItemData.tag.length > 0 ? (ListItemData.tag + " ") : ""
                                visible: ListItemData.tag.length > 0
                                verticalAlignment: VerticalAlignment.Top
                                textStyle.fontWeight: FontWeight.Bold
                            }

                            Label {
                                text: ListItemData.text
                                multiline: true
                                layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                                textStyle.color: Color.create("#333333")
                            }
                        }
                    }
                }
            ]
        }

        Label {
            id: changelogEmpty
            text: "Changelog is empty right now."
            visible: false
            horizontalAlignment: HorizontalAlignment.Center
            verticalAlignment: VerticalAlignment.Center
        }

        ActivityIndicator {
            id: changelogLoading
            horizontalAlignment: HorizontalAlignment.Center
            verticalAlignment: VerticalAlignment.Center
            running: true
            visible: true
        }

        attachedObjects: [
            ArrayDataModel { id: rowModel }
        ]
    }
}
