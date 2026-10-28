import bb.cascades 1.4

Container {
    signal emojiFilePicked(string file)

    property real pageWidth: 0
    property int  pageSymbols: 0

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
            imageSource: pageSymbols === 0
                ? "asset:///images/ChatView/emoji/activePageDot.png"
                : "asset:///images/ChatView/emoji/inactivePageDot.png"
        }
        ImageView {
            preferredWidth: ui.du(1.2); preferredHeight: ui.du(1.2)
            verticalAlignment: VerticalAlignment.Center
            leftMargin: ui.du(0.2); rightMargin: ui.du(0.2)
            imageSource: pageSymbols === 1
                ? "asset:///images/ChatView/emoji/activePageDot.png"
                : "asset:///images/ChatView/emoji/inactivePageDot.png"
        }
        ImageView {
            preferredWidth: ui.du(1.2); preferredHeight: ui.du(1.2)
            verticalAlignment: VerticalAlignment.Center
            leftMargin: ui.du(0.2); rightMargin: ui.du(0.2)
            imageSource: pageSymbols === 2
                ? "asset:///images/ChatView/emoji/activePageDot.png"
                : "asset:///images/ChatView/emoji/inactivePageDot.png"
        }
        ImageView {
            preferredWidth: ui.du(1.2); preferredHeight: ui.du(1.2)
            verticalAlignment: VerticalAlignment.Center
            leftMargin: ui.du(0.2); rightMargin: ui.du(0.2)
            imageSource: pageSymbols === 3
                ? "asset:///images/ChatView/emoji/activePageDot.png"
                : "asset:///images/ChatView/emoji/inactivePageDot.png"
        }
        ImageView {
            preferredWidth: ui.du(1.2); preferredHeight: ui.du(1.2)
            verticalAlignment: VerticalAlignment.Center
            leftMargin: ui.du(0.2); rightMargin: ui.du(0.2)
            imageSource: pageSymbols === 4
                ? "asset:///images/ChatView/emoji/activePageDot.png"
                : "asset:///images/ChatView/emoji/inactivePageDot.png"
        }
        ImageView {
            preferredWidth: ui.du(1.2); preferredHeight: ui.du(1.2)
            verticalAlignment: VerticalAlignment.Center
            leftMargin: ui.du(0.2); rightMargin: ui.du(0.2)
            imageSource: pageSymbols === 5
                ? "asset:///images/ChatView/emoji/activePageDot.png"
                : "asset:///images/ChatView/emoji/inactivePageDot.png"
        }
        ImageView {
            preferredWidth: ui.du(1.2); preferredHeight: ui.du(1.2)
            verticalAlignment: VerticalAlignment.Center
            leftMargin: ui.du(0.2); rightMargin: ui.du(0.2)
            imageSource: pageSymbols === 6
                ? "asset:///images/ChatView/emoji/activePageDot.png"
                : "asset:///images/ChatView/emoji/inactivePageDot.png"
        }
        ImageView {
            preferredWidth: ui.du(1.2); preferredHeight: ui.du(1.2)
            verticalAlignment: VerticalAlignment.Center
            leftMargin: ui.du(0.2); rightMargin: ui.du(0.2)
            imageSource: pageSymbols === 7
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
                if (pg >= 8) pg = 8 - 1;
                pageSymbols = pg;
            }
        }
        Container {
            layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
            Container {
                preferredWidth: pageWidth > 0 ? pageWidth : ui.du(52)
                layout: GridLayout { columnCount: 6 }
                horizontalAlignment: HorizontalAlignment.Fill
                topPadding: ui.du(0.5); bottomPadding: ui.du(0.5); leftPadding: ui.du(1); rightPadding: ui.du(1)
                EmojiButton { emojiFile: "emoji_31_20e3_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_32_20e3_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_33_20e3_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_34_20e3_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_35_20e3_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_36_20e3_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_37_20e3_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_38_20e3_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_39_20e3_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_30_20e3_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_23_20e3_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_2b06_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
            }
            Container {
                preferredWidth: pageWidth > 0 ? pageWidth : ui.du(52)
                layout: GridLayout { columnCount: 6 }
                horizontalAlignment: HorizontalAlignment.Fill
                topPadding: ui.du(0.5); bottomPadding: ui.du(0.5); leftPadding: ui.du(1); rightPadding: ui.du(1)
                EmojiButton { emojiFile: "emoji_2b07_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_2b05_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_27a1_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_2197_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_2196_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_2198_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_2199_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_2935_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_2934_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f199_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f192_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f4f6_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
            }
            Container {
                preferredWidth: pageWidth > 0 ? pageWidth : ui.du(52)
                layout: GridLayout { columnCount: 6 }
                horizontalAlignment: HorizontalAlignment.Fill
                topPadding: ui.du(0.5); bottomPadding: ui.du(0.5); leftPadding: ui.du(1); rightPadding: ui.du(1)
                EmojiButton { emojiFile: "emoji_1f3a6_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f201_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f22f_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f233_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f235_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f250_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f239_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f23a_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f236_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f21a_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f6bb_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f6b9_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
            }
            Container {
                preferredWidth: pageWidth > 0 ? pageWidth : ui.du(52)
                layout: GridLayout { columnCount: 6 }
                horizontalAlignment: HorizontalAlignment.Fill
                topPadding: ui.du(0.5); bottomPadding: ui.du(0.5); leftPadding: ui.du(1); rightPadding: ui.du(1)
                EmojiButton { emojiFile: "emoji_1f6ba_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f6bc_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f6be_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_267f_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f6ad_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f237_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f238_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f202_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_3299_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_3297_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f51e_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_26d4_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
            }
            Container {
                preferredWidth: pageWidth > 0 ? pageWidth : ui.du(52)
                layout: GridLayout { columnCount: 6 }
                horizontalAlignment: HorizontalAlignment.Fill
                topPadding: ui.du(0.5); bottomPadding: ui.du(0.5); leftPadding: ui.du(1); rightPadding: ui.du(1)
                EmojiButton { emojiFile: "emoji_2733_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_274e_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_2734_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f49f_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f19a_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f4f3_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f4f4_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f4a0_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_27bf_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f3e7_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f4b2_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_303d_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
            }
            Container {
                preferredWidth: pageWidth > 0 ? pageWidth : ui.du(52)
                layout: GridLayout { columnCount: 6 }
                horizontalAlignment: HorizontalAlignment.Fill
                topPadding: ui.du(0.5); bottomPadding: ui.du(0.5); leftPadding: ui.du(1); rightPadding: ui.du(1)
                EmojiButton { emojiFile: "emoji_274c_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_2b55_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_2757_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_2753_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_2755_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_2754_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_2716_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_2660_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_2665_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_2663_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_2666_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f531_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
            }
            Container {
                preferredWidth: pageWidth > 0 ? pageWidth : ui.du(52)
                layout: GridLayout { columnCount: 6 }
                horizontalAlignment: HorizontalAlignment.Fill
                topPadding: ui.du(0.5); bottomPadding: ui.du(0.5); leftPadding: ui.du(1); rightPadding: ui.du(1)
                EmojiButton { emojiFile: "emoji_1f532_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f533_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_25fc_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_25fb_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_25fe_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_25fd_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_25aa_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_25ab_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_2b1c_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_2b1b_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_26ab_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_26aa_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
            }
            Container {
                preferredWidth: pageWidth > 0 ? pageWidth : ui.du(52)
                layout: GridLayout { columnCount: 6 }
                horizontalAlignment: HorizontalAlignment.Fill
                topPadding: ui.du(0.5); bottomPadding: ui.du(0.5); leftPadding: ui.du(1); rightPadding: ui.du(1)
                EmojiButton { emojiFile: "emoji_1f534_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f535_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f536_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f537_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f538_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
                EmojiButton { emojiFile: "emoji_1f539_64.png"; emojiCategory: "symbols"; onEmojiTapped: { emojiFilePicked(file) } }
            }
        }
    }
}
