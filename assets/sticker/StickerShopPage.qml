import bb.cascades 1.4
import bb.system 1.0
import QtQuick 1.0

// Sticker Shop: every pack in Data/Zalo10-stickers.json, free to get. "Get" downloads the pack to
// local storage (StickerManager); "Remove" deletes it again. Used as the Shop tab and pushed from
// the sticker bar in a chat.
Page {
    id: shopPage

    property bool isDark: app.getDarkTheme()
    property bool loading: false

    function sizeText(bytes) {
        if (bytes <= 0) return "";
        if (bytes < 1024 * 1024) return Math.max(1, Math.round(bytes / 1024)) + " KB";
        return (Math.round(bytes / 104857.6) / 10) + " MB";
    }

    function buildItem(c) {
        var parts = [c.count + (c.count === 1 ? " sticker" : " stickers")];
        if (c.animated) parts.push("Animated");
        var sz = sizeText(c.size);
        if (sz.length > 0) parts.push(sz);
        var info = [];
        if (c.animated) info.push("Animated");
        if (sz.length > 0) info.push(sz);
        return {
            id:         c.id,
            name:       c.name,
            preview:    c.preview,
            count:      c.count,
            info:       info.join(" \u00B7 "),
            subtitle:   parts.join(" \u00B7 "),
            installed:  c.installed,
            installing: c.installing,
            btnText:    c.installed ? "Remove" : (c.installing ? "..." : "Get"),
            btnEnabled: !c.installing
        };
    }

    function reload() {
        var cat = stickerManager.catalog();
        shopModel.clear();
        for (var i = 0; i < cat.length; i++) shopModel.append(buildItem(cat[i]));
        emptyLabel.visible = (cat.length === 0 && !shopPage.loading);
    }

    function indexOfPack(id) {
        for (var i = 0; i < shopModel.size(); i++) {
            if (shopModel.value(i).id === id) return i;
        }
        return -1;
    }

    // Refresh one row without rebuilding the list, so scrolling is not disturbed.
    function refreshPack(id) {
        var i = indexOfPack(id);
        if (i < 0) { reload(); return; }
        var cat = stickerManager.catalog();
        for (var k = 0; k < cat.length; k++) {
            if (cat[k].id === id) { shopModel.replace(i, buildItem(cat[k])); return; }
        }
    }

    function packAction(id) {
        var i = indexOfPack(id);
        if (i < 0) return;
        var it = shopModel.value(i);
        if (it.installing) return;
        if (it.installed) stickerManager.removePack(id);
        else stickerManager.installPack(id);
        refreshPack(id);
    }

    function refresh() {
        shopPage.loading = true;
        emptyLabel.visible = false;
        stickerManager.fetchCatalog();
    }

    onCreationCompleted: {
        reload();
        refresh();
    }

    titleBar: TitleBar {
        kind: TitleBarKind.FreeForm
        kindProperties: FreeFormTitleBarKindProperties {
            content: Container {
                background: Color.create("#2575fc")
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment: VerticalAlignment.Fill
                layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                leftPadding: ui.du(2)
                Label {
                    text: "Sticker Shop"
                    layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                    horizontalAlignment: HorizontalAlignment.Fill
                    verticalAlignment: VerticalAlignment.Center
                    textStyle { color: Color.White; base: SystemDefaults.TextStyles.TitleText; fontWeight: FontWeight.Bold }
                }
            }
        }
    }

    actions: [
        ActionItem {
            title: "Refresh"
            imageSource: "asset:///images/ChatView/ic_resend.png"
            ActionBar.placement: ActionBarPlacement.InOverflow
            onTriggered: shopPage.refresh()
        }
    ]

    attachedObjects: [
        StickerPackSheet {
            id: packSheet
            isDark: shopPage.isDark
        },
        Connections {
            target: app
            onDarkThemeChanged: { shopPage.isDark = dark; }
        },
        Connections {
            target: stickerManager
            onCatalogReady:   { shopPage.loading = false; shopPage.reload(); }
            onCatalogFailed:  {
                shopPage.loading = false;
                emptyLabel.text = "Could not load the sticker list.\nCheck your connection and tap Refresh.";
                emptyLabel.visible = (shopModel.size() === 0);
                shopToast.body = "Could not load stickers: " + error;
                shopToast.show();
            }
            onPreviewReady:   { shopPage.refreshPack(packId); }
            onPackProgress:   {
                var i = shopPage.indexOfPack(packId);
                if (i < 0) return;
                var it = shopModel.value(i);
                it.installing = true;
                it.btnEnabled = false;
                it.btnText = done + "/" + total;
                shopModel.replace(i, it);
            }
            onPackInstalled:  { shopPage.refreshPack(packId); }
            onPackRemoved:    { shopPage.refreshPack(packId); }
            onPackFailed:     {
                shopPage.refreshPack(packId);
                shopToast.body = "Could not download " + packId + ": " + error;
                shopToast.show();
            }
        },
        SystemToast {
            id: shopToast
            position: SystemUiPosition.MiddleCenter
        }
    ]

    Container {
        layout: DockLayout {}

        ListView {
            id: shopList
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment: VerticalAlignment.Fill
            dataModel: ArrayDataModel { id: shopModel }

            function packAction(id) { shopPage.packAction(id); }

            // Tapping the row (not its Get / Remove button) opens the pack viewer.
            onTriggered: {
                var d = shopModel.data(indexPath);
                if (d) packSheet.openPack(d);
            }

            listItemComponents: [
                ListItemComponent {
                    type: ""
                    Container {
                        id: shopRow
                        horizontalAlignment: HorizontalAlignment.Fill
                        layout: StackLayout { orientation: LayoutOrientation.TopToBottom }

                        Container {
                            horizontalAlignment: HorizontalAlignment.Fill
                            preferredHeight: ui.du(12)
                            leftPadding: ui.du(1.5); rightPadding: ui.du(1.5)
                            layout: StackLayout { orientation: LayoutOrientation.LeftToRight }

                            ImageView {
                                verticalAlignment: VerticalAlignment.Center
                                preferredWidth: ui.du(9); preferredHeight: ui.du(9)
                                scalingMethod: ScalingMethod.AspectFit
                                imageSource: ListItemData.preview
                            }
                            Container {
                                layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                                verticalAlignment: VerticalAlignment.Center
                                leftMargin: ui.du(2)
                                layout: StackLayout { orientation: LayoutOrientation.TopToBottom }
                                Label {
                                    text: ListItemData.name
                                    bottomMargin: 0
                                    textStyle.base: SystemDefaults.TextStyles.TitleText
                                }
                                Label {
                                    text: ListItemData.subtitle
                                    topMargin: 0
                                    textStyle.base: SystemDefaults.TextStyles.SmallText
                                    textStyle.color: Color.Gray
                                }
                            }
                            Button {
                                verticalAlignment: VerticalAlignment.Center
                                preferredWidth: ui.du(18)
                                text: ListItemData.btnText
                                enabled: ListItemData.btnEnabled
                                onClicked: { shopRow.ListItem.view.packAction(ListItemData.id); }
                            }
                        }
                        Divider {}
                    }
                }
            ]
        }

        Container {
            horizontalAlignment: HorizontalAlignment.Center
            verticalAlignment: VerticalAlignment.Center
            visible: shopPage.loading && shopModel.size() === 0
            ActivityIndicator {
                horizontalAlignment: HorizontalAlignment.Center
                preferredWidth: ui.du(10); preferredHeight: ui.du(10)
                running: shopPage.loading
            }
        }

        Label {
            id: emptyLabel
            visible: false
            horizontalAlignment: HorizontalAlignment.Center
            verticalAlignment: VerticalAlignment.Center
            multiline: true
            textStyle { textAlign: TextAlign.Center }
            text: "No sticker packs available."
        }
    }
}
