import bb.cascades 1.4
import QtQuick 1.0

Page {
    id: chatViewPage

    property string threadId:    ""
    property string threadName:  ""
    property bool   isGroup:     false
    property string avatarUrl:   ""
    property string selfName:    "Me"
    property string pendingMsg:  ""
    property bool   initialized: false
    property bool   emojiPanelOpen:    false
    property int    emojiCategoryIndex: 0
    property int    emojiPagePeople: 0
    property int    emojiPageNature: 0
    property int    emojiPageObjects: 0
    property int    emojiPagePlaces: 0
    property int    emojiPageSymbols: 0
    property real   emojiPageWidth: 0

    // ─── Convert emoji filename to Unicode character ──────────────
    function emojiFileToChar(filename) {
        // filename: "emoji_1f604_64.png" or "emoji_1f1ef_1f1f5_64.png"
        var s = filename.replace("emoji_", "").replace("_64.png", "");
        var parts = s.split("_");
        var result = "";
        for (var i = 0; i < parts.length; i++) {
            var cp = parseInt(parts[i], 16);
            if (cp > 0xFFFF) {
                // Surrogate pair
                cp -= 0x10000;
                result += String.fromCharCode(0xD800 + (cp >> 10), 0xDC00 + (cp & 0x3FF));
            } else {
                result += String.fromCharCode(cp);
            }
        }
        return result;
    }


    // ─── TITLE BAR ───────────────────────────────────────────────
    titleBar: TitleBar {
        scrollBehavior: TitleBarScrollBehavior.Sticky
        kind: TitleBarKind.FreeForm
        kindProperties: FreeFormTitleBarKindProperties {
            content: Container {
                background: Color.create("#2575fc")
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment:   VerticalAlignment.Fill
                layout: StackLayout { orientation: LayoutOrientation.LeftToRight }

                Container {
                    id: avatarBlock
                    verticalAlignment: VerticalAlignment.Fill
                    layout: DockLayout {}
                    preferredWidth:  titleBarLUH.layoutFrame.height > 0
                                     ? titleBarLUH.layoutFrame.height : ui.du(7)
                    minWidth:        titleBarLUH.layoutFrame.height > 0
                                     ? titleBarLUH.layoutFrame.height : ui.du(7)

                    ImageView {
                        horizontalAlignment: HorizontalAlignment.Fill
                        verticalAlignment:   VerticalAlignment.Fill
                        scalingMethod: ScalingMethod.AspectFill
                        imageSource: chatViewPage.avatarUrl
                        visible: chatViewPage.avatarUrl.length > 0
                    }
                    Container {
                        horizontalAlignment: HorizontalAlignment.Fill
                        verticalAlignment:   VerticalAlignment.Fill
                        background: Color.create("#1a5fc8")
                        layout: DockLayout {}
                        visible: chatViewPage.avatarUrl.length === 0
                        Label {
                            text: chatViewPage.threadName.length > 0
                                  ? chatViewPage.threadName.charAt(0).toUpperCase() : "?"
                            horizontalAlignment: HorizontalAlignment.Center
                            verticalAlignment:   VerticalAlignment.Center
                            textStyle { color: Color.White; fontSize: FontSize.XXLarge; fontWeight: FontWeight.Bold }
                        }
                    }
                    attachedObjects: [ LayoutUpdateHandler { id: titleBarLUH } ]
                }

                Container {
                    verticalAlignment: VerticalAlignment.Center
                    leftPadding: ui.du(1.5)
                    layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                    Label {
                        text: chatViewPage.threadName.length > 0 ? chatViewPage.threadName : "..."
                        textStyle { color: Color.White; base: SystemDefaults.TextStyles.TitleText; fontWeight: FontWeight.Bold }
                        topMargin: 0; bottomMargin: 0
                    }
                    Label {
                        text: chatViewPage.isGroup ? "Group" : "Zalo Contact"
                        textStyle { color: Color.create("#b3d4ff"); fontSize: FontSize.XSmall }
                        topMargin: ui.du(0.2); bottomMargin: 0
                    }
                }

                ImageButton {
                    verticalAlignment: VerticalAlignment.Center
                    preferredWidth: ui.du(7); preferredHeight: ui.du(7)
                    defaultImageSource: "asset:///images/ic_voice_call.png"
                    pressedImageSource: "asset:///images/ic_voice_call.png"
                    rightMargin: ui.du(0.3)
                    onClicked: { callSheet.open() }
                }
                ImageButton {
                    verticalAlignment: VerticalAlignment.Center
                    preferredWidth: ui.du(7); preferredHeight: ui.du(7)
                    defaultImageSource: "asset:///images/ca_video_chat_active.png"
                    pressedImageSource: "asset:///images/ca_video_chat_active.png"
                    rightMargin: ui.du(0.5)
                    onClicked: { videoCallSheet.open() }
                }
            }
        }
    }

    // Chờ cả threadId lẫn selfName được set trước khi load
    // main.qml assign threadId trước selfName nên cần cả 2 handler
    onThreadIdChanged: { chatViewPage.tryInit() }
    onSelfNameChanged: { chatViewPage.tryInit() }

    function tryInit() {
        // Chỉ init khi cả threadId và selfName đã có giá trị thực
        if (chatViewPage.threadId === "") return;
        if (chatViewPage.selfName === "" || chatViewPage.selfName === "Me") {
            // selfName chưa set từ server, chờ thêm — nhưng nếu threadId đã set
            // thì vẫn init với selfName mặc định nếu đã chờ đủ lâu
        }
        chatViewPage.doInit();
    }



    function doInit() {
        if (chatViewPage.threadId === "") return;
        if (chatViewPage.initialized) return;
        chatViewPage.initialized = true;

        // Đảm bảo model sạch trước khi load
        msgModel.clear();

        zService.setActiveThread(chatViewPage.threadId, chatViewPage.isGroup);

        // Load từ DB trước — selfName đã được set đúng lúc này
        var cached = zService.dbLoadMessages(chatViewPage.threadId);
        if (cached && cached.length > 0) {
            for (var i = 0; i < cached.length; i++) {
                var c = cached[i];
                c.selfName = chatViewPage.selfName;
                msgModel.append(c);
            }
            chatViewPage.rebuildGroups();
            msgList.scrollToPosition(ScrollPosition.End, ScrollAnimation.None);
        }

        // Fetch từ server
        zService.fetchMessages(chatViewPage.threadId, chatViewPage.isGroup);
    }

    // ─── CONTENT ─────────────────────────────────────────────────
    content: Container {
        layout: StackLayout { orientation: LayoutOrientation.TopToBottom }
        horizontalAlignment: HorizontalAlignment.Fill
        verticalAlignment:   VerticalAlignment.Fill
        background: Color.create("#d6d6d6")

        // ── Message list ─────────────────────────────────────────
        ListView {
            id: msgList
            horizontalAlignment: HorizontalAlignment.Fill
            layoutProperties: StackLayoutProperties { spaceQuota: 1 }
            dataModel: ArrayDataModel { id: msgModel }

            flickMode: FlickMode.Momentum

            listItemComponents: [
                ListItemComponent {
                    type: ""
                    Container {
                        id: rowRoot
                        horizontalAlignment: HorizontalAlignment.Fill
                        topPadding:    ListItemData.grouped === true ? 0 : 6
                        bottomPadding: 0

                        // Dùng LeftToRight + spaceQuota để force bubble fill đúng width
                        layout: StackLayout { orientation: LayoutOrientation.LeftToRight }

                        property bool mine: (ListItemData.isMine === true
                                             || ListItemData.isMine === "true"
                                             || ListItemData.isMine === 1)
                        property bool grouped: ListItemData.grouped === true

                        // Spacer trái: nhỏ nếu mine(trái), lớn nếu người khác(phải)
                        Container {
                            preferredWidth: rowRoot.mine ? 6 : 60
                            minWidth:       rowRoot.mine ? 6 : 60
                            maxWidth:       rowRoot.mine ? 6 : 60
                        }

                        // ── Bubble chiếm phần còn lại ─────────────────
                        Container {
                            layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                            background: Color.White
                            topPadding:    rowRoot.grouped ? 6 : 10
                            bottomPadding: 10
                            leftPadding:   14
                            rightPadding:  14

                            // Header: tên + timestamp — chỉ hiện ở tin ĐẦU nhóm
                            Container {
                                visible: !rowRoot.grouped
                                layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                                horizontalAlignment: HorizontalAlignment.Fill
                                bottomMargin: 2

                                Label {
                                    layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                                    text: rowRoot.mine
                                          ? (ListItemData.selfName || "Me")
                                          : (ListItemData.dName    || "Unknown")
                                    textStyle {
                                        fontSize:   FontSize.Small
                                        fontWeight: FontWeight.Bold
                                        color: rowRoot.mine
                                            ? Color.create("#555555")
                                            : Color.create("#0073BC")
                                    }
                                    topMargin: 0; bottomMargin: 0
                                }

                                Label {
                                    text: {
                                        var ts = ListItemData.latestTs || ListItemData.ts;
                                        if (!ts) return "";
                                        var n = ts * 1;
                                        if (n > 0 && n < 1e12) n *= 1000;
                                        var d   = new Date(n);
                                        var dow = ["Sun","Mon","Tue","Wed","Thu","Fri","Sat"][d.getDay()];
                                        var h   = d.getHours();
                                        var m2  = d.getMinutes();
                                        var ap  = h >= 12 ? "PM" : "AM";
                                        var h12 = h % 12; if (h12 === 0) h12 = 12;
                                        return dow + " " + h12 + ":" + (m2 < 10 ? "0" : "") + m2 + " " + ap;
                                    }
                                    horizontalAlignment: HorizontalAlignment.Right
                                    textStyle { fontSize: FontSize.XSmall; color: Color.create("#777777") }
                                    topMargin: 0; bottomMargin: 0
                                }
                            }

                            // Nội dung tin nhắn
                            Label {
                                text: {
                                    var c = ListItemData.content;
                                    if (typeof c === "string" && c.length > 0) return c;
                                    if (c && typeof c === "object" && c.content) return c.content;
                                    return "[Image/Sticker]";
                                }
                                textStyle {
                                    base:  SystemDefaults.TextStyles.BodyText
                                    color: Color.create("#111111")
                                }
                                multiline: true
                                topMargin: 0; bottomMargin: 0
                            }
                        } // end bubble

                        // Spacer phải: lớn nếu mine(trái), nhỏ nếu người khác(phải)
                        Container {
                            preferredWidth: rowRoot.mine ? 60 : 6
                            minWidth:       rowRoot.mine ? 60 : 6
                            maxWidth:       rowRoot.mine ? 60 : 6
                        }
                    } // end rowRoot
                }
            ]
        } // end ListView



        // ── Emoji Picker Panel ────────────────────────────────────
        Container {
            id: emojiPanel
            horizontalAlignment: HorizontalAlignment.Fill
            background: Color.create("#F0F0F0")
            visible: false
            preferredHeight: ui.du(21)
            minHeight:       ui.du(21)
            maxHeight:       ui.du(21)
            layout: StackLayout { orientation: LayoutOrientation.TopToBottom }

            // ── Measure page width ──────────────────────────────
            attachedObjects: [
                LayoutUpdateHandler {
                    id: panelLUH
                    onLayoutFrameChanged: {
                        if (layoutFrame.width > 0)
                            emojiPageWidth = layoutFrame.width;
                    }
                }
            ]


            // ── People ──
            Container {
                horizontalAlignment: HorizontalAlignment.Fill
                layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                visible: emojiCategoryIndex === 0
                layout: StackLayout { orientation: LayoutOrientation.TopToBottom }

                Container {
                    horizontalAlignment: HorizontalAlignment.Center
                    preferredHeight: ui.du(2.5)
                    layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                    ImageView {
                        preferredWidth: ui.du(1.2); preferredHeight: ui.du(1.2)
                        verticalAlignment: VerticalAlignment.Center
                        leftMargin: ui.du(0.2); rightMargin: ui.du(0.2)
                        imageSource: emojiPagePeople === 0
                            ? "asset:///images/emoji/activePageDot.png"
                            : "asset:///images/emoji/inactivePageDot.png"
                    }
                    ImageView {
                        preferredWidth: ui.du(1.2); preferredHeight: ui.du(1.2)
                        verticalAlignment: VerticalAlignment.Center
                        leftMargin: ui.du(0.2); rightMargin: ui.du(0.2)
                        imageSource: emojiPagePeople === 1
                            ? "asset:///images/emoji/activePageDot.png"
                            : "asset:///images/emoji/inactivePageDot.png"
                    }
                    ImageView {
                        preferredWidth: ui.du(1.2); preferredHeight: ui.du(1.2)
                        verticalAlignment: VerticalAlignment.Center
                        leftMargin: ui.du(0.2); rightMargin: ui.du(0.2)
                        imageSource: emojiPagePeople === 2
                            ? "asset:///images/emoji/activePageDot.png"
                            : "asset:///images/emoji/inactivePageDot.png"
                    }
                    ImageView {
                        preferredWidth: ui.du(1.2); preferredHeight: ui.du(1.2)
                        verticalAlignment: VerticalAlignment.Center
                        leftMargin: ui.du(0.2); rightMargin: ui.du(0.2)
                        imageSource: emojiPagePeople === 3
                            ? "asset:///images/emoji/activePageDot.png"
                            : "asset:///images/emoji/inactivePageDot.png"
                    }
                    ImageView {
                        preferredWidth: ui.du(1.2); preferredHeight: ui.du(1.2)
                        verticalAlignment: VerticalAlignment.Center
                        leftMargin: ui.du(0.2); rightMargin: ui.du(0.2)
                        imageSource: emojiPagePeople === 4
                            ? "asset:///images/emoji/activePageDot.png"
                            : "asset:///images/emoji/inactivePageDot.png"
                    }
                    ImageView {
                        preferredWidth: ui.du(1.2); preferredHeight: ui.du(1.2)
                        verticalAlignment: VerticalAlignment.Center
                        leftMargin: ui.du(0.2); rightMargin: ui.du(0.2)
                        imageSource: emojiPagePeople === 5
                            ? "asset:///images/emoji/activePageDot.png"
                            : "asset:///images/emoji/inactivePageDot.png"
                    }
                    ImageView {
                        preferredWidth: ui.du(1.2); preferredHeight: ui.du(1.2)
                        verticalAlignment: VerticalAlignment.Center
                        leftMargin: ui.du(0.2); rightMargin: ui.du(0.2)
                        imageSource: emojiPagePeople === 6
                            ? "asset:///images/emoji/activePageDot.png"
                            : "asset:///images/emoji/inactivePageDot.png"
                    }
                    ImageView {
                        preferredWidth: ui.du(1.2); preferredHeight: ui.du(1.2)
                        verticalAlignment: VerticalAlignment.Center
                        leftMargin: ui.du(0.2); rightMargin: ui.du(0.2)
                        imageSource: emojiPagePeople === 7
                            ? "asset:///images/emoji/activePageDot.png"
                            : "asset:///images/emoji/inactivePageDot.png"
                    }
                    ImageView {
                        preferredWidth: ui.du(1.2); preferredHeight: ui.du(1.2)
                        verticalAlignment: VerticalAlignment.Center
                        leftMargin: ui.du(0.2); rightMargin: ui.du(0.2)
                        imageSource: emojiPagePeople === 8
                            ? "asset:///images/emoji/activePageDot.png"
                            : "asset:///images/emoji/inactivePageDot.png"
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
                        if (emojiPageWidth > 0) {
                            var pg = Math.round(viewableArea.x / emojiPageWidth);
                            if (pg < 0) pg = 0;
                            if (pg >= 9) pg = 9 - 1;
                            emojiPagePeople = pg;
                        }
                    }
                    Container {
                        layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                        Container {
                            preferredWidth: emojiPageWidth > 0 ? emojiPageWidth : ui.du(52)
                            layout: GridLayout { columnCount: 8 }
                            horizontalAlignment: HorizontalAlignment.Fill
                            topPadding: ui.du(0.5); bottomPadding: ui.du(0.5); leftPadding: ui.du(1); rightPadding: ui.du(1)
                            EmojiButton { emojiFile: "emoji_1f604_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f603_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_263a_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f609_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f60d_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f618_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f61a_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f61c_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f61d_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f633_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f601_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f614_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f60c_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f612_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f61e_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f623_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                        }
                        Container {
                            preferredWidth: emojiPageWidth > 0 ? emojiPageWidth : ui.du(52)
                            layout: GridLayout { columnCount: 8 }
                            horizontalAlignment: HorizontalAlignment.Fill
                            topPadding: ui.du(0.5); bottomPadding: ui.du(0.5); leftPadding: ui.du(1); rightPadding: ui.du(1)
                            EmojiButton { emojiFile: "emoji_1f622_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f602_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f62d_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f62a_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f625_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f630_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f613_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f629_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f62b_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f628_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f631_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f620_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f621_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f624_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f616_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f606_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                        }
                        Container {
                            preferredWidth: emojiPageWidth > 0 ? emojiPageWidth : ui.du(52)
                            layout: GridLayout { columnCount: 8 }
                            horizontalAlignment: HorizontalAlignment.Fill
                            topPadding: ui.du(0.5); bottomPadding: ui.du(0.5); leftPadding: ui.du(1); rightPadding: ui.du(1)
                            EmojiButton { emojiFile: "emoji_1f60b_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f637_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f635_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f632_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f47f_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f60f_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f472_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f473_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f46e_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f477_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f482_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f476_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f466_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f467_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f468_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f469_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                        }
                        Container {
                            preferredWidth: emojiPageWidth > 0 ? emojiPageWidth : ui.du(52)
                            layout: GridLayout { columnCount: 8 }
                            horizontalAlignment: HorizontalAlignment.Fill
                            topPadding: ui.du(0.5); bottomPadding: ui.du(0.5); leftPadding: ui.du(1); rightPadding: ui.du(1)
                            EmojiButton { emojiFile: "emoji_1f474_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f475_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f471_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f47c_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f478_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f63a_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f638_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f63b_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f63d_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f63c_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f640_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f63f_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f639_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f480_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f47d_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f4a9_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                        }
                        Container {
                            preferredWidth: emojiPageWidth > 0 ? emojiPageWidth : ui.du(52)
                            layout: GridLayout { columnCount: 8 }
                            horizontalAlignment: HorizontalAlignment.Fill
                            topPadding: ui.du(0.5); bottomPadding: ui.du(0.5); leftPadding: ui.du(1); rightPadding: ui.du(1)
                            EmojiButton { emojiFile: "emoji_1f525_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_2728_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f31f_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f4a2_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f4a6_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f4a7_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f4a4_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f4a8_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f442_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f440_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f443_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f445_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f444_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f44d_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f44e_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f44c_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                        }
                        Container {
                            preferredWidth: emojiPageWidth > 0 ? emojiPageWidth : ui.du(52)
                            layout: GridLayout { columnCount: 8 }
                            horizontalAlignment: HorizontalAlignment.Fill
                            topPadding: ui.du(0.5); bottomPadding: ui.du(0.5); leftPadding: ui.du(1); rightPadding: ui.du(1)
                            EmojiButton { emojiFile: "emoji_1f44a_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_270a_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_270c_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f44b_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f64b_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f450_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f446_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f447_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f449_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f448_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f64c_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_261d_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f64f_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f44f_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f4aa_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f483_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                        }
                        Container {
                            preferredWidth: emojiPageWidth > 0 ? emojiPageWidth : ui.du(52)
                            layout: GridLayout { columnCount: 8 }
                            horizontalAlignment: HorizontalAlignment.Fill
                            topPadding: ui.du(0.5); bottomPadding: ui.du(0.5); leftPadding: ui.du(1); rightPadding: ui.du(1)
                            EmojiButton { emojiFile: "emoji_1f46b_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f48f_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f491_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f46f_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f64b_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f485_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f64d_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f3a9_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f451_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f452_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f45f_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f461_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f460_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f462_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f455_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f454_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                        }
                        Container {
                            preferredWidth: emojiPageWidth > 0 ? emojiPageWidth : ui.du(52)
                            layout: GridLayout { columnCount: 8 }
                            horizontalAlignment: HorizontalAlignment.Fill
                            topPadding: ui.du(0.5); bottomPadding: ui.du(0.5); leftPadding: ui.du(1); rightPadding: ui.du(1)
                            EmojiButton { emojiFile: "emoji_1f457_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f458_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f459_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f4bc_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f45c_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f380_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f302_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f484_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f49b_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f499_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f49c_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f49a_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_2764_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f494_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f497_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f493_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                        }
                        Container {
                            preferredWidth: emojiPageWidth > 0 ? emojiPageWidth : ui.du(52)
                            layout: GridLayout { columnCount: 8 }
                            horizontalAlignment: HorizontalAlignment.Fill
                            topPadding: ui.du(0.5); bottomPadding: ui.du(0.5); leftPadding: ui.du(1); rightPadding: ui.du(1)
                            EmojiButton { emojiFile: "emoji_1f496_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f49e_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f498_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f48c_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f48b_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f48d_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f48e_64.png"; emojiCategory: "people"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                        }
                    }
                }
            }

            // ── Nature ──
            Container {
                horizontalAlignment: HorizontalAlignment.Fill
                layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                visible: emojiCategoryIndex === 1
                layout: StackLayout { orientation: LayoutOrientation.TopToBottom }

                Container {
                    horizontalAlignment: HorizontalAlignment.Center
                    preferredHeight: ui.du(2.5)
                    layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                    ImageView {
                        preferredWidth: ui.du(1.2); preferredHeight: ui.du(1.2)
                        verticalAlignment: VerticalAlignment.Center
                        leftMargin: ui.du(0.2); rightMargin: ui.du(0.2)
                        imageSource: emojiPageNature === 0
                            ? "asset:///images/emoji/activePageDot.png"
                            : "asset:///images/emoji/inactivePageDot.png"
                    }
                    ImageView {
                        preferredWidth: ui.du(1.2); preferredHeight: ui.du(1.2)
                        verticalAlignment: VerticalAlignment.Center
                        leftMargin: ui.du(0.2); rightMargin: ui.du(0.2)
                        imageSource: emojiPageNature === 1
                            ? "asset:///images/emoji/activePageDot.png"
                            : "asset:///images/emoji/inactivePageDot.png"
                    }
                    ImageView {
                        preferredWidth: ui.du(1.2); preferredHeight: ui.du(1.2)
                        verticalAlignment: VerticalAlignment.Center
                        leftMargin: ui.du(0.2); rightMargin: ui.du(0.2)
                        imageSource: emojiPageNature === 2
                            ? "asset:///images/emoji/activePageDot.png"
                            : "asset:///images/emoji/inactivePageDot.png"
                    }
                    ImageView {
                        preferredWidth: ui.du(1.2); preferredHeight: ui.du(1.2)
                        verticalAlignment: VerticalAlignment.Center
                        leftMargin: ui.du(0.2); rightMargin: ui.du(0.2)
                        imageSource: emojiPageNature === 3
                            ? "asset:///images/emoji/activePageDot.png"
                            : "asset:///images/emoji/inactivePageDot.png"
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
                        if (emojiPageWidth > 0) {
                            var pg = Math.round(viewableArea.x / emojiPageWidth);
                            if (pg < 0) pg = 0;
                            if (pg >= 4) pg = 4 - 1;
                            emojiPageNature = pg;
                        }
                    }
                    Container {
                        layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                        Container {
                            preferredWidth: emojiPageWidth > 0 ? emojiPageWidth : ui.du(52)
                            layout: GridLayout { columnCount: 8 }
                            horizontalAlignment: HorizontalAlignment.Fill
                            topPadding: ui.du(0.5); bottomPadding: ui.du(0.5); leftPadding: ui.du(1); rightPadding: ui.du(1)
                            EmojiButton { emojiFile: "emoji_1f436_64.png"; emojiCategory: "nature"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f43a_64.png"; emojiCategory: "nature"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f431_64.png"; emojiCategory: "nature"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f42d_64.png"; emojiCategory: "nature"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f439_64.png"; emojiCategory: "nature"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f430_64.png"; emojiCategory: "nature"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f438_64.png"; emojiCategory: "nature"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f42f_64.png"; emojiCategory: "nature"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f428_64.png"; emojiCategory: "nature"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f43b_64.png"; emojiCategory: "nature"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f437_64.png"; emojiCategory: "nature"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f42e_64.png"; emojiCategory: "nature"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f417_64.png"; emojiCategory: "nature"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f435_64.png"; emojiCategory: "nature"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f412_64.png"; emojiCategory: "nature"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f434_64.png"; emojiCategory: "nature"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                        }
                        Container {
                            preferredWidth: emojiPageWidth > 0 ? emojiPageWidth : ui.du(52)
                            layout: GridLayout { columnCount: 8 }
                            horizontalAlignment: HorizontalAlignment.Fill
                            topPadding: ui.du(0.5); bottomPadding: ui.du(0.5); leftPadding: ui.du(1); rightPadding: ui.du(1)
                            EmojiButton { emojiFile: "emoji_1f411_64.png"; emojiCategory: "nature"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f418_64.png"; emojiCategory: "nature"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f427_64.png"; emojiCategory: "nature"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f426_64.png"; emojiCategory: "nature"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f425_64.png"; emojiCategory: "nature"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f414_64.png"; emojiCategory: "nature"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f40d_64.png"; emojiCategory: "nature"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f41b_64.png"; emojiCategory: "nature"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f419_64.png"; emojiCategory: "nature"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f41a_64.png"; emojiCategory: "nature"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f420_64.png"; emojiCategory: "nature"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f41f_64.png"; emojiCategory: "nature"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f42c_64.png"; emojiCategory: "nature"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f433_64.png"; emojiCategory: "nature"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f40e_64.png"; emojiCategory: "nature"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f421_64.png"; emojiCategory: "nature"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                        }
                        Container {
                            preferredWidth: emojiPageWidth > 0 ? emojiPageWidth : ui.du(52)
                            layout: GridLayout { columnCount: 8 }
                            horizontalAlignment: HorizontalAlignment.Fill
                            topPadding: ui.du(0.5); bottomPadding: ui.du(0.5); leftPadding: ui.du(1); rightPadding: ui.du(1)
                            EmojiButton { emojiFile: "emoji_1f42b_64.png"; emojiCategory: "nature"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f429_64.png"; emojiCategory: "nature"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f43e_64.png"; emojiCategory: "nature"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f490_64.png"; emojiCategory: "nature"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f338_64.png"; emojiCategory: "nature"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f337_64.png"; emojiCategory: "nature"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f340_64.png"; emojiCategory: "nature"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f339_64.png"; emojiCategory: "nature"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f33b_64.png"; emojiCategory: "nature"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f33a_64.png"; emojiCategory: "nature"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f341_64.png"; emojiCategory: "nature"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f343_64.png"; emojiCategory: "nature"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f342_64.png"; emojiCategory: "nature"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f33e_64.png"; emojiCategory: "nature"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f335_64.png"; emojiCategory: "nature"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f334_64.png"; emojiCategory: "nature"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                        }
                        Container {
                            preferredWidth: emojiPageWidth > 0 ? emojiPageWidth : ui.du(52)
                            layout: GridLayout { columnCount: 8 }
                            horizontalAlignment: HorizontalAlignment.Fill
                            topPadding: ui.du(0.5); bottomPadding: ui.du(0.5); leftPadding: ui.du(1); rightPadding: ui.du(1)
                            EmojiButton { emojiFile: "emoji_1f331_64.png"; emojiCategory: "nature"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f319_64.png"; emojiCategory: "nature"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_2b50_64.png"; emojiCategory: "nature"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_2600_64.png"; emojiCategory: "nature"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_2601_64.png"; emojiCategory: "nature"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_26a1_64.png"; emojiCategory: "nature"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_2614_64.png"; emojiCategory: "nature"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_26c4_64.png"; emojiCategory: "nature"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f300_64.png"; emojiCategory: "nature"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f308_64.png"; emojiCategory: "nature"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f30a_64.png"; emojiCategory: "nature"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                        }
                    }
                }
            }

            // ── Objects ──
            Container {
                horizontalAlignment: HorizontalAlignment.Fill
                layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                visible: emojiCategoryIndex === 2
                layout: StackLayout { orientation: LayoutOrientation.TopToBottom }

                Container {
                    horizontalAlignment: HorizontalAlignment.Center
                    preferredHeight: ui.du(2.5)
                    layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                    ImageView {
                        preferredWidth: ui.du(1.2); preferredHeight: ui.du(1.2)
                        verticalAlignment: VerticalAlignment.Center
                        leftMargin: ui.du(0.2); rightMargin: ui.du(0.2)
                        imageSource: emojiPageObjects === 0
                            ? "asset:///images/emoji/activePageDot.png"
                            : "asset:///images/emoji/inactivePageDot.png"
                    }
                    ImageView {
                        preferredWidth: ui.du(1.2); preferredHeight: ui.du(1.2)
                        verticalAlignment: VerticalAlignment.Center
                        leftMargin: ui.du(0.2); rightMargin: ui.du(0.2)
                        imageSource: emojiPageObjects === 1
                            ? "asset:///images/emoji/activePageDot.png"
                            : "asset:///images/emoji/inactivePageDot.png"
                    }
                    ImageView {
                        preferredWidth: ui.du(1.2); preferredHeight: ui.du(1.2)
                        verticalAlignment: VerticalAlignment.Center
                        leftMargin: ui.du(0.2); rightMargin: ui.du(0.2)
                        imageSource: emojiPageObjects === 2
                            ? "asset:///images/emoji/activePageDot.png"
                            : "asset:///images/emoji/inactivePageDot.png"
                    }
                    ImageView {
                        preferredWidth: ui.du(1.2); preferredHeight: ui.du(1.2)
                        verticalAlignment: VerticalAlignment.Center
                        leftMargin: ui.du(0.2); rightMargin: ui.du(0.2)
                        imageSource: emojiPageObjects === 3
                            ? "asset:///images/emoji/activePageDot.png"
                            : "asset:///images/emoji/inactivePageDot.png"
                    }
                    ImageView {
                        preferredWidth: ui.du(1.2); preferredHeight: ui.du(1.2)
                        verticalAlignment: VerticalAlignment.Center
                        leftMargin: ui.du(0.2); rightMargin: ui.du(0.2)
                        imageSource: emojiPageObjects === 4
                            ? "asset:///images/emoji/activePageDot.png"
                            : "asset:///images/emoji/inactivePageDot.png"
                    }
                    ImageView {
                        preferredWidth: ui.du(1.2); preferredHeight: ui.du(1.2)
                        verticalAlignment: VerticalAlignment.Center
                        leftMargin: ui.du(0.2); rightMargin: ui.du(0.2)
                        imageSource: emojiPageObjects === 5
                            ? "asset:///images/emoji/activePageDot.png"
                            : "asset:///images/emoji/inactivePageDot.png"
                    }
                    ImageView {
                        preferredWidth: ui.du(1.2); preferredHeight: ui.du(1.2)
                        verticalAlignment: VerticalAlignment.Center
                        leftMargin: ui.du(0.2); rightMargin: ui.du(0.2)
                        imageSource: emojiPageObjects === 6
                            ? "asset:///images/emoji/activePageDot.png"
                            : "asset:///images/emoji/inactivePageDot.png"
                    }
                    ImageView {
                        preferredWidth: ui.du(1.2); preferredHeight: ui.du(1.2)
                        verticalAlignment: VerticalAlignment.Center
                        leftMargin: ui.du(0.2); rightMargin: ui.du(0.2)
                        imageSource: emojiPageObjects === 7
                            ? "asset:///images/emoji/activePageDot.png"
                            : "asset:///images/emoji/inactivePageDot.png"
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
                        if (emojiPageWidth > 0) {
                            var pg = Math.round(viewableArea.x / emojiPageWidth);
                            if (pg < 0) pg = 0;
                            if (pg >= 8) pg = 8 - 1;
                            emojiPageObjects = pg;
                        }
                    }
                    Container {
                        layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                        Container {
                            preferredWidth: emojiPageWidth > 0 ? emojiPageWidth : ui.du(52)
                            layout: GridLayout { columnCount: 8 }
                            horizontalAlignment: HorizontalAlignment.Fill
                            topPadding: ui.du(0.5); bottomPadding: ui.du(0.5); leftPadding: ui.du(1); rightPadding: ui.du(1)
                            EmojiButton { emojiFile: "emoji_1f38d_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f49d_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f38e_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f392_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f393_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f38f_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f390_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f383_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f47b_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f385_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f384_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f381_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f389_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f388_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f38c_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f3a5_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                        }
                        Container {
                            preferredWidth: emojiPageWidth > 0 ? emojiPageWidth : ui.du(52)
                            layout: GridLayout { columnCount: 8 }
                            horizontalAlignment: HorizontalAlignment.Fill
                            topPadding: ui.du(0.5); bottomPadding: ui.du(0.5); leftPadding: ui.du(1); rightPadding: ui.du(1)
                            EmojiButton { emojiFile: "emoji_1f4f7_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f4fc_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f4bf_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f4c0_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f4bd_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f4be_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f4bb_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f4f1_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_260e_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f4de_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f4e0_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f4e1_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f4fa_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f4fb_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f508_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f514_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                        }
                        Container {
                            preferredWidth: emojiPageWidth > 0 ? emojiPageWidth : ui.du(52)
                            layout: GridLayout { columnCount: 8 }
                            horizontalAlignment: HorizontalAlignment.Fill
                            topPadding: ui.du(0.5); bottomPadding: ui.du(0.5); leftPadding: ui.du(1); rightPadding: ui.du(1)
                            EmojiButton { emojiFile: "emoji_1f4e2_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f4e3_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f513_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f512_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f50f_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f510_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f511_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f50e_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f4a1_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f50d_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f6c0_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f6bd_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f528_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f6ac_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f4a3_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f52b_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                        }
                        Container {
                            preferredWidth: emojiPageWidth > 0 ? emojiPageWidth : ui.du(52)
                            layout: GridLayout { columnCount: 8 }
                            horizontalAlignment: HorizontalAlignment.Fill
                            topPadding: ui.du(0.5); bottomPadding: ui.du(0.5); leftPadding: ui.du(1); rightPadding: ui.du(1)
                            EmojiButton { emojiFile: "emoji_1f48a_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f489_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f4b0_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f4b4_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f4b5_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f4f2_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_2709_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f4e9_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f4e8_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f4eb_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f4ea_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f4ec_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f4ed_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f4ee_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f4dd_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_2702_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                        }
                        Container {
                            preferredWidth: emojiPageWidth > 0 ? emojiPageWidth : ui.du(52)
                            layout: GridLayout { columnCount: 8 }
                            horizontalAlignment: HorizontalAlignment.Fill
                            topPadding: ui.du(0.5); bottomPadding: ui.du(0.5); leftPadding: ui.du(1); rightPadding: ui.du(1)
                            EmojiButton { emojiFile: "emoji_1f4d6_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f3a8_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f3ac_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f3a4_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f3a7_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f3bc_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f3b5_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f3b6_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f3ba_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f3b7_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f3b8_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f47e_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f004_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f3af_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f3c8_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f3c0_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                        }
                        Container {
                            preferredWidth: emojiPageWidth > 0 ? emojiPageWidth : ui.du(52)
                            layout: GridLayout { columnCount: 8 }
                            horizontalAlignment: HorizontalAlignment.Fill
                            topPadding: ui.du(0.5); bottomPadding: ui.du(0.5); leftPadding: ui.du(1); rightPadding: ui.du(1)
                            EmojiButton { emojiFile: "emoji_26bd_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_26be_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f3be_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f3b1_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_26f3_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f3c1_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f3c6_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f3bf_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_2615_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f375_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f376_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f37a_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f37b_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f378_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f374_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f354_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                        }
                        Container {
                            preferredWidth: emojiPageWidth > 0 ? emojiPageWidth : ui.du(52)
                            layout: GridLayout { columnCount: 8 }
                            horizontalAlignment: HorizontalAlignment.Fill
                            topPadding: ui.du(0.5); bottomPadding: ui.du(0.5); leftPadding: ui.du(1); rightPadding: ui.du(1)
                            EmojiButton { emojiFile: "emoji_1f35f_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f35d_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f35b_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f371_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f363_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f359_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f358_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f35a_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f35c_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f372_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f362_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f361_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f373_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f35e_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f366_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f367_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                        }
                        Container {
                            preferredWidth: emojiPageWidth > 0 ? emojiPageWidth : ui.du(52)
                            layout: GridLayout { columnCount: 8 }
                            horizontalAlignment: HorizontalAlignment.Fill
                            topPadding: ui.du(0.5); bottomPadding: ui.du(0.5); leftPadding: ui.du(1); rightPadding: ui.du(1)
                            EmojiButton { emojiFile: "emoji_1f370_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f34e_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f34a_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f349_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f353_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f346_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f345_64.png"; emojiCategory: "objects"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                        }
                    }
                }
            }

            // ── Places ──
            Container {
                horizontalAlignment: HorizontalAlignment.Fill
                layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                visible: emojiCategoryIndex === 3
                layout: StackLayout { orientation: LayoutOrientation.TopToBottom }

                Container {
                    horizontalAlignment: HorizontalAlignment.Center
                    preferredHeight: ui.du(2.5)
                    layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                    ImageView {
                        preferredWidth: ui.du(1.2); preferredHeight: ui.du(1.2)
                        verticalAlignment: VerticalAlignment.Center
                        leftMargin: ui.du(0.2); rightMargin: ui.du(0.2)
                        imageSource: emojiPagePlaces === 0
                            ? "asset:///images/emoji/activePageDot.png"
                            : "asset:///images/emoji/inactivePageDot.png"
                    }
                    ImageView {
                        preferredWidth: ui.du(1.2); preferredHeight: ui.du(1.2)
                        verticalAlignment: VerticalAlignment.Center
                        leftMargin: ui.du(0.2); rightMargin: ui.du(0.2)
                        imageSource: emojiPagePlaces === 1
                            ? "asset:///images/emoji/activePageDot.png"
                            : "asset:///images/emoji/inactivePageDot.png"
                    }
                    ImageView {
                        preferredWidth: ui.du(1.2); preferredHeight: ui.du(1.2)
                        verticalAlignment: VerticalAlignment.Center
                        leftMargin: ui.du(0.2); rightMargin: ui.du(0.2)
                        imageSource: emojiPagePlaces === 2
                            ? "asset:///images/emoji/activePageDot.png"
                            : "asset:///images/emoji/inactivePageDot.png"
                    }
                    ImageView {
                        preferredWidth: ui.du(1.2); preferredHeight: ui.du(1.2)
                        verticalAlignment: VerticalAlignment.Center
                        leftMargin: ui.du(0.2); rightMargin: ui.du(0.2)
                        imageSource: emojiPagePlaces === 3
                            ? "asset:///images/emoji/activePageDot.png"
                            : "asset:///images/emoji/inactivePageDot.png"
                    }
                    ImageView {
                        preferredWidth: ui.du(1.2); preferredHeight: ui.du(1.2)
                        verticalAlignment: VerticalAlignment.Center
                        leftMargin: ui.du(0.2); rightMargin: ui.du(0.2)
                        imageSource: emojiPagePlaces === 4
                            ? "asset:///images/emoji/activePageDot.png"
                            : "asset:///images/emoji/inactivePageDot.png"
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
                        if (emojiPageWidth > 0) {
                            var pg = Math.round(viewableArea.x / emojiPageWidth);
                            if (pg < 0) pg = 0;
                            if (pg >= 5) pg = 5 - 1;
                            emojiPagePlaces = pg;
                        }
                    }
                    Container {
                        layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                        Container {
                            preferredWidth: emojiPageWidth > 0 ? emojiPageWidth : ui.du(52)
                            layout: GridLayout { columnCount: 8 }
                            horizontalAlignment: HorizontalAlignment.Fill
                            topPadding: ui.du(0.5); bottomPadding: ui.du(0.5); leftPadding: ui.du(1); rightPadding: ui.du(1)
                            EmojiButton { emojiFile: "emoji_1f3e0_64.png"; emojiCategory: "places"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f3e1_64.png"; emojiCategory: "places"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f3eb_64.png"; emojiCategory: "places"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f3e2_64.png"; emojiCategory: "places"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f3e3_64.png"; emojiCategory: "places"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f3e5_64.png"; emojiCategory: "places"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f3e6_64.png"; emojiCategory: "places"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f3ea_64.png"; emojiCategory: "places"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f3e9_64.png"; emojiCategory: "places"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f3e8_64.png"; emojiCategory: "places"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f492_64.png"; emojiCategory: "places"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_26ea_64.png"; emojiCategory: "places"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f3ec_64.png"; emojiCategory: "places"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f307_64.png"; emojiCategory: "places"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f306_64.png"; emojiCategory: "places"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f3ef_64.png"; emojiCategory: "places"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                        }
                        Container {
                            preferredWidth: emojiPageWidth > 0 ? emojiPageWidth : ui.du(52)
                            layout: GridLayout { columnCount: 8 }
                            horizontalAlignment: HorizontalAlignment.Fill
                            topPadding: ui.du(0.5); bottomPadding: ui.du(0.5); leftPadding: ui.du(1); rightPadding: ui.du(1)
                            EmojiButton { emojiFile: "emoji_1f3f0_64.png"; emojiCategory: "places"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_26fa_64.png"; emojiCategory: "places"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f3ed_64.png"; emojiCategory: "places"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f5fc_64.png"; emojiCategory: "places"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f5fb_64.png"; emojiCategory: "places"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f304_64.png"; emojiCategory: "places"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f305_64.png"; emojiCategory: "places"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f303_64.png"; emojiCategory: "places"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f5fd_64.png"; emojiCategory: "places"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f3a1_64.png"; emojiCategory: "places"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_26f2_64.png"; emojiCategory: "places"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f3a2_64.png"; emojiCategory: "places"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f6a2_64.png"; emojiCategory: "places"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_26f5_64.png"; emojiCategory: "places"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f6a4_64.png"; emojiCategory: "places"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f680_64.png"; emojiCategory: "places"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                        }
                        Container {
                            preferredWidth: emojiPageWidth > 0 ? emojiPageWidth : ui.du(52)
                            layout: GridLayout { columnCount: 8 }
                            horizontalAlignment: HorizontalAlignment.Fill
                            topPadding: ui.du(0.5); bottomPadding: ui.du(0.5); leftPadding: ui.du(1); rightPadding: ui.du(1)
                            EmojiButton { emojiFile: "emoji_2708_64.png"; emojiCategory: "places"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f4ba_64.png"; emojiCategory: "places"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f689_64.png"; emojiCategory: "places"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f684_64.png"; emojiCategory: "places"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f685_64.png"; emojiCategory: "places"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f683_64.png"; emojiCategory: "places"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f68c_64.png"; emojiCategory: "places"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f699_64.png"; emojiCategory: "places"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f697_64.png"; emojiCategory: "places"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f695_64.png"; emojiCategory: "places"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f69a_64.png"; emojiCategory: "places"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f693_64.png"; emojiCategory: "places"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f692_64.png"; emojiCategory: "places"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f691_64.png"; emojiCategory: "places"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f6b2_64.png"; emojiCategory: "places"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f488_64.png"; emojiCategory: "places"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                        }
                        Container {
                            preferredWidth: emojiPageWidth > 0 ? emojiPageWidth : ui.du(52)
                            layout: GridLayout { columnCount: 8 }
                            horizontalAlignment: HorizontalAlignment.Fill
                            topPadding: ui.du(0.5); bottomPadding: ui.du(0.5); leftPadding: ui.du(1); rightPadding: ui.du(1)
                            EmojiButton { emojiFile: "emoji_1f68f_64.png"; emojiCategory: "places"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f3ab_64.png"; emojiCategory: "places"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f6a5_64.png"; emojiCategory: "places"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_26a0_64.png"; emojiCategory: "places"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f6a7_64.png"; emojiCategory: "places"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f530_64.png"; emojiCategory: "places"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_26fd_64.png"; emojiCategory: "places"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_2668_64.png"; emojiCategory: "places"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f3ad_64.png"; emojiCategory: "places"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f1ef_1f1f5_64.png"; emojiCategory: "places"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f1f0_1f1f7_64.png"; emojiCategory: "places"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f1e9_1f1ea_64.png"; emojiCategory: "places"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f1e8_1f1f3_64.png"; emojiCategory: "places"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f1fa_1f1f8_64.png"; emojiCategory: "places"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f1eb_1f1f7_64.png"; emojiCategory: "places"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f1ea_1f1f8_64.png"; emojiCategory: "places"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                        }
                        Container {
                            preferredWidth: emojiPageWidth > 0 ? emojiPageWidth : ui.du(52)
                            layout: GridLayout { columnCount: 8 }
                            horizontalAlignment: HorizontalAlignment.Fill
                            topPadding: ui.du(0.5); bottomPadding: ui.du(0.5); leftPadding: ui.du(1); rightPadding: ui.du(1)
                            EmojiButton { emojiFile: "emoji_1f1ee_1f1f9_64.png"; emojiCategory: "places"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f1f7_1f1fa_64.png"; emojiCategory: "places"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f1ec_1f1e7_64.png"; emojiCategory: "places"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                        }
                    }
                }
            }

            // ── Symbols ──
            Container {
                horizontalAlignment: HorizontalAlignment.Fill
                layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                visible: emojiCategoryIndex === 4
                layout: StackLayout { orientation: LayoutOrientation.TopToBottom }

                Container {
                    horizontalAlignment: HorizontalAlignment.Center
                    preferredHeight: ui.du(2.5)
                    layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                    ImageView {
                        preferredWidth: ui.du(1.2); preferredHeight: ui.du(1.2)
                        verticalAlignment: VerticalAlignment.Center
                        leftMargin: ui.du(0.2); rightMargin: ui.du(0.2)
                        imageSource: emojiPageSymbols === 0
                            ? "asset:///images/emoji/activePageDot.png"
                            : "asset:///images/emoji/inactivePageDot.png"
                    }
                    ImageView {
                        preferredWidth: ui.du(1.2); preferredHeight: ui.du(1.2)
                        verticalAlignment: VerticalAlignment.Center
                        leftMargin: ui.du(0.2); rightMargin: ui.du(0.2)
                        imageSource: emojiPageSymbols === 1
                            ? "asset:///images/emoji/activePageDot.png"
                            : "asset:///images/emoji/inactivePageDot.png"
                    }
                    ImageView {
                        preferredWidth: ui.du(1.2); preferredHeight: ui.du(1.2)
                        verticalAlignment: VerticalAlignment.Center
                        leftMargin: ui.du(0.2); rightMargin: ui.du(0.2)
                        imageSource: emojiPageSymbols === 2
                            ? "asset:///images/emoji/activePageDot.png"
                            : "asset:///images/emoji/inactivePageDot.png"
                    }
                    ImageView {
                        preferredWidth: ui.du(1.2); preferredHeight: ui.du(1.2)
                        verticalAlignment: VerticalAlignment.Center
                        leftMargin: ui.du(0.2); rightMargin: ui.du(0.2)
                        imageSource: emojiPageSymbols === 3
                            ? "asset:///images/emoji/activePageDot.png"
                            : "asset:///images/emoji/inactivePageDot.png"
                    }
                    ImageView {
                        preferredWidth: ui.du(1.2); preferredHeight: ui.du(1.2)
                        verticalAlignment: VerticalAlignment.Center
                        leftMargin: ui.du(0.2); rightMargin: ui.du(0.2)
                        imageSource: emojiPageSymbols === 4
                            ? "asset:///images/emoji/activePageDot.png"
                            : "asset:///images/emoji/inactivePageDot.png"
                    }
                    ImageView {
                        preferredWidth: ui.du(1.2); preferredHeight: ui.du(1.2)
                        verticalAlignment: VerticalAlignment.Center
                        leftMargin: ui.du(0.2); rightMargin: ui.du(0.2)
                        imageSource: emojiPageSymbols === 5
                            ? "asset:///images/emoji/activePageDot.png"
                            : "asset:///images/emoji/inactivePageDot.png"
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
                        if (emojiPageWidth > 0) {
                            var pg = Math.round(viewableArea.x / emojiPageWidth);
                            if (pg < 0) pg = 0;
                            if (pg >= 6) pg = 6 - 1;
                            emojiPageSymbols = pg;
                        }
                    }
                    Container {
                        layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                        Container {
                            preferredWidth: emojiPageWidth > 0 ? emojiPageWidth : ui.du(52)
                            layout: GridLayout { columnCount: 8 }
                            horizontalAlignment: HorizontalAlignment.Fill
                            topPadding: ui.du(0.5); bottomPadding: ui.du(0.5); leftPadding: ui.du(1); rightPadding: ui.du(1)
                            EmojiButton { emojiFile: "emoji_31_20e3_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_32_20e3_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_33_20e3_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_34_20e3_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_35_20e3_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_36_20e3_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_37_20e3_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_38_20e3_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_39_20e3_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_30_20e3_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_23_20e3_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_2b06_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_2b07_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_2b05_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_27a1_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_2197_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                        }
                        Container {
                            preferredWidth: emojiPageWidth > 0 ? emojiPageWidth : ui.du(52)
                            layout: GridLayout { columnCount: 8 }
                            horizontalAlignment: HorizontalAlignment.Fill
                            topPadding: ui.du(0.5); bottomPadding: ui.du(0.5); leftPadding: ui.du(1); rightPadding: ui.du(1)
                            EmojiButton { emojiFile: "emoji_2196_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_2198_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_2199_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_2935_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_2934_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f199_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f192_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f4f6_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f3a6_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f201_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f22f_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f233_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f235_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f250_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f239_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f23a_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                        }
                        Container {
                            preferredWidth: emojiPageWidth > 0 ? emojiPageWidth : ui.du(52)
                            layout: GridLayout { columnCount: 8 }
                            horizontalAlignment: HorizontalAlignment.Fill
                            topPadding: ui.du(0.5); bottomPadding: ui.du(0.5); leftPadding: ui.du(1); rightPadding: ui.du(1)
                            EmojiButton { emojiFile: "emoji_1f236_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f21a_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f6bb_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f6b9_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f6ba_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f6bc_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f6be_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_267f_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f6ad_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f237_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f238_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f202_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_3299_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_3297_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f51e_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_26d4_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                        }
                        Container {
                            preferredWidth: emojiPageWidth > 0 ? emojiPageWidth : ui.du(52)
                            layout: GridLayout { columnCount: 8 }
                            horizontalAlignment: HorizontalAlignment.Fill
                            topPadding: ui.du(0.5); bottomPadding: ui.du(0.5); leftPadding: ui.du(1); rightPadding: ui.du(1)
                            EmojiButton { emojiFile: "emoji_2733_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_274e_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_2734_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f49f_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f19a_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f4f3_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f4f4_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f4a0_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_27bf_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f3e7_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f4b2_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_303d_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_274c_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_2b55_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_2757_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_2753_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                        }
                        Container {
                            preferredWidth: emojiPageWidth > 0 ? emojiPageWidth : ui.du(52)
                            layout: GridLayout { columnCount: 8 }
                            horizontalAlignment: HorizontalAlignment.Fill
                            topPadding: ui.du(0.5); bottomPadding: ui.du(0.5); leftPadding: ui.du(1); rightPadding: ui.du(1)
                            EmojiButton { emojiFile: "emoji_2755_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_2754_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_2716_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_2660_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_2665_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_2663_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_2666_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f531_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f532_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f533_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_25fc_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_25fb_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_25fe_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_25fd_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_25aa_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_25ab_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                        }
                        Container {
                            preferredWidth: emojiPageWidth > 0 ? emojiPageWidth : ui.du(52)
                            layout: GridLayout { columnCount: 8 }
                            horizontalAlignment: HorizontalAlignment.Fill
                            topPadding: ui.du(0.5); bottomPadding: ui.du(0.5); leftPadding: ui.du(1); rightPadding: ui.du(1)
                            EmojiButton { emojiFile: "emoji_2b1c_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_2b1b_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_26ab_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_26aa_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f534_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f535_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f536_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f537_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f538_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                            EmojiButton { emojiFile: "emoji_1f539_64.png"; emojiCategory: "symbols"; onEmojiTapped: { inputField.text = inputField.text + chatViewPage.emojiFileToChar(file) } }
                        }
                    }
                }
            }


            // ── Bottom: Category tabs + Backspace ────────────────
            Container {
                horizontalAlignment: HorizontalAlignment.Fill
                background: Color.create("#E0E0E0")
                preferredHeight: ui.du(5.5)
                layout: StackLayout { orientation: LayoutOrientation.LeftToRight }

                Container {
                    layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                    layout: DockLayout {}
                    background: emojiCategoryIndex === 0 ? Color.create("#FFFFFF") : Color.Transparent
                    ImageButton {
                        horizontalAlignment: HorizontalAlignment.Center
                        verticalAlignment: VerticalAlignment.Center
                        preferredWidth: ui.du(5); preferredHeight: ui.du(5)
                        defaultImageSource: "asset:///images/emoji/emoji_category_people.png"
                        pressedImageSource: "asset:///images/emoji/emoji_category_people.png"
                        onClicked: { emojiCategoryIndex = 0 }
                    }
                }
                Container {
                    layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                    layout: DockLayout {}
                    background: emojiCategoryIndex === 1 ? Color.create("#FFFFFF") : Color.Transparent
                    ImageButton {
                        horizontalAlignment: HorizontalAlignment.Center
                        verticalAlignment: VerticalAlignment.Center
                        preferredWidth: ui.du(5); preferredHeight: ui.du(5)
                        defaultImageSource: "asset:///images/emoji/emoji_category_nature.png"
                        pressedImageSource: "asset:///images/emoji/emoji_category_nature.png"
                        onClicked: { emojiCategoryIndex = 1 }
                    }
                }
                Container {
                    layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                    layout: DockLayout {}
                    background: emojiCategoryIndex === 2 ? Color.create("#FFFFFF") : Color.Transparent
                    ImageButton {
                        horizontalAlignment: HorizontalAlignment.Center
                        verticalAlignment: VerticalAlignment.Center
                        preferredWidth: ui.du(5); preferredHeight: ui.du(5)
                        defaultImageSource: "asset:///images/emoji/emoji_category_objects.png"
                        pressedImageSource: "asset:///images/emoji/emoji_category_objects.png"
                        onClicked: { emojiCategoryIndex = 2 }
                    }
                }
                Container {
                    layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                    layout: DockLayout {}
                    background: emojiCategoryIndex === 3 ? Color.create("#FFFFFF") : Color.Transparent
                    ImageButton {
                        horizontalAlignment: HorizontalAlignment.Center
                        verticalAlignment: VerticalAlignment.Center
                        preferredWidth: ui.du(5); preferredHeight: ui.du(5)
                        defaultImageSource: "asset:///images/emoji/emoji_category_places.png"
                        pressedImageSource: "asset:///images/emoji/emoji_category_places.png"
                        onClicked: { emojiCategoryIndex = 3 }
                    }
                }
                Container {
                    layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                    layout: DockLayout {}
                    background: emojiCategoryIndex === 4 ? Color.create("#FFFFFF") : Color.Transparent
                    ImageButton {
                        horizontalAlignment: HorizontalAlignment.Center
                        verticalAlignment: VerticalAlignment.Center
                        preferredWidth: ui.du(5); preferredHeight: ui.du(5)
                        defaultImageSource: "asset:///images/emoji/emoji_category_symbols.png"
                        pressedImageSource: "asset:///images/emoji/emoji_category_symbols.png"
                        onClicked: { emojiCategoryIndex = 4 }
                    }
                }
            }

        } // end emojiPanel

        // ── Input bar ────────────────────────────────────────────
        Container {
            horizontalAlignment: HorizontalAlignment.Fill
            background: Color.create("#F8F8F8")
            topPadding:    ui.du(1.2)
            bottomPadding: ui.du(1.2)
            leftPadding:   ui.du(1.2)
            rightPadding:  ui.du(1.2)
            layout: StackLayout { orientation: LayoutOrientation.LeftToRight }

            ImageButton {
                verticalAlignment: VerticalAlignment.Center
                preferredWidth: ui.du(7); preferredHeight: ui.du(7)
                rightMargin: ui.du(1)
                defaultImageSource: "asset:///images/ic_attach.png"
                pressedImageSource: "asset:///images/ic_attach.png"
                onClicked: { /* TODO */ }
            }

            Container {
                layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                verticalAlignment: VerticalAlignment.Center
                TextField {
                    id: inputField
                    hintText: "Enter a message"
                    inputMode: TextFieldInputMode.Chat
                    minHeight: ui.du(6)
                    input {
                        flags: TextInputFlag.SpellCheck | TextInputFlag.WordSubstitution
                        submitKey: SubmitKey.Send
                        onSubmitted: { doSend() }
                    }
                    onTextChanging: {
                        sendAction.enabled = (inputField.text.trim().length > 0)
                    }
                }
            }

            ImageButton {
                id: emoticonBtn
                verticalAlignment: VerticalAlignment.Center
                preferredWidth: ui.du(7); preferredHeight: ui.du(7)
                leftMargin: ui.du(1)
                defaultImageSource: emojiPanelOpen
                    ? "asset:///images/emoji/darkkeyboard.png"
                    : "asset:///images/ic_emoticon_enabled.png"
                pressedImageSource: emojiPanelOpen
                    ? "asset:///images/emoji/darkkeyboard.png"
                    : "asset:///images/ic_emoticon_enabled.png"
                onClicked: {
                    emojiPanelOpen = !emojiPanelOpen;
                    emojiPanel.visible = emojiPanelOpen;
                }
            }
        }

    } // end content

    actions: [
        ActionItem {
            id: sendAction
            title: "Send"
            imageSource: "asset:///images/ConversationPaneSend.png"
            ActionBar.placement: ActionBarPlacement.OnBar
            enabled: false
            onTriggered: { doSend() }
        }
    ]

    function doSend() {
        var txt = inputField.text.trim();
        if (txt.length === 0) return;
        chatViewPage.pendingMsg = txt;
        sendAction.enabled = false;
        inputField.text = "";
        zService.sendMessage(chatViewPage.threadId, txt, chatViewPage.isGroup);
    }

    // ─── isMine normalizer ───────────────────────────────────────
    function normMine(v) {
        if (v === true  || v === 1)  return true;
        if (v === false || v === 0)  return false;
        if (typeof v === "string")   return (v === "true" || v === "1");
        return false;
    }

    // ─── Xây lại bubblePos + grouped + latestTs cho toàn bộ model
    function rebuildGroups() {
        var size = msgModel.size();
        if (size === 0) return;

        for (var i = 0; i < size; i++) {
            var cur  = msgModel.value(i);
            var prev = (i > 0)          ? msgModel.value(i - 1) : null;
            var next = (i < size - 1)   ? msgModel.value(i + 1) : null;

            var curMine  = chatViewPage.normMine(cur.isMine);
            var prevMine = prev ? chatViewPage.normMine(prev.isMine) : !curMine;
            var nextMine = next ? chatViewPage.normMine(next.isMine) : !curMine;

            var samePrev = (prev !== null) && (prevMine === curMine);
            var sameNext = (next !== null) && (nextMine === curMine);

            var pos;
            if      ( samePrev &&  sameNext) pos = "middle";
            else if ( samePrev && !sameNext) pos = "bottom";
            else if (!samePrev &&  sameNext) pos = "top";
            else                             pos = "full";

            cur.bubblePos = pos;
            cur.grouped   = samePrev;
            cur.selfName  = chatViewPage.selfName;
            // Đảm bảo isMine không bị mất sau replace — normalize về bool
            cur.isMine    = curMine;

            // Timestamp hiển thị ở tin CUỐI nhóm — cập nhật ngược lên các tin trước
            if (!sameNext) {
                cur.latestTs = cur.ts;
                var k = i - 1;
                while (k >= 0) {
                    var prev2 = msgModel.value(k);
                    if (chatViewPage.normMine(prev2.isMine) !== curMine) break;
                    prev2.latestTs = cur.ts;
                    msgModel.replace(k, prev2);
                    k--;
                }
            }
            msgModel.replace(i, cur);
        }
    }

    // ─── Helper: lấy set msgId hiện có trong model (chỉ id thật, bỏ local_)
    function buildExistingIds() {
        var ids = {};
        for (var i = 0; i < msgModel.size(); i++) {
            var mid = msgModel.value(i).msgId;
            if (mid && mid.indexOf("local_") !== 0)
                ids[mid] = true;
        }
        return ids;
    }

    // ─── Helper: xóa local placeholder có content khớp (tìm từ cuối lên)
    function removeLocalPlaceholder(content) {
        for (var k = msgModel.size() - 1; k >= 0; k--) {
            var item = msgModel.value(k);
            if (item.msgId && item.msgId.indexOf("local_") === 0
                    && item.content === content) {
                msgModel.removeAt(k);
                return;
            }
        }
    }

    attachedObjects: [
        Sheet {
            id: callSheet
            peekEnabled: false
            Page {
                titleBar: TitleBar {
                    title: "Voice Call"
                    dismissAction: ActionItem { title: "End"; onTriggered: { callSheet.close() } }
                }
                content: Container {
                    layout: StackLayout { orientation: LayoutOrientation.TopToBottom }
                    horizontalAlignment: HorizontalAlignment.Fill
                    verticalAlignment:   VerticalAlignment.Fill
                    background: Color.create("#1a1a1a")
                    Container {
                        layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                        layout: DockLayout {}
                        horizontalAlignment: HorizontalAlignment.Fill
                        Container {
                            horizontalAlignment: HorizontalAlignment.Center
                            verticalAlignment:   VerticalAlignment.Center
                            layout: StackLayout { orientation: LayoutOrientation.TopToBottom }
                            ImageView {
                                horizontalAlignment: HorizontalAlignment.Center
                                preferredWidth: ui.du(22); preferredHeight: ui.du(22)
                                scalingMethod: ScalingMethod.AspectFill
                                imageSource: chatViewPage.avatarUrl.length > 0
                                             ? chatViewPage.avatarUrl : "asset:///images/ic_contact.png"
                            }
                            Label {
                                text: chatViewPage.threadName
                                horizontalAlignment: HorizontalAlignment.Center
                                textStyle { color: Color.White; base: SystemDefaults.TextStyles.TitleText; fontWeight: FontWeight.Bold }
                                topMargin: ui.du(2)
                            }
                            Label {
                                text: "Calling..."
                                horizontalAlignment: HorizontalAlignment.Center
                                textStyle { color: Color.create("#aaaaaa"); fontSize: FontSize.Medium }
                                topMargin: ui.du(1)
                            }
                        }
                    }
                    Container {
                        horizontalAlignment: HorizontalAlignment.Center
                        bottomPadding: ui.du(5); topPadding: ui.du(3)
                        ImageButton {
                            horizontalAlignment: HorizontalAlignment.Center
                            preferredWidth: ui.du(14); preferredHeight: ui.du(14)
                            defaultImageSource: "asset:///images/ic_voice_call.png"
                            pressedImageSource: "asset:///images/ic_voice_call.png"
                            onClicked: { callSheet.close() }
                        }
                        Label { text: "End"; horizontalAlignment: HorizontalAlignment.Center; textStyle { color: Color.White; fontSize: FontSize.Small } }
                    }
                }
            }
        },

        Sheet {
            id: videoCallSheet
            peekEnabled: false
            Page {
                titleBar: TitleBar {
                    title: "Video Call"
                    dismissAction: ActionItem { title: "End"; onTriggered: { videoCallSheet.close() } }
                }
                content: Container {
                    layout: StackLayout { orientation: LayoutOrientation.TopToBottom }
                    horizontalAlignment: HorizontalAlignment.Fill
                    verticalAlignment:   VerticalAlignment.Fill
                    background: Color.create("#0d0d0d")
                    Container {
                        layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                        layout: DockLayout {}
                        horizontalAlignment: HorizontalAlignment.Fill
                        background: Color.create("#111111")
                        Container {
                            horizontalAlignment: HorizontalAlignment.Center
                            verticalAlignment:   VerticalAlignment.Center
                            layout: StackLayout { orientation: LayoutOrientation.TopToBottom }
                            ImageView {
                                horizontalAlignment: HorizontalAlignment.Center
                                preferredWidth: ui.du(22); preferredHeight: ui.du(22)
                                scalingMethod: ScalingMethod.AspectFill
                                imageSource: chatViewPage.avatarUrl.length > 0
                                             ? chatViewPage.avatarUrl : "asset:///images/ic_contact.png"
                            }
                            Label {
                                text: chatViewPage.threadName
                                horizontalAlignment: HorizontalAlignment.Center
                                textStyle { color: Color.White; base: SystemDefaults.TextStyles.TitleText; fontWeight: FontWeight.Bold }
                                topMargin: ui.du(2)
                            }
                            Label {
                                text: "Connecting video..."
                                horizontalAlignment: HorizontalAlignment.Center
                                textStyle { color: Color.create("#aaaaaa"); fontSize: FontSize.Medium }
                                topMargin: ui.du(1)
                            }
                        }
                        Container {
                            horizontalAlignment: HorizontalAlignment.Right
                            verticalAlignment:   VerticalAlignment.Top
                            rightPadding: ui.du(2); topPadding: ui.du(2)
                            preferredWidth: ui.du(16); preferredHeight: ui.du(22)
                            background: Color.create("#333333")
                            layout: DockLayout {}
                            Label { text: "You"; horizontalAlignment: HorizontalAlignment.Center; verticalAlignment: VerticalAlignment.Center; textStyle { color: Color.create("#888888"); fontSize: FontSize.Small } }
                        }
                    }
                    Container {
                        horizontalAlignment: HorizontalAlignment.Fill
                        background: Color.create("#1a1a1a")
                        topPadding: ui.du(2); bottomPadding: ui.du(3)
                        layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                        Container { layoutProperties: StackLayoutProperties { spaceQuota: 1 } }
                        Container {
                            layout: StackLayout { orientation: LayoutOrientation.TopToBottom }
                            ImageButton { horizontalAlignment: HorizontalAlignment.Center; preferredWidth: ui.du(10); preferredHeight: ui.du(10); defaultImageSource: "asset:///images/ic_microphone.png"; pressedImageSource: "asset:///images/ic_microphone.png"; onClicked: {} }
                            Label { text: "Mute"; horizontalAlignment: HorizontalAlignment.Center; textStyle { color: Color.White; fontSize: FontSize.XSmall } }
                        }
                        Container { layoutProperties: StackLayoutProperties { spaceQuota: 1 } }
                        Container {
                            layout: StackLayout { orientation: LayoutOrientation.TopToBottom }
                            ImageButton { horizontalAlignment: HorizontalAlignment.Center; preferredWidth: ui.du(13); preferredHeight: ui.du(13); defaultImageSource: "asset:///images/ca_video_chat_active.png"; pressedImageSource: "asset:///images/ca_video_chat_active.png"; onClicked: { videoCallSheet.close() } }
                            Label { text: "End"; horizontalAlignment: HorizontalAlignment.Center; textStyle { color: Color.White; fontSize: FontSize.XSmall } }
                        }
                        Container { layoutProperties: StackLayoutProperties { spaceQuota: 1 } }
                        Container {
                            layout: StackLayout { orientation: LayoutOrientation.TopToBottom }
                            ImageButton { horizontalAlignment: HorizontalAlignment.Center; preferredWidth: ui.du(10); preferredHeight: ui.du(10); defaultImageSource: "asset:///images/ic_video_chat.png"; pressedImageSource: "asset:///images/ic_video_chat.png"; onClicked: {} }
                            Label { text: "Flip"; horizontalAlignment: HorizontalAlignment.Center; textStyle { color: Color.White; fontSize: FontSize.XSmall } }
                        }
                        Container { layoutProperties: StackLayoutProperties { spaceQuota: 1 } }
                    }
                }
            }
        },

        Connections {
            target: zService

            onMessagesReady: {
                // Bỏ qua nếu không phải thread đang mở
                if (threadId !== chatViewPage.threadId) return;

                // Tập hợp tất cả msgId thật đã có trong model
                var existing = chatViewPage.buildExistingIds();

                var added = false;
                for (var j = 0; j < messages.length; j++) {
                    var msg = messages[j];
                    // Bỏ qua tin đã có
                    if (existing[msg.msgId]) continue;

                    // Nếu là tin của mình → xóa local placeholder trùng content
                    if (chatViewPage.normMine(msg.isMine)) {
                        chatViewPage.removeLocalPlaceholder(msg.content);
                    }

                    var nm = msg;
                    nm.selfName = chatViewPage.selfName;
                    msgModel.append(nm);
                    added = true;
                }

                if (added) {
                    chatViewPage.rebuildGroups();
                    msgList.scrollToPosition(ScrollPosition.End, ScrollAnimation.Smooth);
                }
            }

            onMessageSent: {
                if (threadId !== chatViewPage.threadId) return;
                sendAction.enabled = (inputField.text.trim().length > 0);

                if (success && chatViewPage.pendingMsg !== "") {
                    // Thêm local placeholder — KHÔNG lưu DB
                    // Sẽ bị xóa khi tin thật từ server về qua onMessagesReady/onNewMessage
                    var m = {
                        msgId:    "local_" + new Date().getTime(),
                        content:  chatViewPage.pendingMsg,
                        isMine:   true,
                        isGroup:  chatViewPage.isGroup,
                        senderId: "self",
                        dName:    chatViewPage.selfName,
                        ts:       String(new Date().getTime()),
                        selfName: chatViewPage.selfName
                    };
                    msgModel.append(m);
                    chatViewPage.rebuildGroups();
                    msgList.scrollToPosition(ScrollPosition.End, ScrollAnimation.Smooth);
                    chatViewPage.pendingMsg = "";
                }
            }

            onNewMessage: {
                if (threadId !== chatViewPage.threadId) return;

                // Bỏ qua nếu msgId đã có
                for (var i = 0; i < msgModel.size(); i++) {
                    if (msgModel.value(i).msgId === message.msgId) return;
                }

                var msg = message;
                msg.selfName = chatViewPage.selfName;

                // Nếu là tin của mình → xóa local placeholder trùng content
                if (chatViewPage.normMine(msg.isMine)) {
                    chatViewPage.removeLocalPlaceholder(msg.content);
                }

                msgModel.append(msg);
                chatViewPage.rebuildGroups();
                msgList.scrollToPosition(ScrollPosition.End, ScrollAnimation.Smooth);
            }
        }
    ]
}
