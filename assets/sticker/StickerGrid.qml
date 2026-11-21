import bb.cascades 1.4

// Stickers of one installed pack. Tapping one emits its file path; ChatView does the sending.
Container {
    id: stickerGrid

    property bool   isDark: false
    property string packId: ""

    signal stickerTapped(string path)

    horizontalAlignment: HorizontalAlignment.Fill
    layoutProperties: StackLayoutProperties { spaceQuota: 1 }
    layout: DockLayout {}

    function load(id) {
        stickerGrid.packId = id;
        gridModel.clear();
        if (id === "") { emptyLabel.visible = false; return; }
        var items = stickerManager.stickersOf(id);
        for (var i = 0; i < items.length; i++) gridModel.append(items[i]);
        emptyLabel.visible = (items.length === 0);
    }

    ListView {
        id: gridList
        horizontalAlignment: HorizontalAlignment.Fill
        verticalAlignment: VerticalAlignment.Fill
        dataModel: ArrayDataModel { id: gridModel }
        layout: GridListLayout {
            columnCount: 4
            cellAspectRatio: 1.0
            horizontalCellSpacing: ui.du(0.5)
            verticalCellSpacing: ui.du(0.5)
            headerMode: ListHeaderMode.None
        }
        onTriggered: {
            var d = gridModel.data(indexPath);
            if (d) stickerGrid.stickerTapped(d.path);
        }
        listItemComponents: [
            ListItemComponent {
                type: ""
                Container {
                    layout: DockLayout {}
                    horizontalAlignment: HorizontalAlignment.Fill
                    verticalAlignment: VerticalAlignment.Fill
                    ImageView {
                        horizontalAlignment: HorizontalAlignment.Fill
                        verticalAlignment: VerticalAlignment.Fill
                        scalingMethod: ScalingMethod.AspectFit
                        imageSource: ListItemData.url
                    }
                }
            }
        ]
    }

    Label {
        id: emptyLabel
        visible: false
        horizontalAlignment: HorizontalAlignment.Center
        verticalAlignment: VerticalAlignment.Center
        multiline: true
        textStyle { textAlign: TextAlign.Center; color: stickerGrid.isDark ? Color.create("#9a9a9a") : Color.create("#555555") }
        text: "This pack has no stickers."
    }
}
