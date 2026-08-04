import bb.cascades 1.4
import QtQuick 1.0

// Lists every app registered for bb.action.SHARE, in the order
// app.queryShareTargets() returns them
Sheet {
    id: sharePickerSheetRoot

    property variant targets: []
    property string pendingShareText: ""   // set by ChatView before opening
    // Set instead of pendingShareText when sharing a photo message that has a
    // local cached copy, so the target app gets the actual image, not a URL
    property bool   pendingShareIsImage: false
    property string pendingShareImagePath: ""

    function openWithTargets(tgts) {
        targets = tgts;
        open();
    }

    onTargetsChanged: {
        shareTargetModel.clear();
        for (var oi = 0; oi < targets.length; oi = oi + 1) {
            shareTargetModel.append(targets[oi]);
        }
    }

    Page {
        titleBar: TitleBar {
            title: "Share"
            dismissAction: ActionItem {
                title: "Cancel"
                onTriggered: { sharePickerSheetRoot.close(); }
            }
        }

        ListView {
            dataModel: ArrayDataModel { id: shareTargetModel }

            listItemComponents: [
                ListItemComponent {
                    CustomListItem {
                        dividerVisible: true
                        Container {
                            layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                            verticalAlignment: VerticalAlignment.Center
                            leftPadding: 0; rightPadding: 16; topPadding: 2; bottomPadding: 2
                            ImageView {
                                id: shareIcon
                                imageSource: ListItemData.icon
                                scalingMethod: ScalingMethod.AspectFit
                                verticalAlignment:   VerticalAlignment.Center
                                horizontalAlignment: HorizontalAlignment.Left
                                minWidth: 80
                                preferredWidth:  77
                                preferredHeight: 77
                                onCreationCompleted: {
                                    if (!ListItemData.isNative) {
                                        preferredWidth  = 65;
                                        preferredHeight = 65;
                                    }
                                }
                            }
                            Label {
                                text: ListItemData.label
                                verticalAlignment: VerticalAlignment.Center
                                layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                                leftMargin: 7
                                textStyle.base: SystemDefaults.TextStyles.TitleText
                            }
                        }
                    }
                }
            ]

            onTriggered: {
                var item = dataModel.data(indexPath);
                sharePickerSheetRoot.close();
                if (sharePickerSheetRoot.pendingShareIsImage && sharePickerSheetRoot.pendingShareImagePath.length > 0) {
                    app.invokeShareTargetForImage(item.target, item.action, sharePickerSheetRoot.pendingShareImagePath);
                } else {
                    app.invokeShareTarget(item.target, item.action, sharePickerSheetRoot.pendingShareText);
                }
            }
        }
    }
}
