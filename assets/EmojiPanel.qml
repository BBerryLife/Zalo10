import bb.cascades 1.4
import QtQuick 1.0
import "emoji"
import "sticker"

Container {
    id: emojiPanel
    background: isDark ? Color.create("#1f1f1f") : Color.create("#e8e8e8")

    signal emojiPicked(string charStr)
    signal stickerPicked(string path)
    signal stickerShopRequested()
    signal stickerSettingsRequested()

    property real   pageWidth:       0
    property int    categoryIndex:   0
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
        if (emojiPanel.activePackId === "") return;
        for (var i = 0; i < list.length; i++) {
            if (list[i].id === emojiPanel.activePackId) { stickerGrid.load(emojiPanel.activePackId); return; }
        }
        emojiPanel.showEmoji();   // the pack was disabled or removed
    }
    onCreationCompleted: reloadPacks()

    function fileToChar(filename) {
        var s = filename.replace("emoji_", "").replace("_64.png", "");
        var parts = s.split("_");
        var result = "";
        for (var i = 0; i < parts.length; i++) {
            var cp = parseInt(parts[i], 16);
            if (cp > 0xFFFF) {
                cp -= 0x10000;
                result += String.fromCharCode(0xD800 + (cp >> 10), 0xDC00 + (cp & 0x3FF));
            } else {
                result += String.fromCharCode(cp);
            }
        }
        return result;
    }

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
        onShopTapped: { emojiPanel.stickerShopRequested(); }
        onSettingsTapped: { emojiPanel.stickerSettingsRequested(); }
    }

    Container {
        id: emojiSection
        visible: emojiPanel.activePackId === ""
        horizontalAlignment: HorizontalAlignment.Fill
        layoutProperties: StackLayoutProperties { spaceQuota: 1 }
        layout: StackLayout { orientation: LayoutOrientation.TopToBottom }

        EmojiPeoplePage {
            visible: categoryIndex === 0
            pageWidth: emojiPanel.pageWidth
            onEmojiFilePicked: { emojiPicked(fileToChar(file)) }
        }

        EmojiNaturePage {
            visible: categoryIndex === 1
            pageWidth: emojiPanel.pageWidth
            onEmojiFilePicked: { emojiPicked(fileToChar(file)) }
        }

        EmojiObjectsPage {
            visible: categoryIndex === 2
            pageWidth: emojiPanel.pageWidth
            onEmojiFilePicked: { emojiPicked(fileToChar(file)) }
        }

        EmojiPlacesPage {
            visible: categoryIndex === 3
            pageWidth: emojiPanel.pageWidth
            onEmojiFilePicked: { emojiPicked(fileToChar(file)) }
        }

        EmojiSymbolsPage {
            visible: categoryIndex === 4
            pageWidth: emojiPanel.pageWidth
            onEmojiFilePicked: { emojiPicked(fileToChar(file)) }
        }

        Container {
            horizontalAlignment: HorizontalAlignment.Fill
            background: emojiPanel.isDark ? Color.create("#272727") : Color.create("#C8C8C8")
            preferredHeight: ui.du(5.5)
            layout: StackLayout { orientation: LayoutOrientation.LeftToRight }

            Container {
                layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                layout: DockLayout {}
                background: categoryIndex === 0 ? (emojiPanel.isDark ? Color.create("#3a3a3a") : Color.create("#EFEFEF"))  : Color.Transparent
                onTouch: {
                    if (event.isUp()) categoryIndex = 0;
                }
                ImageView {
                    horizontalAlignment: HorizontalAlignment.Center
                    verticalAlignment: VerticalAlignment.Center
                    preferredWidth: ui.du(5); preferredHeight: ui.du(5)
                    imageSource: "asset:///images/ChatView/emoji/emoji_category_people.png"
                }
            }
            Container {
                layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                layout: DockLayout {}
                background: categoryIndex === 1 ? (emojiPanel.isDark ? Color.create("#3a3a3a") : Color.create("#EFEFEF"))  : Color.Transparent
                onTouch: {
                    if (event.isUp()) categoryIndex = 1;
                }
                ImageView {
                    horizontalAlignment: HorizontalAlignment.Center
                    verticalAlignment: VerticalAlignment.Center
                    preferredWidth: ui.du(5); preferredHeight: ui.du(5)
                    imageSource: "asset:///images/ChatView/emoji/emoji_category_nature.png"
                }
            }
            Container {
                layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                layout: DockLayout {}
                background: categoryIndex === 2 ? (emojiPanel.isDark ? Color.create("#3a3a3a") : Color.create("#EFEFEF"))  : Color.Transparent
                onTouch: {
                    if (event.isUp()) categoryIndex = 2;
                }
                ImageView {
                    horizontalAlignment: HorizontalAlignment.Center
                    verticalAlignment: VerticalAlignment.Center
                    preferredWidth: ui.du(5); preferredHeight: ui.du(5)
                    imageSource: "asset:///images/ChatView/emoji/emoji_category_objects.png"
                }
            }
            Container {
                layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                layout: DockLayout {}
                background: categoryIndex === 3 ? (emojiPanel.isDark ? Color.create("#3a3a3a") : Color.create("#EFEFEF"))  : Color.Transparent
                onTouch: {
                    if (event.isUp()) categoryIndex = 3;
                }
                ImageView {
                    horizontalAlignment: HorizontalAlignment.Center
                    verticalAlignment: VerticalAlignment.Center
                    preferredWidth: ui.du(5); preferredHeight: ui.du(5)
                    imageSource: "asset:///images/ChatView/emoji/emoji_category_places.png"
                }
            }
            Container {
                layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                layout: DockLayout {}
                background: categoryIndex === 4 ? (emojiPanel.isDark ? Color.create("#3a3a3a") : Color.create("#EFEFEF"))  : Color.Transparent
                onTouch: {
                    if (event.isUp()) categoryIndex = 4;
                }
                ImageView {
                    horizontalAlignment: HorizontalAlignment.Center
                    verticalAlignment: VerticalAlignment.Center
                    preferredWidth: ui.du(5); preferredHeight: ui.du(5)
                    imageSource: "asset:///images/ChatView/emoji/emoji_category_symbols.png"
                }
            }
        }
    }

    StickerGrid {
        id: stickerGrid
        visible: emojiPanel.activePackId !== ""
        isDark: emojiPanel.isDark
        onStickerTapped: { emojiPanel.stickerPicked(path); }
    }

}
