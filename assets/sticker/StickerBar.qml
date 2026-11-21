import bb.cascades 1.4

// Top bar of the emoji/sticker panel: [emoji] [shop] | installed packs ... | [settings].
// Pure view: it only emits what was tapped, EmojiPanel decides what to show.
Container {
    id: stickerBar

    property bool    isDark: false
    property string  activePackId: ""          // "" = the emoji keyboard is showing
    property variant packs: []                 // [{id, name, thumb, enabled}]

    signal emojiTapped()
    signal shopTapped()
    signal settingsTapped()
    signal packTapped(string packId)

    horizontalAlignment: HorizontalAlignment.Fill
    preferredHeight: ui.du(7.5)
    background: isDark ? Color.create("#272727") : Color.create("#C8C8C8")
    layout: StackLayout { orientation: LayoutOrientation.LeftToRight }

    function highlight() { return stickerBar.isDark ? "#3a3a3a" : "#EFEFEF"; }

    // The tab strip is a ListView (the only native way to get a recycled, horizontally scrolling
    // row in Cascades); it is rebuilt whenever the packs, the selection or the theme change.
    function rebuild() {
        tabsModel.clear();
        var list = stickerBar.packs;
        for (var i = 0; i < list.length; i++) {
            tabsModel.append({
                id:       list[i].id,
                thumb:    list[i].thumb,
                selected: (list[i].id === stickerBar.activePackId),
                bg:       stickerBar.highlight()
            });
        }
    }
    onPacksChanged: rebuild()
    onActivePackIdChanged: rebuild()
    onIsDarkChanged: rebuild()

    attachedObjects: [
        ArrayDataModel { id: tabsModel }
    ]

    // Emoji keyboard
    Container {
        preferredWidth: ui.du(9)
        verticalAlignment: VerticalAlignment.Fill
        layout: DockLayout {}
        background: stickerBar.activePackId === "" ? Color.create(stickerBar.highlight()) : Color.Transparent
        onTouch: { if (event.isUp()) stickerBar.emojiTapped(); }
        ImageView {
            horizontalAlignment: HorizontalAlignment.Center
            verticalAlignment: VerticalAlignment.Center
            preferredWidth: ui.du(5); preferredHeight: ui.du(5)
            imageSource: "asset:///images/ChatView/ic_emoticon_enabled_white.png"
            filterColor: stickerBar.isDark ? Color.White : Color.Black
        }
    }

    // Shop
    Container {
        preferredWidth: ui.du(9)
        verticalAlignment: VerticalAlignment.Fill
        layout: DockLayout {}
        onTouch: { if (event.isUp()) stickerBar.shopTapped(); }
        ImageView {
            horizontalAlignment: HorizontalAlignment.Center
            verticalAlignment: VerticalAlignment.Center
            preferredWidth: ui.du(5); preferredHeight: ui.du(5)
            imageSource: "asset:///images/ic_buy.png"
            filterColor: stickerBar.isDark ? Color.White : Color.Black
        }
    }

    Container {
        preferredWidth: ui.du(0.2)
        verticalAlignment: VerticalAlignment.Fill
        topMargin: ui.du(1.2); bottomMargin: ui.du(1.2)
        background: stickerBar.isDark ? Color.create("#4a4a4a") : Color.create("#9a9a9a")
    }

    ListView {
        id: tabsList
        layoutProperties: StackLayoutProperties { spaceQuota: 1 }
        verticalAlignment: VerticalAlignment.Fill
        layout: StackListLayout { orientation: LayoutOrientation.LeftToRight }
        scrollIndicatorMode: ScrollIndicatorMode.None
        dataModel: tabsModel
        onTriggered: {
            var d = tabsModel.data(indexPath);
            if (d) stickerBar.packTapped(d.id);
        }
        listItemComponents: [
            ListItemComponent {
                type: ""
                Container {
                    preferredWidth: ui.du(9)
                    preferredHeight: ui.du(7.5)
                    layout: DockLayout {}
                    background: ListItemData.selected ? Color.create(ListItemData.bg) : Color.Transparent
                    ImageView {
                        horizontalAlignment: HorizontalAlignment.Center
                        verticalAlignment: VerticalAlignment.Center
                        preferredWidth: ui.du(6); preferredHeight: ui.du(6)
                        scalingMethod: ScalingMethod.AspectFit
                        imageSource: ListItemData.thumb
                    }
                }
            }
        ]
    }

    // Settings
    Container {
        preferredWidth: ui.du(9)
        verticalAlignment: VerticalAlignment.Fill
        layout: DockLayout {}
        onTouch: { if (event.isUp()) stickerBar.settingsTapped(); }
        ImageView {
            horizontalAlignment: HorizontalAlignment.Center
            verticalAlignment: VerticalAlignment.Center
            preferredWidth: ui.du(5); preferredHeight: ui.du(5)
            imageSource: "asset:///images/ic_settings.png"
            filterColor: stickerBar.isDark ? Color.White : Color.Black
        }
    }
}
