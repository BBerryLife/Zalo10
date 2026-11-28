import bb.cascades 1.4
import QtQuick 1.0
import "sticker"
import "emoji/EmojiData.js" as EmojiData

Container {
    id: emojiPanel
    background: isDark ? Color.create("#1f1f1f") : Color.create("#e8e8e8")

    signal emojiPicked(string charStr)
    signal stickerPicked(string path)
    signal stickerShopRequested()
    signal stickerSettingsRequested()
    signal zaloStickerPicked(int stickerId, int cateId, int type)

    property real   pageWidth:       0
    property bool   isDark:       false
    // "" = emoji keyboard, otherwise the id of the sticker pack on show.
    property string  activePackId: ""
    property variant packs:        []

    // Showing/hiding the emoji section and the sticker grid only toggles `visible`: Cascades'
    // built-in implicit animations do the sliding/fading, nothing is animated by hand here.
    function showEmoji() { emojiPanel.activePackId = ""; }
    function showPack(id) {
        emojiPanel.activePackId = id;
        stickerGrid.load(id);
    }
    function reloadPacks() {
        var list = stickerManager.enabledPacks();
        emojiPanel.packs = list;
        if (emojiPanel.activePackId === "" || emojiPanel.activePackId === "__zalo__") return;
        for (var i = 0; i < list.length; i++) {
            if (list[i].id === emojiPanel.activePackId) { stickerGrid.load(emojiPanel.activePackId); return; }
        }
        emojiPanel.showEmoji();   // the pack was disabled or removed
    }
    onCreationCompleted: { reloadPacks(); emojiModel.append(EmojiData.all()); }

    attachedObjects: [
        Connections {
            target: stickerManager
            onInstalledChanged: { emojiPanel.reloadPacks(); }
        },
        LayoutUpdateHandler {
            id: panelLUH
            onLayoutFrameChanged: {
                if (layoutFrame.width > 0)
                    pageWidth = layoutFrame.width;
            }
        }
    ]

    StickerBar {
        id: stickerBar
        isDark: emojiPanel.isDark
        activePackId: emojiPanel.activePackId
        packs: emojiPanel.packs
        onEmojiTapped: { emojiPanel.showEmoji(); }
        onPackTapped: { emojiPanel.showPack(packId); }
        onZaloTapped: { emojiPanel.activePackId = "__zalo__"; zaloPanel.activate(); }
        onShopTapped: { emojiPanel.stickerShopRequested(); }
        onSettingsTapped: { emojiPanel.stickerSettingsRequested(); }
    }

    // The system emoji, one scrolling grid (as BBM's picker): the glyphs come from the OS emoji font.
    Container {
        id: emojiSection
        visible: emojiPanel.activePackId === ""
        horizontalAlignment: HorizontalAlignment.Fill
        layoutProperties: StackLayoutProperties { spaceQuota: 1 }
        layout: DockLayout {}

        ListView {
            id: emojiList
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment: VerticalAlignment.Fill
            scrollIndicatorMode: ScrollIndicatorMode.Default
            dataModel: ArrayDataModel { id: emojiModel }
            layout: GridListLayout {
                columnCount: 7
                cellAspectRatio: 1.0
                headerMode: ListHeaderMode.None
            }
            onTriggered: {
                var d = emojiModel.data(indexPath);
                if (d) emojiPanel.emojiPicked(d.ch);
            }
            listItemComponents: [
                ListItemComponent {
                    type: ""
                    Container {
                        layout: DockLayout {}
                        horizontalAlignment: HorizontalAlignment.Fill
                        verticalAlignment: VerticalAlignment.Fill
                        Label {
                            horizontalAlignment: HorizontalAlignment.Center
                            verticalAlignment: VerticalAlignment.Center
                            text: ListItemData.ch
                            textStyle.fontSize: FontSize.XLarge
                            textStyle.textAlign: TextAlign.Center
                        }
                    }
                }
            ]
        }
    }

    StickerGrid {
        id: stickerGrid
        visible: emojiPanel.activePackId !== "" && emojiPanel.activePackId !== "__zalo__"
        isDark: emojiPanel.isDark
        onStickerTapped: { emojiPanel.stickerPicked(path); }
    }

    ZaloStickerPanel {
        id: zaloPanel
        visible: emojiPanel.activePackId === "__zalo__"
        isDark: emojiPanel.isDark
        onStickerTapped: { emojiPanel.zaloStickerPicked(stickerId, cateId, type); }
    }

}
