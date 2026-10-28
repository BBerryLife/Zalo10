import bb.cascades 1.4

Container {
    signal emojiFilePicked(string file)

    property real pageWidth: 0
    property int  pagePlaces: 0

    horizontalAlignment: HorizontalAlignment.Fill
    layoutProperties: StackLayoutProperties { spaceQuota: 1 }
    layout: StackLayout { orientation: LayoutOrientation.TopToBottom }

    Container {
        horizontalAlignment: HorizontalAlignment.Center
        preferredHeight: ui.du(2.5)
        layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
        ImageView {
            preferredWidth: ui.du(1.2); preferredHeight: ui.du(1.2)
            verticalAlignment: VerticalAlignment.Center
            leftMargin: ui.du(0.2); rightMargin: ui.du(0.2)
            imageSource: pagePlaces === 0
                ? "asset:///images/ChatView/emoji/activePageDot.png"
                : "asset:///images/ChatView/emoji/inactivePageDot.png"
        }
        ImageView {
            preferredWidth: ui.du(1.2); preferredHeight: ui.du(1.2)
            verticalAlignment: VerticalAlignment.Center
            leftMargin: ui.du(0.2); rightMargin: ui.du(0.2)
            imageSource: pagePlaces === 1
                ? "asset:///images/ChatView/emoji/activePageDot.png"
                : "asset:///images/ChatView/emoji/inactivePageDot.png"
        }
        ImageView {
            preferredWidth: ui.du(1.2); preferredHeight: ui.du(1.2)
            verticalAlignment: VerticalAlignment.Center
            leftMargin: ui.du(0.2); rightMargin: ui.du(0.2)
            imageSource: pagePlaces === 2
                ? "asset:///images/ChatView/emoji/activePageDot.png"
                : "asset:///images/ChatView/emoji/inactivePageDot.png"
        }
        ImageView {
            preferredWidth: ui.du(1.2); preferredHeight: ui.du(1.2)
            verticalAlignment: VerticalAlignment.Center
            leftMargin: ui.du(0.2); rightMargin: ui.du(0.2)
            imageSource: pagePlaces === 3
                ? "asset:///images/ChatView/emoji/activePageDot.png"
                : "asset:///images/ChatView/emoji/inactivePageDot.png"
        }
        ImageView {
            preferredWidth: ui.du(1.2); preferredHeight: ui.du(1.2)
            verticalAlignment: VerticalAlignment.Center
            leftMargin: ui.du(0.2); rightMargin: ui.du(0.2)
            imageSource: pagePlaces === 4
                ? "asset:///images/ChatView/emoji/activePageDot.png"
                : "asset:///images/ChatView/emoji/inactivePageDot.png"
        }
        ImageView {
            preferredWidth: ui.du(1.2); preferredHeight: ui.du(1.2)
            verticalAlignment: VerticalAlignment.Center
            leftMargin: ui.du(0.2); rightMargin: ui.du(0.2)
            imageSource: pagePlaces === 5
                ? "asset:///images/ChatView/emoji/activePageDot.png"
                : "asset:///images/ChatView/emoji/inactivePageDot.png"
        }
    }

    ScrollView {
        horizontalAlignment: HorizontalAlignment.Fill
        layoutProperties: StackLayoutProperties { spaceQuota: 1 }
        scrollViewProperties {
            scrollMode: ScrollMode.Horizontal
            pinchToZoomEnabled: false
        }
        onViewableAreaChanging: {
            if (pageWidth > 0) {
                var pg = Math.round(viewableArea.x / pageWidth);
                if (pg < 0) pg = 0;
                if (pg >= 6) pg = 6 - 1;
                pagePlaces = pg;
            }
        }
        Container {
            layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
            Container {
                preferredWidth: pageWidth > 0 ? pageWidth : ui.du(52)
                layout: GridLayout { columnCount: 6 }
                horizontalAlignment: HorizontalAlignment.Fill
                topPadding: ui.du(0.5); bottomPadding: ui.du(0.5); leftPadding: ui.du(1); rightPadding: ui.du(1)
                EmojiButton { emojiFile: "emoji_1f3e0_64.png"; emojiCategory: "places"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f3e1_64.png"; emojiCategory: "places"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f3eb_64.png"; emojiCategory: "places"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f3e2_64.png"; emojiCategory: "places"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f3e3_64.png"; emojiCategory: "places"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f3e5_64.png"; emojiCategory: "places"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f3e6_64.png"; emojiCategory: "places"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f3ea_64.png"; emojiCategory: "places"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f3e9_64.png"; emojiCategory: "places"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f3e8_64.png"; emojiCategory: "places"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f492_64.png"; emojiCategory: "places"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_26ea_64.png"; emojiCategory: "places"; onEmojiTapped: { emojiFilePicked(file) } }
            }
            Container {
                preferredWidth: pageWidth > 0 ? pageWidth : ui.du(52)
                layout: GridLayout { columnCount: 6 }
                horizontalAlignment: HorizontalAlignment.Fill
                topPadding: ui.du(0.5); bottomPadding: ui.du(0.5); leftPadding: ui.du(1); rightPadding: ui.du(1)
                EmojiButton { emojiFile: "emoji_1f3ec_64.png"; emojiCategory: "places"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f307_64.png"; emojiCategory: "places"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f306_64.png"; emojiCategory: "places"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f3ef_64.png"; emojiCategory: "places"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f3f0_64.png"; emojiCategory: "places"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_26fa_64.png"; emojiCategory: "places"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f3ed_64.png"; emojiCategory: "places"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f5fc_64.png"; emojiCategory: "places"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f5fb_64.png"; emojiCategory: "places"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f304_64.png"; emojiCategory: "places"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f305_64.png"; emojiCategory: "places"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f303_64.png"; emojiCategory: "places"; onEmojiTapped: { emojiFilePicked(file) } }
            }
            Container {
                preferredWidth: pageWidth > 0 ? pageWidth : ui.du(52)
                layout: GridLayout { columnCount: 6 }
                horizontalAlignment: HorizontalAlignment.Fill
                topPadding: ui.du(0.5); bottomPadding: ui.du(0.5); leftPadding: ui.du(1); rightPadding: ui.du(1)
                EmojiButton { emojiFile: "emoji_1f5fd_64.png"; emojiCategory: "places"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f3a1_64.png"; emojiCategory: "places"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_26f2_64.png"; emojiCategory: "places"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f3a2_64.png"; emojiCategory: "places"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f6a2_64.png"; emojiCategory: "places"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_26f5_64.png"; emojiCategory: "places"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f6a4_64.png"; emojiCategory: "places"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f680_64.png"; emojiCategory: "places"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_2708_64.png"; emojiCategory: "places"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f4ba_64.png"; emojiCategory: "places"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f689_64.png"; emojiCategory: "places"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f684_64.png"; emojiCategory: "places"; onEmojiTapped: { emojiFilePicked(file) } }
            }
            Container {
                preferredWidth: pageWidth > 0 ? pageWidth : ui.du(52)
                layout: GridLayout { columnCount: 6 }
                horizontalAlignment: HorizontalAlignment.Fill
                topPadding: ui.du(0.5); bottomPadding: ui.du(0.5); leftPadding: ui.du(1); rightPadding: ui.du(1)
                EmojiButton { emojiFile: "emoji_1f685_64.png"; emojiCategory: "places"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f683_64.png"; emojiCategory: "places"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f68c_64.png"; emojiCategory: "places"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f699_64.png"; emojiCategory: "places"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f697_64.png"; emojiCategory: "places"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f695_64.png"; emojiCategory: "places"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f69a_64.png"; emojiCategory: "places"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f693_64.png"; emojiCategory: "places"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f692_64.png"; emojiCategory: "places"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f691_64.png"; emojiCategory: "places"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f6b2_64.png"; emojiCategory: "places"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f488_64.png"; emojiCategory: "places"; onEmojiTapped: { emojiFilePicked(file) } }
            }
            Container {
                preferredWidth: pageWidth > 0 ? pageWidth : ui.du(52)
                layout: GridLayout { columnCount: 6 }
                horizontalAlignment: HorizontalAlignment.Fill
                topPadding: ui.du(0.5); bottomPadding: ui.du(0.5); leftPadding: ui.du(1); rightPadding: ui.du(1)
                EmojiButton { emojiFile: "emoji_1f68f_64.png"; emojiCategory: "places"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f3ab_64.png"; emojiCategory: "places"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f6a5_64.png"; emojiCategory: "places"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_26a0_64.png"; emojiCategory: "places"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f6a7_64.png"; emojiCategory: "places"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f530_64.png"; emojiCategory: "places"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_26fd_64.png"; emojiCategory: "places"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_2668_64.png"; emojiCategory: "places"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f3ad_64.png"; emojiCategory: "places"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f1ef_1f1f5_64.png"; emojiCategory: "places"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f1f0_1f1f7_64.png"; emojiCategory: "places"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f1e9_1f1ea_64.png"; emojiCategory: "places"; onEmojiTapped: { emojiFilePicked(file) } }
            }
            Container {
                preferredWidth: pageWidth > 0 ? pageWidth : ui.du(52)
                layout: GridLayout { columnCount: 6 }
                horizontalAlignment: HorizontalAlignment.Fill
                topPadding: ui.du(0.5); bottomPadding: ui.du(0.5); leftPadding: ui.du(1); rightPadding: ui.du(1)
                EmojiButton { emojiFile: "emoji_1f1e8_1f1f3_64.png"; emojiCategory: "places"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f1fa_1f1f8_64.png"; emojiCategory: "places"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f1eb_1f1f7_64.png"; emojiCategory: "places"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f1ea_1f1f8_64.png"; emojiCategory: "places"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f1ee_1f1f9_64.png"; emojiCategory: "places"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f1f7_1f1fa_64.png"; emojiCategory: "places"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f1ec_1f1e7_64.png"; emojiCategory: "places"; onEmojiTapped: { emojiFilePicked(file) } }
            }
        }
    }
}
