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
    property string pendingAttachPath: ""
    property variant dbIsMineCache: ({})
    property bool   isMuted: false
    property bool   isBlocked: false
    property bool   popRequested: false
    property variant pendingImageUpdates: ([])
    property bool   pageVisible: false
    property bool   isDark: app.getDarkTheme()

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

    // Apply a single image update into the model
    function applyImageUpdate(msgId, localPath) {
        // BB10 ImageView does not re-render when imageSource changes via replace() or removeAt+insert.
        // The only reliable fix: snapshot all items, set localImage, clear model, re-append all.
        // This forces BB10 to create fresh delegates with the correct imageSource from the start.
        var size = msgModel.size();
        if (size === 0) {
            console.log("[QML] applyImageUpdate: model empty, msgId=" + msgId);
            return;
        }
        var found = false;
        var snapshot = [];
        for (var j = 0; j < size; j++) {
            var d = msgModel.value(j);
            if ((d.msgId || "") === msgId) {
                d.localImage = localPath;
                found = true;
                console.log("[QML] applyImageUpdate: set idx=" + j + " msgId=" + msgId);
            }
            snapshot.push(d);
        }
        if (!found) {
            console.log("[QML] applyImageUpdate: NOT found msgId=" + msgId);
            return;
        }
        // Full rebuild so BB10 creates new delegates with correct imageSource
        msgModel.clear();
        for (var k = 0; k < snapshot.length; k++) {
            msgModel.append(snapshot[k]);
        }
        msgList.scrollToPosition(ScrollPosition.End, ScrollAnimation.None);
    }

    // Flush any image updates that arrived before page was visible
    function flushPendingImages() {
        var pending = chatViewPage.pendingImageUpdates;
        if (!pending || pending.length === 0) return;
        console.log("[QML] flushPendingImages: count=" + pending.length);
        for (var i = 0; i < pending.length; i++) {
            chatViewPage.applyImageUpdate(pending[i].msgId, pending[i].localPath);
        }
        chatViewPage.pendingImageUpdates = [];
    }

    // Gọi từ JS sau khi assign đủ threadId + selfName + isGroup
    function startChat() {
        if (chatViewPage.initialized) return;
        if (chatViewPage.threadId === "") return;
        chatViewPage.pageVisible = false;  // Will be set true in onPushTransitionEnded
        chatViewPage.pendingImageUpdates = [];
        if (chatViewPage.selfName === "") chatViewPage.selfName = "Me";
        chatViewPage.initialized = true;

        // Init block/mute state from C++ in-memory sets
        chatViewPage.isBlocked = zService.isBlocked(chatViewPage.threadId);
        chatViewPage.isMuted   = zService.isMutedThread(chatViewPage.threadId);
        blockedBanner.visible  = chatViewPage.isBlocked;

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
                    var photoUrl1 = chatViewPage.extractPhotoUrl(c.content || "");
                    if (photoUrl1.length > 0)
                        zService.downloadImageMessage(c.msgId, photoUrl1, chatViewPage.threadId);
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
        chatViewPage.pageVisible = false;
        chatViewPage.pendingImageUpdates = [];
        msgModel.clear();
        chatViewPage.dbIsMineCache = {};  // FIX1: reset cache for new thread
    }

    // - CONTENT -
    content: Container {
        layout: StackLayout { orientation: LayoutOrientation.TopToBottom }
        horizontalAlignment: HorizontalAlignment.Fill
        verticalAlignment:   VerticalAlignment.Fill
        background: chatViewPage.isDark ? Color.create("#1a1a1a") : Color.create("#d6d6d6")

        // - Message list -
        ListView {
            id: msgList
            property bool isDark: chatViewPage.isDark
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

                        property bool isDark: ListItem.view.isDark

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
                            background: rowRoot.isDark ? Color.create("#2a2a2a") : Color.White
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
                                            ? (rowRoot.isDark ? Color.create("#aaaaaa") : Color.create("#555555"))
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
                                    textStyle { fontSize: FontSize.XSmall; color: rowRoot.isDark ? Color.create("#888888") : Color.create("#777777") }
                                    topMargin: 0; bottomMargin: 0
                                }
                            }

                            // Nội dung tin nhắn — text hoặc ảnh
                            // BB10 QtQuick 1.0: property bindings MUST be single expressions,
                            // không được dùng block { var x; return x } trong property declaration.
                            Container {
                                id: msgContentRoot
                                // BB10 QtQuick 1.0: only SIMPLE expressions re-evaluate on replace().
                                // No block { var x; return x } syntax — use inline ternary only.
                                topMargin: 0; bottomMargin: 0

                                // - Text message: visible when NOT a photo -
                                Label {
                                    visible: (ListItemData.msgType !== 2 && ListItemData.msgType !== "2")
                                             && !(typeof ListItemData.content === "string"
                                                  && ListItemData.content.length > 1
                                                  && ListItemData.content.charAt(0) === "{"
                                                  && (ListItemData.content.indexOf("normalUrl") >= 0
                                                      || ListItemData.content.indexOf("thumbUrl") >= 0
                                                      || ListItemData.content.indexOf("thumb") >= 0
                                                      || ListItemData.content.indexOf("href") >= 0))
                                    text: (typeof ListItemData.content === "string" && ListItemData.content.length > 0)
                                          ? ListItemData.content
                                          : ((ListItemData.msgType === 2 || ListItemData.msgType === "2")
                                             ? "[Photo]"
                                             : ((ListItemData.msgType === 6 || ListItemData.msgType === "6")
                                                ? "[Sticker]" : "[Photo]"))
                                    textStyle {
                                        base:  SystemDefaults.TextStyles.BodyText
                                        color: rowRoot.isDark ? Color.create("#eeeeee") : Color.create("#111111")
                                    }
                                    multiline: true
                                    topMargin: 0; bottomMargin: 0
                                }
                                // - Image message — full bubble width -
                                Container {
                                    visible: (ListItemData.msgType === 2 || ListItemData.msgType === "2")
                                             || (typeof ListItemData.content === "string"
                                                 && ListItemData.content.length > 1
                                                 && ListItemData.content.charAt(0) === "{"
                                                 && (ListItemData.content.indexOf("normalUrl") >= 0
                                                     || ListItemData.content.indexOf("thumbUrl") >= 0
                                                     || ListItemData.content.indexOf("thumb") >= 0
                                                     || ListItemData.content.indexOf("href") >= 0))
                                    horizontalAlignment: HorizontalAlignment.Fill
                                    topMargin: 2; bottomMargin: 2
                                    preferredHeight: ui.du(30)
                                    minHeight:       ui.du(12)
                                    background: rowRoot.isDark ? Color.create("#3a3a3a") : Color.create("#e0e0e0")
                                    layout: DockLayout {}
                                    ImageView {
                                        horizontalAlignment: HorizontalAlignment.Fill
                                        verticalAlignment:   VerticalAlignment.Fill
                                        scalingMethod: ScalingMethod.AspectFit
                                        // Always visible — BB10 renders nothing when imageSource is empty
                                        // visible binding with !== does NOT re-evaluate after replace() on BB10
                                        imageSource: ListItemData.localImage
                                    }
                                    Label {
                                        visible: !ListItemData.localImage || ListItemData.localImage === ""
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
            isDark: chatViewPage.isDark
            onEmojiPicked: {
                inputField.text = inputField.text + charStr
            }
        }


        // - Blocked banner -
        Container {
            id: blockedBanner
            visible: false
            horizontalAlignment: HorizontalAlignment.Fill
            background: Color.create("#c0392b")
            topPadding: ui.du(1.2)
            bottomPadding: ui.du(1.2)
            leftPadding: ui.du(2)
            rightPadding: ui.du(2)
            Label {
                text: "You have blocked this person. They cannot send you messages."
                textStyle {
                    color: Color.White
                    fontSize: FontSize.Small
                }
                multiline: true
                horizontalAlignment: HorizontalAlignment.Fill
            }
        }

        // - Input bar (BBM style) -
        Container {
            horizontalAlignment: HorizontalAlignment.Fill
            background: chatViewPage.isDark ? Color.create("#272727") : Color.White
            topPadding:    ui.du(1.2)
            bottomPadding: ui.du(1.2)
            leftPadding:   ui.du(1.0)
            rightPadding:  ui.du(1.0)
            layout: StackLayout { orientation: LayoutOrientation.LeftToRight }

            // Attach icon
            ImageButton {
                verticalAlignment: VerticalAlignment.Center
                preferredWidth:  ui.du(8); preferredHeight: ui.du(8)
                rightMargin: ui.du(0.8)
                defaultImageSource: chatViewPage.isDark ? "asset:///images/attach_icon.png" : "asset:///images/ic_attach.png"
                pressedImageSource: chatViewPage.isDark ? "asset:///images/attach_icon.png" : "asset:///images/ic_attach.png"
                onClicked: { filePicker.open() }
            }

            // TextField
            TextField {
                id: inputField
                layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                verticalAlignment: VerticalAlignment.Center
                hintText: "Enter a message"
                inputMode: TextFieldInputMode.Chat
                minHeight: ui.du(7)
                backgroundVisible: false
                clearButtonVisible: false
                input {
                    flags: TextInputFlag.SpellCheck | TextInputFlag.WordSubstitution
                    submitKey: SubmitKey.Send
                    onSubmitted: { doSend() }
                }
                onTextChanging: {
                    sendAction.enabled = (inputField.text.trim().length > 0)
                }
            }

            // Timed message icon — preferredWidth wider than height to match 116x96 ratio
            ImageButton {
                verticalAlignment: VerticalAlignment.Center
                preferredWidth:  ui.du(11); preferredHeight: ui.du(9)
                leftMargin: ui.du(0.6)
                defaultImageSource: chatViewPage.isDark ? "asset:///images/timemesswhite.png" : "asset:///images/timemess.png"
                pressedImageSource: chatViewPage.isDark ? "asset:///images/timemesswhite.png" : "asset:///images/timemess.png"
                onClicked: { timedMsgDialog.show() }
            }

            // Emoji icon
            ImageButton {
                id: emoticonBtn
                verticalAlignment: VerticalAlignment.Center
                preferredWidth:  ui.du(8); preferredHeight: ui.du(8)
                leftMargin: ui.du(0.5)
                defaultImageSource: emojiPanelOpen
                    ? (chatViewPage.isDark ? "asset:///images/ic_keyboard_enabled.png" : "asset:///images/emoji/darkkeyboard.png")
                    : (chatViewPage.isDark ? "asset:///images/ic_emoticon_enabled_white.png" : "asset:///images/ic_emoticon_enabled.png")
                pressedImageSource: emojiPanelOpen
                    ? (chatViewPage.isDark ? "asset:///images/ic_keyboard_enabled.png" : "asset:///images/emoji/darkkeyboard.png")
                    : (chatViewPage.isDark ? "asset:///images/ic_emoticon_enabled_white.png" : "asset:///images/ic_emoticon_enabled.png")
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
        },
        ActionItem {
            id: muteAction
            title: chatViewPage.isMuted ? "Unmute" : "Mute notifications"
            imageSource: "asset:///images/ic_notifications_off.png"
            ActionBar.placement: ActionBarPlacement.InOverflow
            onTriggered: {
                zService.setMute(chatViewPage.threadId, chatViewPage.isGroup, !chatViewPage.isMuted);
            }
        },
        ActionItem {
            title: chatViewPage.isBlocked ? "Unblock user" : "Block user"
            imageSource: "asset:///images/ic_block_contact.png"
            ActionBar.placement: ActionBarPlacement.InOverflow
            enabled: !chatViewPage.isGroup
            onTriggered: {
                if (chatViewPage.isBlocked)
                    zService.unblockUser(chatViewPage.threadId);
                else
                    blockDialog.show();
            }
        },
        ActionItem {
            title: "Clear history"
            imageSource: "asset:///images/clear_chat.png"
            ActionBar.placement: ActionBarPlacement.InOverflow
            onTriggered: { clearHistoryDialog.show() }
        },
        ActionItem {
            title: "Leave group"
            imageSource: "asset:///images/ic_chat_leave.png"
            ActionBar.placement: ActionBarPlacement.InOverflow
            enabled: chatViewPage.isGroup
            onTriggered: { leaveGroupDialog.show() }
        }
    ]

    function doSend() {
        var txt = inputField.text.trim();
        if (txt.length === 0) return;
        if (!chatViewPage.threadId || chatViewPage.threadId === "") return;
        sendAction.enabled = false;
        inputField.text = "";

        // Optimistic placeholder added BEFORE HTTP send to avoid race with WS cmd=501
        var placeholder = {
            msgId:    "local_" + new Date().getTime(),
            content:  txt,
            isMine:   true,
            isGroup:  chatViewPage.isGroup,
            senderId: "self",
            dName:    chatViewPage.selfName,
            ts:       String(new Date().getTime()),
            selfName: chatViewPage.selfName
        };
        msgModel.append(placeholder);
        chatViewPage.rebuildGroups();
        msgList.scrollToPosition(ScrollPosition.End, ScrollAnimation.Smooth);

        chatViewPage.pendingMsg = txt;
        zService.sendMessage(chatViewPage.threadId, txt, chatViewPage.isGroup);
    }

    // - isMine normalizer -
    function normMine(v) {
        if (v === true  || v === 1)  return true;
        if (v === false || v === 0)  return false;
        if (typeof v === "string")   return (v === "true" || v === "1");
        return false;
    }

    // Extract best available photo URL from msgType=2 content string
    // Tries thumbUrl, normalUrl, hdUrl, href, thumb in order — skips empty values
    function extractPhotoUrl(content) {
        if (typeof content !== "string" || content.length === 0) return "";
        if (content.charAt(0) === "{") {
            // Try each key individually so we skip empty-string values
            var keys = ["thumbUrl", "normalUrl", "hdUrl", "href", "thumb", "oriUrl"];
            for (var k = 0; k < keys.length; k++) {
                var re = new RegExp("\"" + keys[k] + "\"\\s*:\\s*\"([^\"]+)\"");
                var m = content.match(re);
                if (m && m[1] && m[1].length > 0) return m[1];
            }
        }
        // Fallback: content itself might be a bare URL
        if (content.indexOf("http") === 0) return content;
        return "";
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

            // FIX6: trong group, phân biệt theo senderId — không chỉ mine/not-mine
            // Nếu cả hai đều là !isMine nhưng senderId khác nhau → KHÔNG gộp
            var curSender  = cur.senderId  || "";
            var prevSender = prev ? (prev.senderId  || "") : "";
            var nextSender = next ? (next.senderId  || "") : "";

            var samePrev = (prev !== null) && (prevMine === curMine)
                           && (!chatViewPage.isGroup || curMine || curSender === prevSender);
            var sameNext = (next !== null) && (nextMine === curMine)
                           && (!chatViewPage.isGroup || curMine || curSender === nextSender);

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
                            var photoUrl2 = chatViewPage.extractPhotoUrl(nm.content || "");
                            if (photoUrl2.length > 0)
                                zService.downloadImageMessage(nm.msgId, photoUrl2, chatViewPage.threadId);
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
                if (!success) {
                    // Send failed — remove placeholder and restore input
                    chatViewPage.removeLocalPlaceholder(chatViewPage.pendingMsg);
                    inputField.text = chatViewPage.pendingMsg;
                    sendAction.enabled = true;
                }
                chatViewPage.pendingMsg = "";
            }

            onNewMessage: {
                if (threadId !== chatViewPage.threadId) return;

                var msg = message;
                msg.selfName = chatViewPage.selfName || "Me";
                var newMsgRaw = (msg.isMine === true || msg.isMine === 1 || msg.isMine === "true" || msg.isMine === "1");
                var newMsgCached = chatViewPage.dbIsMineCache[msg.msgId];
                msg.isMine = (newMsgCached !== undefined) ? newMsgCached : newMsgRaw;
                if (msg.msgId) {
                    var updC = chatViewPage.dbIsMineCache;
                    updC[msg.msgId] = msg.isMine;
                    chatViewPage.dbIsMineCache = updC;
                }

                // Remove local placeholder BEFORE duplicate check to avoid race condition
                if (chatViewPage.normMine(msg.isMine)) {
                    if (msg.msgType === 2 || msg.msgType === "2") {
                        // FIX5: Giữ lại localImage từ placeholder để hiển thị ngay, không cần download
                        var savedLocalImage = "";
                        for (var pi = msgModel.size() - 1; pi >= 0; pi--) {
                            var pitem = msgModel.value(pi);
                            if (pitem.msgId && pitem.msgId.indexOf("local_img_") === 0) {
                                savedLocalImage = pitem.localImage || "";
                                msgModel.removeAt(pi);
                                break;
                            }
                        }
                        if (savedLocalImage.length > 0) msg.localImage = savedLocalImage;
                    } else {
                        chatViewPage.removeLocalPlaceholder(msg.content);
                    }
                }

                // Duplicate check after placeholder removal
                for (var di = 0; di < msgModel.size(); di++) {
                    if (msgModel.value(di).msgId === msg.msgId) return;
                }

                msgModel.append(msg);
                chatViewPage.rebuildGroups();
                msgList.scrollToPosition(ScrollPosition.End, ScrollAnimation.Smooth);

                // Trigger thumbnail download for image messages
                if (msg.msgType === 2 || msg.msgType === "2") {
                    if (!msg.localImage || msg.localImage.length === 0) {
                        var photoUrl3 = chatViewPage.extractPhotoUrl(msg.content || "");
                        if (photoUrl3.length > 0)
                            zService.downloadImageMessage(msg.msgId, photoUrl3, chatViewPage.threadId);
                    }
                }
            }

            onImageMsgReady: {
                if (chatViewPage.pageVisible) {
                    // Page visible: apply immediately
                    chatViewPage.applyImageUpdate(msgId, localPath);
                } else {
                    // Page not yet visible: queue for flush when page becomes active
                    var pending = chatViewPage.pendingImageUpdates;
                    pending.push({ msgId: msgId, localPath: localPath });
                    chatViewPage.pendingImageUpdates = pending;
                    console.log("[QML] onImageMsgReady: queued msgId=" + msgId + " pending=" + pending.length);
                }
            }

            onMuteDone: {
                if (threadId !== chatViewPage.threadId) return;
                if (success) chatViewPage.isMuted = muted;
            }

            onBlockUserDone: {
                if (userId !== chatViewPage.threadId) return;
                if (success) {
                    chatViewPage.isBlocked = true;
                    blockedBanner.visible  = true;
                }
            }

            onUnblockUserDone: {
                if (userId !== chatViewPage.threadId) return;
                if (success) {
                    chatViewPage.isBlocked = false;
                    blockedBanner.visible  = false;
                }
            }

            onClearHistoryDone: {
                if (threadId !== chatViewPage.threadId) return;
                if (success) msgModel.clear();
            }

            onLeaveGroupDone: {
                if (groupId !== chatViewPage.threadId) return;
                if (success) chatViewPage.popRequested = true;
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
        },

        SystemDialog {
            id: blockDialog
            title: "Block user"
            body: "Block " + chatViewPage.threadName + "? They won't be able to message you."
            confirmButton.label: "Block"
            cancelButton.label: "Cancel"
            onFinished: {
                if (result === SystemUiResult.ConfirmButtonSelection)
                    zService.blockUser(chatViewPage.threadId);
            }
        },

        SystemDialog {
            id: clearHistoryDialog
            title: "Clear history"
            body: "Delete all messages in this conversation? This only removes them for you."
            confirmButton.label: "Clear"
            cancelButton.label: "Cancel"
            onFinished: {
                if (result === SystemUiResult.ConfirmButtonSelection)
                    zService.clearHistory(chatViewPage.threadId, chatViewPage.isGroup);
            }
        },

        SystemDialog {
            id: leaveGroupDialog
            title: "Leave group"
            body: "Leave " + chatViewPage.threadName + "? You won't be able to receive messages from this group."
            confirmButton.label: "Leave"
            cancelButton.label: "Cancel"
            onFinished: {
                if (result === SystemUiResult.ConfirmButtonSelection)
                    zService.leaveGroup(chatViewPage.threadId);
            }
        },

        SystemDialog {
            id: timedMsgDialog
            title: "Timed Messages"
            body: "This feature is still under development."
            confirmButton.label: "OK"
            cancelButton.label: ""
            cancelButton.enabled: false
        }
    ]
}
