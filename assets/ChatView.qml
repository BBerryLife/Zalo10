import bb.cascades 1.4
import bb.cascades.pickers 1.0
import bb.system 1.0
import QtQuick 1.0

Page {
    id: chatViewPage

    property string threadId:    ""
    property string threadName:  ""
    property bool   isGroup:     false
    property string avatarUrl:   ""
    property string selfName:    ""
    property string pendingMsg:  ""
    property bool   initialized: false
    property bool   emojiPanelOpen: false
    property string pendingAttachPath: ""  // path selected by FilePicker
    property variant dbIsMineCache: ({})     // msgId -> isMine từ DB, làm nguồn tin cậy

    // - TITLE BAR -
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

    // Gọi từ JS sau khi assign đủ threadId + selfName + isGroup
    function startChat() {
        if (chatViewPage.initialized) return;
        if (chatViewPage.threadId === "") return;
        if (chatViewPage.selfName === "") chatViewPage.selfName = "Me";
        chatViewPage.initialized = true;

        msgModel.clear();
        zService.setActiveThread(chatViewPage.threadId, chatViewPage.isGroup);

        var cached = zService.dbLoadMessages(chatViewPage.threadId);
        if (cached && cached.length > 0) {
            // Reset cache mỗi lần mở conversation mới
            var newCache = {};
            for (var i = 0; i < cached.length; i++) {
                var c = cached[i];
                // Always stamp selfName so label shows correctly
                c.selfName = chatViewPage.selfName || "Me";
                // Normalize isMine to boolean NOW before append
                if (c.isMine === "true" || c.isMine === 1 || c.isMine === true) {
                    c.isMine = true;
                } else {
                    c.isMine = false;
                }
                // Lưu isMine vào cache — DB là nguồn chính xác nhất
                if (c.msgId) newCache[c.msgId] = c.isMine;
                msgModel.append(c);

                // FIX5: Ảnh reload từ DB không có localImage → trigger download thumbnail
                var isPhoto = (c.msgType === 2 || c.msgType === "2");
                var hasLocal = (c.localImage && c.localImage.length > 0);
                if (isPhoto && !hasLocal && c.msgId) {
                    var ct = c.content || "";
                    if (ct.length > 1 && ct.charAt(0) === "{") {
                        var m1 = ct.match(/"(?:thumbUrl|normalUrl|hdUrl)"\s*:\s*"([^"]+)"/);
                        if (m1 && m1[1])
                            zService.downloadImageMessage(c.msgId, m1[1]);
                    }
                }
            }
            chatViewPage.dbIsMineCache = newCache;
            chatViewPage.rebuildGroups();
            msgList.scrollToPosition(ScrollPosition.End, ScrollAnimation.None);
        }

        zService.fetchMessages(chatViewPage.threadId, chatViewPage.isGroup);
    }

    onThreadIdChanged: {
        chatViewPage.initialized = false;
        msgModel.clear();
        chatViewPage.dbIsMineCache = {};  // FIX1: reset cache for new thread
    }

    // - CONTENT -
    content: Container {
        layout: StackLayout { orientation: LayoutOrientation.TopToBottom }
        horizontalAlignment: HorizontalAlignment.Fill
        verticalAlignment:   VerticalAlignment.Fill
        background: Color.create("#d6d6d6")

        // - Message list -
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

                        // - Bubble chiếm phần còn lại -
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

                            // Nội dung tin nhắn — text hoặc ảnh
                            // BB10 QtQuick 1.0: property bindings MUST be single expressions,
                            // không được dùng block { var x; return x } trong property declaration.
                            Container {
                                // isImage: msgType==2 OR content starts with '{' and has image keys
                                property bool isImage: (ListItemData.msgType === 2 || ListItemData.msgType === "2")
                                    || (typeof ListItemData.content === "string"
                                        && ListItemData.content.length > 1
                                        && ListItemData.content.charAt(0) === "{"
                                        && (ListItemData.content.indexOf("thumb") >= 0
                                            || ListItemData.content.indexOf("normalUrl") >= 0
                                            || ListItemData.content.indexOf("href") >= 0))

                                // localImage takes priority; fall back to empty (download triggered separately)
                                property string imageUrl: ListItemData.localImage || ""

                                topMargin: 0; bottomMargin: 0

                                // - Text message -
                                Label {
                                    visible: !parent.isImage
                                    text: {
                                        if (typeof ListItemData.content === "string" && ListItemData.content.length > 0)
                                            return ListItemData.content;
                                        // msgType=2: image — show [Photo]
                                        if (ListItemData.msgType === 2 || ListItemData.msgType === "2")
                                            return "[Photo]";
                                        // msgType=6: sticker
                                        if (ListItemData.msgType === 6 || ListItemData.msgType === "6")
                                            return "[Sticker]";
                                        // Default: show content or placeholder
                                        return "[Photo]";
                                    }
                                    textStyle {
                                        base:  SystemDefaults.TextStyles.BodyText
                                        color: Color.create("#111111")
                                    }
                                    multiline: true
                                    topMargin: 0; bottomMargin: 0
                                }
                                // - Image message — full bubble width -
                                Container {
                                    visible: parent.isImage
                                    horizontalAlignment: HorizontalAlignment.Fill
                                    topMargin: 2; bottomMargin: 2
                                    // Giới hạn chiều cao tối đa, rộng fill theo bubble
                                    preferredHeight: ui.du(30)
                                    minHeight:       ui.du(12)
                                    background: Color.create("#e0e0e0")
                                    layout: DockLayout {}
                                    ImageView {
                                        horizontalAlignment: HorizontalAlignment.Fill
                                        verticalAlignment:   VerticalAlignment.Fill
                                        scalingMethod: ScalingMethod.AspectFit
                                        imageSource: parent.parent.imageUrl
                                        visible: parent.parent.imageUrl.length > 0
                                    }
                                    Label {
                                        visible: parent.parent.imageUrl.length === 0
                                        text: "[Photo]"
                                        horizontalAlignment: HorizontalAlignment.Center
                                        verticalAlignment:   VerticalAlignment.Center
                                        textStyle { color: Color.create("#888888"); fontSize: FontSize.Small }
                                    }
                                }
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



        // - Emoji Picker Panel -
        // Chiều cao cố định ~250dp giống BBM keyboard replacement panel
        EmojiPanel {
            id: emojiPanel
            horizontalAlignment: HorizontalAlignment.Fill
            preferredHeight: ui.du(19)
            minHeight: ui.du(16)
            visible: false
            onEmojiPicked: {
                inputField.text = inputField.text + charStr
            }
        }


        // - Input bar -
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
                onClicked: { filePicker.open() }
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
        if (!chatViewPage.threadId || chatViewPage.threadId === "") return;
        chatViewPage.pendingMsg = txt;
        sendAction.enabled = false;
        inputField.text = "";
        zService.sendMessage(chatViewPage.threadId, txt, chatViewPage.isGroup);
    }

    // - isMine normalizer -
    function normMine(v) {
        if (v === true  || v === 1)  return true;
        if (v === false || v === 0)  return false;
        if (typeof v === "string")   return (v === "true" || v === "1");
        return false;
    }

    // - Xây lại bubblePos + grouped + latestTs cho toàn bộ model
    function rebuildGroups() {
        var size = msgModel.size();
        if (size === 0) return;

        // Pass 1: collect all items into JS array (avoid re-entrancy from replace during iteration)
        var items = [];
        for (var i = 0; i < size; i++) {
            items.push(msgModel.value(i));
        }

        // Pass 2: compute grouping on JS array
        for (var i = 0; i < size; i++) {
            var cur  = items[i];
            var prev = (i > 0)        ? items[i - 1] : null;
            var next = (i < size - 1) ? items[i + 1] : null;

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
            // FIX1: do NOT overwrite selfName here — it was set at append time
            // cur.selfName is already correct per-item
            cur.isMine    = curMine;

            if (!sameNext) {
                cur.latestTs = cur.ts;
                var k = i - 1;
                while (k >= 0) {
                    if (chatViewPage.normMine(items[k].isMine) !== curMine) break;
                    items[k].latestTs = cur.ts;
                    k--;
                }
            }
        }

        // Pass 3: apply back to model in one batch
        for (var i = 0; i < size; i++) {
            msgModel.replace(i, items[i]);
        }
    }

    // - Helper: lấy set msgId hiện có trong model (chỉ id thật, bỏ local_)
    function buildExistingIds() {
        var ids = {};
        for (var i = 0; i < msgModel.size(); i++) {
            var mid = msgModel.value(i).msgId;
            if (mid && mid.indexOf("local_") !== 0)
                ids[mid] = true;
        }
        return ids;
    }

    // - Helper: xóa local placeholder có content khớp (tìm từ cuối lên)
    function removeLocalPlaceholder(content) {
        // Xóa placeholder text khớp content
        for (var k = msgModel.size() - 1; k >= 0; k--) {
            var item = msgModel.value(k);
            if (item.msgId && item.msgId.indexOf("local_") === 0
                    && item.content === content) {
                msgModel.removeAt(k);
                return;
            }
        }
    }

    // Xóa placeholder ảnh (local_img_*) — content="" nên cần match theo msgType
    function removeLocalImagePlaceholder() {
        for (var k = msgModel.size() - 1; k >= 0; k--) {
            var item = msgModel.value(k);
            if (item.msgId && item.msgId.indexOf("local_img_") === 0) {
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
                    nm.selfName = chatViewPage.selfName || "Me";
                    // FIX1: normalize isMine — trust DB cache if available (server uid may be stale)
                    var rawMine = (nm.isMine === true || nm.isMine === 1 || nm.isMine === "true" || nm.isMine === "1");
                    var cachedMine = chatViewPage.dbIsMineCache[nm.msgId];
                    nm.isMine = (cachedMine !== undefined) ? cachedMine : rawMine;
                    // Update cache with confirmed value
                    if (nm.msgId) {
                        var upd = chatViewPage.dbIsMineCache;
                        upd[nm.msgId] = nm.isMine;
                        chatViewPage.dbIsMineCache = upd;
                    }
                    msgModel.append(nm);
                    added = true;

                    // Trigger thumbnail download cho tin ảnh chưa có localImage
                    if (nm.msgType === 2 || nm.msgType === "2") {
                        if (!nm.localImage || nm.localImage.length === 0) {
                            var c = nm.content;
                            if (typeof c === "string" && c.charAt(0) === "{") {
                                var m2 = c.match(/"(?:thumbUrl|normalUrl|hdUrl)"\s*:\s*"([^"]+)"/);
                                if (m2 && m2[1])
                                    zService.downloadImageMessage(nm.msgId, m2[1]);
                            }
                        }
                    }
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
                msg.selfName = chatViewPage.selfName || "Me";
                // FIX1: normalize isMine — trust DB cache if available
                var newMsgRaw = (msg.isMine === true || msg.isMine === 1 || msg.isMine === "true" || msg.isMine === "1");
                var newMsgCached = chatViewPage.dbIsMineCache[msg.msgId];
                msg.isMine = (newMsgCached !== undefined) ? newMsgCached : newMsgRaw;
                // Update cache
                if (msg.msgId) {
                    var updC = chatViewPage.dbIsMineCache;
                    updC[msg.msgId] = msg.isMine;
                    chatViewPage.dbIsMineCache = updC;
                }

                // Nếu là tin của mình → xóa local placeholder trùng content
                if (chatViewPage.normMine(msg.isMine)) {
                    if (msg.msgType === 2 || msg.msgType === "2") {
                        // Ảnh: placeholder có content="" nên dùng hàm riêng
                        chatViewPage.removeLocalImagePlaceholder();
                    } else {
                        chatViewPage.removeLocalPlaceholder(msg.content);
                    }
                }

                msgModel.append(msg);
                chatViewPage.rebuildGroups();
                msgList.scrollToPosition(ScrollPosition.End, ScrollAnimation.Smooth);

                // Nếu tin là ảnh, trigger download thumbnail để hiển thị
                // Nếu localImage đã có (ảnh mình vừa gửi) thì KHÔNG download lại
                if (msg.msgType === 2 || msg.msgType === "2") {
                    if (!msg.localImage || msg.localImage.length === 0) {
                        var c = msg.content;
                        if (typeof c === "string" && c.charAt(0) === "{") {
                            var thumbMatch = c.match(/"(?:thumbUrl|normalUrl|hdUrl)"\s*:\s*"([^"]+)"/);
                            if (thumbMatch && thumbMatch[1])
                                zService.downloadImageMessage(msg.msgId, thumbMatch[1]);
                        }
                    }
                }
            }

            onImageMsgReady: {
                // Update the message in model with localImage path
                for (var j = 0; j < msgModel.size(); j++) {
                    var d = msgModel.value(j);
                    if ((d.msgId || "") === msgId) {
                        d.localImage = localPath;
                        msgModel.removeAt(j);
                        msgModel.insert(j, d);
                        break;
                    }
                }
            }
        },

        // - FilePicker for all file types (ảnh + tài liệu + video ...) -
        FilePicker {
            id: filePicker
            type: FileType.Other   // Cho phép mọi loại file
            title: "Select File"
            // Bao gồm tất cả thư mục phổ biến
            directories: [
                "/accounts/1000/shared/camera",
                "/accounts/1000/shared/photos",
                "/accounts/1000/shared/documents",
                "/accounts/1000/shared/downloads",
                "/accounts/1000/shared/videos",
                "/accounts/1000/shared/voice"
            ]
            onFileSelected: {
                var path = selectedFiles[0];
                chatViewPage.pendingAttachPath = path;

                // Xác định là ảnh hay file thường
                var ext = path.substring(path.lastIndexOf('.') + 1).toLowerCase();
                var isImg = (ext === "jpg" || ext === "jpeg" || ext === "png"
                             || ext === "gif" || ext === "webp" || ext === "bmp");

                if (isImg) {
                    // Hiện preview ngay lập tức
                    var m = {
                        msgId:      "local_img_" + new Date().getTime(),
                        content:    "",
                        msgType:    2,
                        localImage: "file://" + path,
                        isMine:     true,
                        isGroup:    chatViewPage.isGroup,
                        senderId:   "self",
                        dName:      chatViewPage.selfName,
                        ts:         String(new Date().getTime()),
                        selfName:   chatViewPage.selfName
                    };
                    msgModel.append(m);
                    chatViewPage.rebuildGroups();
                    msgList.scrollToPosition(ScrollPosition.End, ScrollAnimation.Smooth);
                    zService.sendPhoto(chatViewPage.threadId, path, chatViewPage.isGroup);
                } else {
                    // File thường — placeholder text
                    var fname = path.substring(path.lastIndexOf('/') + 1);
                    var mf = {
                        msgId:    "local_file_" + new Date().getTime(),
                        content:  "[File: " + fname + "]",
                        msgType:  0,
                        isMine:   true,
                        isGroup:  chatViewPage.isGroup,
                        senderId: "self",
                        dName:    chatViewPage.selfName,
                        ts:       String(new Date().getTime()),
                        selfName: chatViewPage.selfName
                    };
                    msgModel.append(mf);
                    chatViewPage.rebuildGroups();
                    msgList.scrollToPosition(ScrollPosition.End, ScrollAnimation.Smooth);
                    zService.sendFile(chatViewPage.threadId, path, chatViewPage.isGroup);
                }
            }
        }
    ]
}
