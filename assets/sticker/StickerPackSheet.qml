import bb.cascades 1.4
import QtQuick 1.0

// Sticker pack viewer, opened by tapping a pack in the Sticker Shop (not its Get button). Header laid out like My
// Profile (the pack's first sticker where the avatar is, the pack name where the name is), without a cover. Under it the
// sticker count and every sticker of the pack in a grid, like the sticker panel of a chat. A pack that is
// not installed yet is downloaded sticker by sticker (grey cell until it arrives) and cleaned up on its install.
Sheet {
    id: packSheet

    property bool    isDark: false
    property string  packId: ""
    property string  packName: ""
    property string  packPreview: ""
    property string  packInfo: ""      // "Animated · 1.2 MB"
    property int     stickerCount: 0

    // item: one row of the shop's model ({ id, name, preview, count, info })
    function openPack(item) {
        packSheet.packId       = item.id;
        packSheet.packName     = item.name;
        packSheet.packPreview  = item.preview || "";
        packSheet.packInfo     = item.info || "";
        packSheet.stickerCount = item.count || 0;

        gridModel.clear();
        var items = stickerManager.packStickers(item.id);
        for (var i = 0; i < items.length; i++) gridModel.append(items[i]);
        if (items.length > 0) packSheet.stickerCount = items.length;
        stickerManager.browsePack(item.id);
        packSheet.open();
    }

    function indexOfFile(file) {
        for (var i = 0; i < gridModel.size(); i++) {
            if (gridModel.value(i).file === file) return i;
        }
        return -1;
    }

    onClosed: { stickerManager.cancelBrowse(packSheet.packId); }

    attachedObjects: [
        Connections {
            target: stickerManager
            onBrowseReady: {
                if (packId !== packSheet.packId) return;
                var i = packSheet.indexOfFile(file);
                if (i < 0) return;
                var d = gridModel.value(i);
                d.url = url;
                d.path = url.indexOf("file://") === 0 ? url.substring(7) : url;
                gridModel.replace(i, d);
                if (packSheet.packPreview.length === 0 && i === 0) packSheet.packPreview = url;
            }
            onPreviewReady: {
                if (packId === packSheet.packId && packSheet.packPreview.length === 0) packSheet.packPreview = localPath;
            }
        },
        ArrayDataModel { id: gridModel }
    ]

    content: Page {
        titleBar: TitleBar {
            title: qsTr("Sticker Pack")
            dismissAction: ActionItem {
                title: qsTr("Close")
                onTriggered: { packSheet.close(); }
            }
        }

        Container {
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment: VerticalAlignment.Fill
            background: packSheet.isDark ? Color.create("#1a1a1a") : Color.White

            // Header: laid out like the My Profile header (first sticker where the avatar is, pack name where the name
            // is) but plain: no cover and no dark layer.
            Container {
                preferredHeight: ui.du(26)
                horizontalAlignment: HorizontalAlignment.Fill
                layout: StackLayout { orientation: LayoutOrientation.LeftToRight }

                Container {
                    preferredWidth: ui.du(26)
                    preferredHeight: ui.du(26)
                    minWidth: ui.du(26)
                    background: packSheet.isDark ? Color.create("#2a2a2a") : Color.create("#f0f0f0")
                    layout: DockLayout {}
                    ImageView {
                        imageSource: packSheet.packPreview
                        horizontalAlignment: HorizontalAlignment.Fill
                        verticalAlignment: VerticalAlignment.Fill
                        scalingMethod: ScalingMethod.AspectFit
                    }
                }

                Container {
                    layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                    verticalAlignment: VerticalAlignment.Fill
                    leftPadding: ui.du(2)
                    rightPadding: ui.du(2)
                    topPadding: ui.du(2)
                    Label {
                        text: packSheet.packName
                        multiline: false
                        textStyle { base: SystemDefaults.TextStyles.BigText; color: packSheet.isDark ? Color.White : Color.Black }
                    }
                    Label {
                        visible: packSheet.packInfo.length > 0
                        text: packSheet.packInfo
                        multiline: true
                        textStyle {
                            base: SystemDefaults.TextStyles.BodyText
                            color: packSheet.isDark ? Color.create("#b0b0b0") : Color.create("#7a7a7a")
                        }
                    }
                }
            }

            // Count
            Container {
                horizontalAlignment: HorizontalAlignment.Fill
                background: packSheet.isDark ? Color.create("#2a2a2a") : Color.create("#eeeeee")
                topPadding: ui.du(0.8); bottomPadding: ui.du(0.8)
                leftPadding: ui.du(1.5); rightPadding: ui.du(1.5)
                Label {
                    text: packSheet.stickerCount + (packSheet.stickerCount === 1 ? qsTr(" sticker") : qsTr(" stickers"))
                    textStyle {
                        base: SystemDefaults.TextStyles.SmallText
                        fontWeight: FontWeight.Bold
                        color: packSheet.isDark ? Color.create("#b0b0b0") : Color.create("#555555")
                    }
                }
            }

            ListView {
                id: gridList
                layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                horizontalAlignment: HorizontalAlignment.Fill
                dataModel: gridModel
                property bool isDarkProxy: packSheet.isDark
                layout: GridListLayout {
                    columnCount: 4
                    cellAspectRatio: 1.0
                    horizontalCellSpacing: ui.du(0.5)
                    verticalCellSpacing: ui.du(0.5)
                    headerMode: ListHeaderMode.None
                }
                listItemComponents: [
                    ListItemComponent {
                        type: ""
                        Container {
                            id: cell
                            layout: DockLayout {}
                            horizontalAlignment: HorizontalAlignment.Fill
                            verticalAlignment: VerticalAlignment.Fill
                            // grey cell until the sticker has been downloaded
                            background: (ListItemData.url && ListItemData.url.length > 0)
                                ? Color.create("#00000000")
                                : (cell.ListItem.view.isDarkProxy ? Color.create("#2a2a2a") : Color.create("#f0f0f0"))
                            ImageView {
                                horizontalAlignment: HorizontalAlignment.Fill
                                verticalAlignment: VerticalAlignment.Fill
                                scalingMethod: ScalingMethod.AspectFit
                                imageSource: (ListItemData.url && ListItemData.url.length > 0) ? ListItemData.url : ""
                            }
                        }
                    }
                ]
            }
        }
    }
}
