import bb.cascades 1.4
import QtQuick 1.0

// Zalo's own stickers: the account's sticker packs as tabs, and a grid. Tapping a sticker
// emits its ids; ChatView sends it as a real Zalo sticker (zService.sendZaloSticker).
// Data: zService.fetchZaloStickerCategories / fetchZaloStickerCategory , pictures
// from zService.downloadSticker (stickerReady).
Container {
    id: zpanel

    property bool    isDark: false
    property variant cats: []
    property string  currentKey: ""      // "c<cateId>" or "s:<keyword>"
    property int     currentCate: 0
    property bool    started: false

    signal stickerTapped(int stickerId, int cateId, int type)

    horizontalAlignment: HorizontalAlignment.Fill
    layoutProperties: StackLayoutProperties { spaceQuota: 1 }
    layout: StackLayout { orientation: LayoutOrientation.TopToBottom }

    function textColor() { return zpanel.isDark ? Color.create("#dddddd") : Color.create("#222222"); }

    // Called each time the Zalo tab is shown.
    function activate() {
        if (zpanel.started) return;
        zpanel.started = true;
        var cached = zService.zaloStickerCategories();
        if (cached.length > 0) zpanel.setCats(cached);
        zService.fetchZaloStickerCategories(cached.length === 0);
        if (cached.length === 0) zpanel.showStatus("Loading your Zalo stickers…");
    }

    function setCats(list) {
        zpanel.cats = list;
        catModel.clear();
        for (var i = 0; i < list.length; i++)
            catModel.append({ id: list[i].id, name: list[i].name, selected: list[i].id === zpanel.currentCate });
        if (zpanel.currentKey === "" && list.length > 0) zpanel.openCate(list[0].id);
    }

    function markSelected() {
        for (var i = 0; i < catModel.size(); i++) {
            var d = catModel.value(i);
            var sel = (zpanel.currentKey === "c" + d.id);
            if (d.selected !== sel) catModel.replace(i, { id: d.id, name: d.name, selected: sel });
        }
    }

    function openCate(id) {
        zpanel.currentCate = id;
        zpanel.currentKey = "c" + id;
        zpanel.markSelected();
        gridModel.clear();
        zpanel.showStatus("Loading…");
        zService.fetchZaloStickerCategory(id);
    }

    function showStatus(text) {
        statusLabel.text = text;
        statusLabel.visible = text.length > 0;
    }

    function fill(list) {
        gridModel.clear();
        for (var i = 0; i < list.length; i++) {
            gridModel.append({ id: list[i].id, cateId: list[i].cateId, type: list[i].type, url: "" });
            zService.downloadSticker(String(list[i].id));    // answers at once from the cache
        }
        zpanel.showStatus("");
    }

    attachedObjects: [
        Connections {
            target: zService
            onZaloStickerCategoriesReady: {
                if (cats.length > 0) zpanel.setCats(cats);
                else if (zpanel.currentKey === "") zpanel.showStatus(error.length > 0 ? error : "No sticker packs found");
            }
            onZaloStickerListReady: {
                if (key !== zpanel.currentKey) return;
                if (stickers.length > 0) zpanel.fill(stickers);
                else zpanel.showStatus(error.length > 0 ? error : "No stickers.");
            }
            onStickerReady: {
                for (var i = 0; i < gridModel.size(); i++) {
                    var d = gridModel.value(i);
                    if (String(d.id) === stickerId) {
                        if (d.url !== localPath)
                            gridModel.replace(i, { id: d.id, cateId: d.cateId, type: d.type, url: localPath });
                        return;
                    }
                }
            }
        }
    ]

    // Packs of the account, as text tabs.
    ListView {
        id: catList
        horizontalAlignment: HorizontalAlignment.Fill
        preferredHeight: ui.du(6)
        layout: StackListLayout { orientation: LayoutOrientation.LeftToRight }
        scrollIndicatorMode: ScrollIndicatorMode.None
        dataModel: ArrayDataModel { id: catModel }
        onTriggered: {
            var d = catModel.data(indexPath);
            if (d) zpanel.openCate(d.id);
        }
        function bg(sel) { return sel ? (zpanel.isDark ? Color.create("#3a3a3a") : Color.create("#EFEFEF")) : Color.Transparent; }
        function fg() { return zpanel.textColor(); }
        listItemComponents: [
            ListItemComponent {
                type: ""
                Container {
                    id: catItem
                    verticalAlignment: VerticalAlignment.Fill
                    leftPadding: ui.du(1.5); rightPadding: ui.du(1.5)
                    layout: DockLayout {}
                    background: catItem.ListItem.view.bg(ListItemData.selected)
                    Label {
                        verticalAlignment: VerticalAlignment.Center
                        text: ListItemData.name
                        textStyle.fontSize: FontSize.Small
                        textStyle.fontWeight: ListItemData.selected ? FontWeight.Bold : FontWeight.Normal
                        textStyle.color: catItem.ListItem.view.fg()
                    }
                }
            }
        ]
    }

    Container {
        horizontalAlignment: HorizontalAlignment.Fill
        layoutProperties: StackLayoutProperties { spaceQuota: 1 }
        layout: DockLayout {}

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
                if (d) zpanel.stickerTapped(d.id, d.cateId, d.type);
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
            id: statusLabel
            visible: false
            horizontalAlignment: HorizontalAlignment.Center
            verticalAlignment: VerticalAlignment.Center
            multiline: true
            textStyle { textAlign: TextAlign.Center; color: zpanel.isDark ? Color.create("#9a9a9a") : Color.create("#555555") }
        }
    }
}
