import bb.cascades 1.4

Container {
    signal emojiFilePicked(string file)

    property real pageWidth: 0
    property int  pageNature: 0

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
            imageSource: pageNature === 0
                ? "asset:///images/ChatView/emoji/activePageDot.png"
                : "asset:///images/ChatView/emoji/inactivePageDot.png"
        }
        ImageView {
            preferredWidth: ui.du(1.2); preferredHeight: ui.du(1.2)
            verticalAlignment: VerticalAlignment.Center
            leftMargin: ui.du(0.2); rightMargin: ui.du(0.2)
            imageSource: pageNature === 1
                ? "asset:///images/ChatView/emoji/activePageDot.png"
                : "asset:///images/ChatView/emoji/inactivePageDot.png"
        }
        ImageView {
            preferredWidth: ui.du(1.2); preferredHeight: ui.du(1.2)
            verticalAlignment: VerticalAlignment.Center
            leftMargin: ui.du(0.2); rightMargin: ui.du(0.2)
            imageSource: pageNature === 2
                ? "asset:///images/ChatView/emoji/activePageDot.png"
                : "asset:///images/ChatView/emoji/inactivePageDot.png"
        }
        ImageView {
            preferredWidth: ui.du(1.2); preferredHeight: ui.du(1.2)
            verticalAlignment: VerticalAlignment.Center
            leftMargin: ui.du(0.2); rightMargin: ui.du(0.2)
            imageSource: pageNature === 3
                ? "asset:///images/ChatView/emoji/activePageDot.png"
                : "asset:///images/ChatView/emoji/inactivePageDot.png"
        }
        ImageView {
            preferredWidth: ui.du(1.2); preferredHeight: ui.du(1.2)
            verticalAlignment: VerticalAlignment.Center
            leftMargin: ui.du(0.2); rightMargin: ui.du(0.2)
            imageSource: pageNature === 4
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
                if (pg >= 5) pg = 5 - 1;
                pageNature = pg;
            }
        }
        Container {
            layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
            Container {
                preferredWidth: pageWidth > 0 ? pageWidth : ui.du(52)
                layout: GridLayout { columnCount: 6 }
                horizontalAlignment: HorizontalAlignment.Fill
                topPadding: ui.du(0.5); bottomPadding: ui.du(0.5); leftPadding: ui.du(1); rightPadding: ui.du(1)
                EmojiButton { emojiFile: "emoji_1f436_64.png"; emojiCategory: "nature"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f43a_64.png"; emojiCategory: "nature"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f431_64.png"; emojiCategory: "nature"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f42d_64.png"; emojiCategory: "nature"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f439_64.png"; emojiCategory: "nature"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f430_64.png"; emojiCategory: "nature"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f438_64.png"; emojiCategory: "nature"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f42f_64.png"; emojiCategory: "nature"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f428_64.png"; emojiCategory: "nature"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f43b_64.png"; emojiCategory: "nature"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f437_64.png"; emojiCategory: "nature"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f42e_64.png"; emojiCategory: "nature"; onEmojiTapped: { emojiFilePicked(file) } }
            }
            Container {
                preferredWidth: pageWidth > 0 ? pageWidth : ui.du(52)
                layout: GridLayout { columnCount: 6 }
                horizontalAlignment: HorizontalAlignment.Fill
                topPadding: ui.du(0.5); bottomPadding: ui.du(0.5); leftPadding: ui.du(1); rightPadding: ui.du(1)
                EmojiButton { emojiFile: "emoji_1f417_64.png"; emojiCategory: "nature"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f435_64.png"; emojiCategory: "nature"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f412_64.png"; emojiCategory: "nature"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f434_64.png"; emojiCategory: "nature"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f411_64.png"; emojiCategory: "nature"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f418_64.png"; emojiCategory: "nature"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f427_64.png"; emojiCategory: "nature"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f426_64.png"; emojiCategory: "nature"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f425_64.png"; emojiCategory: "nature"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f414_64.png"; emojiCategory: "nature"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f40d_64.png"; emojiCategory: "nature"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f41b_64.png"; emojiCategory: "nature"; onEmojiTapped: { emojiFilePicked(file) } }
            }
            Container {
                preferredWidth: pageWidth > 0 ? pageWidth : ui.du(52)
                layout: GridLayout { columnCount: 6 }
                horizontalAlignment: HorizontalAlignment.Fill
                topPadding: ui.du(0.5); bottomPadding: ui.du(0.5); leftPadding: ui.du(1); rightPadding: ui.du(1)
                EmojiButton { emojiFile: "emoji_1f419_64.png"; emojiCategory: "nature"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f41a_64.png"; emojiCategory: "nature"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f420_64.png"; emojiCategory: "nature"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f41f_64.png"; emojiCategory: "nature"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f42c_64.png"; emojiCategory: "nature"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f433_64.png"; emojiCategory: "nature"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f40e_64.png"; emojiCategory: "nature"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f421_64.png"; emojiCategory: "nature"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f42b_64.png"; emojiCategory: "nature"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f429_64.png"; emojiCategory: "nature"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f43e_64.png"; emojiCategory: "nature"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f490_64.png"; emojiCategory: "nature"; onEmojiTapped: { emojiFilePicked(file) } }
            }
            Container {
                preferredWidth: pageWidth > 0 ? pageWidth : ui.du(52)
                layout: GridLayout { columnCount: 6 }
                horizontalAlignment: HorizontalAlignment.Fill
                topPadding: ui.du(0.5); bottomPadding: ui.du(0.5); leftPadding: ui.du(1); rightPadding: ui.du(1)
                EmojiButton { emojiFile: "emoji_1f338_64.png"; emojiCategory: "nature"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f337_64.png"; emojiCategory: "nature"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f340_64.png"; emojiCategory: "nature"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f339_64.png"; emojiCategory: "nature"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f33b_64.png"; emojiCategory: "nature"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f33a_64.png"; emojiCategory: "nature"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f341_64.png"; emojiCategory: "nature"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f343_64.png"; emojiCategory: "nature"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f342_64.png"; emojiCategory: "nature"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f33e_64.png"; emojiCategory: "nature"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f335_64.png"; emojiCategory: "nature"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f334_64.png"; emojiCategory: "nature"; onEmojiTapped: { emojiFilePicked(file) } }
            }
            Container {
                preferredWidth: pageWidth > 0 ? pageWidth : ui.du(52)
                layout: GridLayout { columnCount: 6 }
                horizontalAlignment: HorizontalAlignment.Fill
                topPadding: ui.du(0.5); bottomPadding: ui.du(0.5); leftPadding: ui.du(1); rightPadding: ui.du(1)
                EmojiButton { emojiFile: "emoji_1f331_64.png"; emojiCategory: "nature"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f319_64.png"; emojiCategory: "nature"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_2b50_64.png"; emojiCategory: "nature"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_2600_64.png"; emojiCategory: "nature"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_2601_64.png"; emojiCategory: "nature"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_26a1_64.png"; emojiCategory: "nature"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_2614_64.png"; emojiCategory: "nature"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_26c4_64.png"; emojiCategory: "nature"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f300_64.png"; emojiCategory: "nature"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f308_64.png"; emojiCategory: "nature"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f30a_64.png"; emojiCategory: "nature"; onEmojiTapped: { emojiFilePicked(file) } }
            }
        }
    }
}
