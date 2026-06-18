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
    property bool   showRecalledMessages: app.getShowRecalledMessages()

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
                    preferredWidth: titleBarLUH.layoutFrame.height > 0 ? titleBarLUH.layoutFrame.height : ui.du(7)
                    minWidth:       titleBarLUH.layoutFrame.height > 0 ? titleBarLUH.layoutFrame.height : ui.du(7)

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
                            text: chatViewPage.threadName.length > 0 ? chatViewPage.threadName.charAt(0).toUpperCase() : "?"
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
                    preferredWidth: ui.du(8); preferredHeight: ui.du(8)
                    defaultImageSource: "asset:///images/ChatView/ic_bbm_voice_answer.png"
                    pressedImageSource: "asset:///images/ChatView/ic_bbm_voice_answer.png"
                    rightMargin: ui.du(0.8)
                    onClicked: { voiceCallUnderDevDialog.show() }
                }
                ImageButton {
                    verticalAlignment: VerticalAlignment.Center
                    preferredWidth: ui.du(8); preferredHeight: ui.du(8)
                    defaultImageSource: "asset:///images/ChatView/ic_bbm_video_answer.png"
                    pressedImageSource: "asset:///images/ChatView/ic_bbm_video_answer.png"
                    onClicked: { videoCallUnderDevDialog.show() }
                }
                // Fixed-width spacer — physically pushes the icon pair away from the
                // screen's right edge. (A plain rightMargin on the last icon wasn't
                // enough: the spaceQuota title Container reclaims that space first.)
                Container {
                    preferredWidth: ui.du(0.2)
                }
            }
        }
    }

    function applyImageUpdate(msgId, localPath, imgWidth, imgHeight) {
        var size = msgModel.size();
        if (size === 0) return;
        for (var j = 0; j < size; j++) {
            var d = msgModel.value(j);
            if ((d.msgId || "") === msgId) {
                d.localImage = localPath;
                if (imgWidth  > 0) d.imgWidth  = imgWidth;
                if (imgHeight > 0) d.imgHeight = imgHeight;
                msgModel.replace(j, d);
                return;
            }
        }
    }

    function applyRecall(msgId) {
        var size = msgModel.size();
        for (var j = 0; j < size; j++) {
            var d = msgModel.value(j);
            if ((d.msgId || "") === msgId) {
                // Preserve the original text/photo content so it can still be shown
                // (with a "(This message was recalled)" tag) when the user has
                // "Show Recalled Messages" enabled in Settings. msgType=99 still
                // marks the message as recalled for everything else that checks it.
                //
                // Idempotency guard: the server can redeliver the same "chat.undo"
                // event again (e.g. a resync after reopening the thread replays
                // both the original message and its recall). If this message was
                // already recalled once, d.content is already "" — without this
                // guard, a second call would overwrite the text we already saved
                // here with that empty string and the bubble would permanently
                // lose its recovered text. Only capture it the first time.
                if (!d.recalledOriginalContent || d.recalledOriginalContent.length === 0) {
                    d.recalledOriginalContent = d.content || "";
                }
                d.content    = "";
                d.msgType    = 99;
                d.localImage = "";
                msgModel.replace(j, d);
                return;
            }
        }
    }

    function flushPendingImages() {
        var pending = chatViewPage.pendingImageUpdates;
        if (!pending || pending.length === 0) return;
        for (var i = 0; i < pending.length; i++) {
            chatViewPage.applyImageUpdate(pending[i].msgId, pending[i].localPath,
                                           pending[i].imgWidth, pending[i].imgHeight);
        }
        chatViewPage.pendingImageUpdates = [];
    }

    function startChat() {
        if (chatViewPage.initialized) return;
        if (chatViewPage.threadId === "") return;
        chatViewPage.pageVisible = false;
        chatViewPage.pendingImageUpdates = [];
        if (chatViewPage.selfName === "") chatViewPage.selfName = "Me";
        chatViewPage.initialized = true;

        chatViewPage.isBlocked = zService.isBlocked(chatViewPage.threadId);
        chatViewPage.isMuted   = zService.isMutedThread(chatViewPage.threadId);
        blockedBanner.visible  = chatViewPage.isBlocked;

        msgModel.clear();
        zService.setActiveThread(chatViewPage.threadId, chatViewPage.isGroup);

        var cached = zService.dbLoadMessages(chatViewPage.threadId);
        if (cached && cached.length > 0) {
            var newCache = {};
            for (var i = 0; i < cached.length; i++) {
                var c = cached[i];
                c.selfName = chatViewPage.selfName || "Me";
                c.isMine = (c.isMine === "true" || c.isMine === 1 || c.isMine === true);
                if (c.msgId) newCache[c.msgId] = c.isMine;
                msgModel.append(c);

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
        chatViewPage.dbIsMineCache = {};
    }

    content: Container {
        layout: StackLayout { orientation: LayoutOrientation.TopToBottom }
        horizontalAlignment: HorizontalAlignment.Fill
        verticalAlignment:   VerticalAlignment.Fill
        background: chatViewPage.isDark ? Color.create("#1a1a1a") : Color.create("#d6d6d6")

        ListView {
            id: msgList
            property bool isDark: chatViewPage.isDark
            property bool showRecalledMessages: chatViewPage.showRecalledMessages
            horizontalAlignment: HorizontalAlignment.Fill
            layoutProperties: StackLayoutProperties { spaceQuota: 1 }
            dataModel: ArrayDataModel { id: msgModel }
            bottomPadding: ui.du(1.5)
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
                        layout: StackLayout { orientation: LayoutOrientation.LeftToRight }

                        property bool mine: (ListItemData.isMine === true
                                             || ListItemData.isMine === "true"
                                             || ListItemData.isMine === 1)
                        property bool grouped: ListItemData.grouped === true
                        property bool recalled: (ListItemData.msgType === 99 || ListItemData.msgType === "99")
                        // "Show Recalled Messages" setting: when on, and the recalled message's
                        // original content was plain text (not a photo/sticker JSON blob), keep
                        // showing that original text instead of the generic placeholder banner.
                        property bool showRecalledSetting: ListItem.view.showRecalledMessages
                        property string recalledOriginal: ListItemData.recalledOriginalContent || ""
                        property bool recalledHasOriginalText: rowRoot.recalledOriginal.length > 0
                                                                 && !(rowRoot.recalledOriginal.charAt(0) === "{"
                                                                      && (rowRoot.recalledOriginal.indexOf("normalUrl") >= 0
                                                                          || rowRoot.recalledOriginal.indexOf("thumbUrl") >= 0
                                                                          || rowRoot.recalledOriginal.indexOf("thumb") >= 0
                                                                          || rowRoot.recalledOriginal.indexOf("href") >= 0))
                        // True when we should fall back to the plain "This message was
                        // recalled" placeholder bubble (setting off, or no recoverable text).
                        property bool recalledHidden: rowRoot.recalled
                                                       && !(rowRoot.showRecalledSetting && rowRoot.recalledHasOriginalText)

                        // Used to size photo bubbles to the image's real aspect ratio
                        // without ever exceeding the bubble's own width. 94 = the two
                        // side spacer Containers below (6+60) + bubble left/right padding (14+14).
                        attachedObjects: [ LayoutUpdateHandler { id: rowLUH } ]
                        property real bubbleMaxW: rowLUH.layoutFrame.width > 94
                                                   ? (rowLUH.layoutFrame.width - 94)
                                                   : ui.du(40)

                        Container {
                            preferredWidth: rowRoot.mine ? 6 : 60
                            minWidth:       rowRoot.mine ? 6 : 60
                            maxWidth:       rowRoot.mine ? 6 : 60
                        }

                        Container {
                            layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                            background: rowRoot.isDark ? Color.create("#2a2a2a") : Color.White
                            topPadding:    rowRoot.grouped ? 6 : 10
                            bottomPadding: 10
                            leftPadding:   14
                            rightPadding:  14

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

                            Container {
                                id: msgContentRoot
                                topMargin: 0; bottomMargin: 0

                                Label {
                                    visible: rowRoot.recalledHidden
                                    text: rowRoot.mine ? "You recalled a message" : "This message was recalled"
                                    textStyle {
                                        base:       SystemDefaults.TextStyles.BodyText
                                        fontStyle:  FontStyle.Italic
                                        color: rowRoot.isDark ? Color.create("#888888") : Color.create("#999999")
                                    }
                                    topMargin: 0; bottomMargin: 0
                                }

                                Container {
                                    visible: rowRoot.recalled && !rowRoot.recalledHidden
                                    topMargin: 0; bottomMargin: 0

                                    Label {
                                        text: rowRoot.recalledOriginal
                                        textStyle {
                                            base:  SystemDefaults.TextStyles.BodyText
                                            color: rowRoot.isDark ? Color.create("#eeeeee") : Color.create("#111111")
                                        }
                                        multiline: true
                                        topMargin: 0; bottomMargin: 0
                                    }
                                    Label {
                                        text: "(This message was recalled)"
                                        textStyle {
                                            base:       SystemDefaults.TextStyles.SmallText
                                            fontStyle:  FontStyle.Italic
                                            color: rowRoot.isDark ? Color.create("#888888") : Color.create("#999999")
                                        }
                                        topMargin: 2; bottomMargin: 0
                                    }
                                }

                                Label {
                                    visible: !rowRoot.recalled
                                             && (ListItemData.msgType !== 2 && ListItemData.msgType !== "2")
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

                                Container {
                                    id: photoWrap
                                    visible: !rowRoot.recalled
                                             && ((ListItemData.msgType === 2 || ListItemData.msgType === "2")
                                                 || (typeof ListItemData.content === "string"
                                                     && ListItemData.content.length > 1
                                                     && ListItemData.content.charAt(0) === "{"
                                                     && (ListItemData.content.indexOf("normalUrl") >= 0
                                                         || ListItemData.content.indexOf("thumbUrl") >= 0
                                                         || ListItemData.content.indexOf("thumb") >= 0
                                                         || ListItemData.content.indexOf("href") >= 0)))
                                    horizontalAlignment: HorizontalAlignment.Left
                                    topMargin: 2; bottomMargin: 2

                                    property bool hasLocal: !!(ListItemData.localImage && ListItemData.localImage !== "")
                                    // Real pixel size of the photo (0 when not known yet, e.g. legacy
                                    // cached messages from before this feature existed).
                                    property real natW: (ListItemData.imgWidth  && ListItemData.imgWidth  > 0) ? ListItemData.imgWidth  : 0
                                    property real natH: (ListItemData.imgHeight && ListItemData.imgHeight > 0) ? ListItemData.imgHeight : 0
                                    // Width never exceeds the bubble's content width. Height follows the
                                    // photo's own ratio (1:1, 4:3, 3:4, 16:9, ...) — never cropped/stretched.
                                    property real dispW: photoWrap.natW > 0
                                                          ? Math.min(rowRoot.bubbleMaxW, photoWrap.natW)
                                                          : Math.min(rowRoot.bubbleMaxW, ui.du(30))
                                    property real dispH: photoWrap.natW > 0
                                                          ? (photoWrap.dispW * (photoWrap.natH / photoWrap.natW))
                                                          : ui.du(30)

                                    preferredWidth:  photoWrap.hasLocal ? photoWrap.dispW : ui.du(30)
                                    preferredHeight: photoWrap.hasLocal ? photoWrap.dispH : ui.du(5)
                                    minHeight:       photoWrap.hasLocal ? ui.du(8)        : ui.du(4)
                                    background: photoWrap.hasLocal
                                                ? (rowRoot.isDark ? Color.create("#3a3a3a") : Color.create("#e0e0e0"))
                                                : Color.Transparent
                                    layout: DockLayout {}
                                    ImageView {
                                        visible: photoWrap.hasLocal
                                        horizontalAlignment: HorizontalAlignment.Fill
                                        verticalAlignment:   VerticalAlignment.Fill
                                        scalingMethod: ScalingMethod.AspectFit
                                        imageSource: ListItemData.localImage
                                    }
                                    Label {
                                        visible: !photoWrap.hasLocal
                                        text: "[Photo]"
                                        horizontalAlignment: HorizontalAlignment.Left
                                        verticalAlignment:   VerticalAlignment.Center
                                        textStyle {
                                            color: rowRoot.isDark ? Color.create("#7ab3f5") : Color.create("#1a73e8")
                                            fontSize: FontSize.Small
                                            fontStyle: FontStyle.Italic
                                        }
                                    }
                                }
                            }
                        }

                        Container {
                            preferredWidth: rowRoot.mine ? 60 : 6
                            minWidth:       rowRoot.mine ? 60 : 6
                            maxWidth:       rowRoot.mine ? 60 : 6
                        }
                    }
                }
            ]
        }

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
                textStyle { color: Color.White; fontSize: FontSize.Small }
                multiline: true
                horizontalAlignment: HorizontalAlignment.Fill
            }
        }

        Container {
            horizontalAlignment: HorizontalAlignment.Fill
            background: chatViewPage.isDark ? Color.create("#272727") : Color.White
            topPadding:    ui.du(1.2)
            bottomPadding: ui.du(1.2)
            leftPadding:   ui.du(1.0)
            rightPadding:  ui.du(1.0)
            layout: StackLayout { orientation: LayoutOrientation.LeftToRight }

            ImageButton {
                verticalAlignment: VerticalAlignment.Center
                preferredWidth:  ui.du(8); preferredHeight: ui.du(8)
                rightMargin: ui.du(0.8)
                defaultImageSource: chatViewPage.isDark ? "asset:///images/ChatView/attach_icon.png" : "asset:///images/ChatView/ic_attach.png"
                pressedImageSource: chatViewPage.isDark ? "asset:///images/ChatView/attach_icon.png" : "asset:///images/ChatView/ic_attach.png"
                onClicked: { filePicker.open() }
            }

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
                // Without this, Cascades was defaulting initial focus/highlight to the
                // first focusable control in the title bar (the voice-call button)
                // whenever this page is pushed from ChatsTab, instead of the message
                // input. Requesting focus here (the control's own onCreationCompleted,
                // not the Page's) matches BlackBerry's own documented Cascades sample
                // pattern for focusing a text field as soon as a page loads.
                onCreationCompleted: {
                    requestFocus();
                }
            }

            ImageButton {
                verticalAlignment: VerticalAlignment.Center
                preferredWidth:  ui.du(11); preferredHeight: ui.du(9)
                leftMargin: ui.du(0.6)
                defaultImageSource: chatViewPage.isDark ? "asset:///images/ChatView/timemesswhite.png" : "asset:///images/ChatView/timemess.png"
                pressedImageSource: chatViewPage.isDark ? "asset:///images/ChatView/timemesswhite.png" : "asset:///images/ChatView/timemess.png"
                onClicked: { timedMsgDialog.show() }
            }

            ImageButton {
                id: emoticonBtn
                verticalAlignment: VerticalAlignment.Center
                preferredWidth:  ui.du(8); preferredHeight: ui.du(8)
                leftMargin: ui.du(0.5)
                defaultImageSource: emojiPanelOpen
                    ? (chatViewPage.isDark ? "asset:///images/ChatView/ic_keyboard_enabled.png" : "asset:///images/ChatView/darkkeyboard.png")
                    : (chatViewPage.isDark ? "asset:///images/ChatView/ic_emoticon_enabled_white.png" : "asset:///images/ChatView/ic_emoticon_enabled.png")
                pressedImageSource: emojiPanelOpen
                    ? (chatViewPage.isDark ? "asset:///images/ChatView/ic_keyboard_enabled.png" : "asset:///images/ChatView/darkkeyboard.png")
                    : (chatViewPage.isDark ? "asset:///images/ChatView/ic_emoticon_enabled_white.png" : "asset:///images/ChatView/ic_emoticon_enabled.png")
                onClicked: {
                    emojiPanelOpen = !emojiPanelOpen;
                    emojiPanel.visible = emojiPanelOpen;
                }
            }
        }

    }

    actions: [
        ActionItem {
            id: sendAction
            title: "Send"
            imageSource: "asset:///images/ChatView/ConversationPaneSend.png"
            ActionBar.placement: ActionBarPlacement.OnBar
            enabled: false
            onTriggered: { doSend() }
        },
        ActionItem {
            id: muteAction
            title: chatViewPage.isMuted ? "Unmute" : "Mute notifications"
            imageSource: "asset:///images/ChatView/ic_notifications_off.png"
            ActionBar.placement: ActionBarPlacement.InOverflow
            onTriggered: {
                zService.setMute(chatViewPage.threadId, chatViewPage.isGroup, !chatViewPage.isMuted);
            }
        },
        ActionItem {
            title: chatViewPage.isBlocked ? "Unblock user" : "Block user"
            imageSource: "asset:///images/ChatView/ic_block_contact.png"
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
            imageSource: "asset:///images/ChatView/clear_chat.png"
            ActionBar.placement: ActionBarPlacement.InOverflow
            onTriggered: { clearHistoryDialog.show() }
        },
        ActionItem {
            title: "Leave group"
            imageSource: "asset:///images/ChatView/ic_chat_leave.png"
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

    function normMine(v) {
        if (v === true  || v === 1)  return true;
        if (v === false || v === 0)  return false;
        if (typeof v === "string")   return (v === "true" || v === "1");
        return false;
    }

    function extractPhotoUrl(content) {
        if (typeof content !== "string" || content.length === 0) return "";
        if (content.charAt(0) === "{") {
            var keys = ["thumbUrl", "normalUrl", "hdUrl", "href", "thumb", "oriUrl"];
            for (var k = 0; k < keys.length; k++) {
                var re = new RegExp("\"" + keys[k] + "\"\\s*:\\s*\"([^\"]+)\"");
                var m = content.match(re);
                if (m && m[1] && m[1].length > 0) return m[1];
            }
        }
        if (content.indexOf("http") === 0) return content;
        return "";
    }

    function rebuildGroups() {
        var size = msgModel.size();
        if (size === 0) return;

        var items = [];
        for (var i = 0; i < size; i++) {
            items.push(msgModel.value(i));
        }

        for (var i = 0; i < size; i++) {
            var cur  = items[i];
            var prev = (i > 0)        ? items[i - 1] : null;
            var next = (i < size - 1) ? items[i + 1] : null;

            var curMine  = chatViewPage.normMine(cur.isMine);
            var prevMine = prev ? chatViewPage.normMine(prev.isMine) : !curMine;
            var nextMine = next ? chatViewPage.normMine(next.isMine) : !curMine;

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

        for (var i = 0; i < size; i++) {
            msgModel.replace(i, items[i]);
        }
    }

    function buildExistingIds() {
        var ids = {};
        for (var i = 0; i < msgModel.size(); i++) {
            var mid = msgModel.value(i).msgId;
            if (mid && mid.indexOf("local_") !== 0)
                ids[mid] = true;
        }
        return ids;
    }

    function removeLocalPlaceholder(content) {
        for (var k = msgModel.size() - 1; k >= 0; k--) {
            var item = msgModel.value(k);
            if (item.msgId && item.msgId.indexOf("local_") === 0 && item.content === content) {
                msgModel.removeAt(k);
                return;
            }
        }
    }

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
        SystemDialog {
            id: voiceCallUnderDevDialog
            title: "Voice Call"
            body: "This feature is still under development."
            confirmButton.label: "OK"
            cancelButton.label: ""
            cancelButton.enabled: false
        },

        SystemDialog {
            id: videoCallUnderDevDialog
            title: "Video Call"
            body: "This feature is still under development."
            confirmButton.label: "OK"
            cancelButton.label: ""
            cancelButton.enabled: false
        },

        Connections {
            target: app

            onShowRecalledMessagesChanged: {
                chatViewPage.showRecalledMessages = show;
            }
        },

        Connections {
            target: zService

            onMessagesReady: {
                if (threadId !== chatViewPage.threadId) return;

                var existing = chatViewPage.buildExistingIds();
                var added = false;

                for (var j = 0; j < messages.length; j++) {
                    var msg = messages[j];
                    if (existing[msg.msgId]) continue;

                    if (chatViewPage.normMine(msg.isMine)) {
                        chatViewPage.removeLocalPlaceholder(msg.content);
                    }

                    var nm = msg;
                    nm.selfName = chatViewPage.selfName || "Me";
                    var rawMine = (nm.isMine === true || nm.isMine === 1 || nm.isMine === "true" || nm.isMine === "1");
                    var cachedMine = chatViewPage.dbIsMineCache[nm.msgId];
                    nm.isMine = (cachedMine !== undefined) ? cachedMine : rawMine;
                    if (nm.msgId) {
                        var upd = chatViewPage.dbIsMineCache;
                        upd[nm.msgId] = nm.isMine;
                        chatViewPage.dbIsMineCache = upd;
                    }
                    msgModel.append(nm);
                    added = true;

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
                    chatViewPage.removeLocalPlaceholder(chatViewPage.pendingMsg);
                    inputField.text = chatViewPage.pendingMsg;
                    for (var ri = msgModel.size() - 1; ri >= 0; ri--) {
                        var ritem = msgModel.value(ri);
                        if (ritem.msgId && (ritem.msgId.indexOf("local_img_") === 0
                                         || ritem.msgId.indexOf("local_file_") === 0)) {
                            msgModel.removeAt(ri);
                            break;
                        }
                    }
                    chatViewPage.rebuildGroups();
                } else {
                    chatViewPage.removeLocalPlaceholder(chatViewPage.pendingMsg);
                }
                chatViewPage.pendingMsg = "";
                sendAction.enabled = true;
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

                var handledInPlace = false;

                if (chatViewPage.normMine(msg.isMine)) {
                    if (msg.msgType === 2 || msg.msgType === "2") {
                        var savedLocalImage = "";
                        var savedImgWidth = 0, savedImgHeight = 0;
                        var placeholderIdx = -1;
                        for (var pi = msgModel.size() - 1; pi >= 0; pi--) {
                            var pitem = msgModel.value(pi);
                            if (pitem.msgId && pitem.msgId.indexOf("local_img_") === 0) {
                                savedLocalImage = pitem.localImage || "";
                                savedImgWidth  = pitem.imgWidth  || 0;
                                savedImgHeight = pitem.imgHeight || 0;
                                placeholderIdx = pi;
                                break;
                            }
                        }
                        if (savedLocalImage.length > 0) msg.localImage = savedLocalImage;
                        if (!msg.imgWidth  && savedImgWidth  > 0) msg.imgWidth  = savedImgWidth;
                        if (!msg.imgHeight && savedImgHeight > 0) msg.imgHeight = savedImgHeight;

                        if (placeholderIdx >= 0) {
                            // Replace the placeholder row in place (same index) instead of
                            // remove+append — avoids a brief duplicate-looking flicker in the
                            // ListView when the server confirmation (HTTP + WS can both fire
                            // close together) lands while the row is still animating in.
                            msgModel.replace(placeholderIdx, msg);
                            handledInPlace = true;
                        } else if (savedLocalImage.length === 0 && msg.localImage && msg.localImage.length > 0) {
                            // Placeholder already consumed by an earlier confirmation for this
                            // same photo (e.g. HTTP confirm + WS echo both arriving) — if we
                            // already have a row with this exact image, update it instead of
                            // adding a second one, even if msgId happened to differ between
                            // the two confirmations.
                            for (var li = msgModel.size() - 1; li >= 0; li--) {
                                if (msgModel.value(li).localImage === msg.localImage) {
                                    msgModel.replace(li, msg);
                                    handledInPlace = true;
                                    break;
                                }
                            }
                        }
                    } else {
                        chatViewPage.removeLocalPlaceholder(msg.content);
                    }
                }

                if (!handledInPlace) {
                    for (var di = 0; di < msgModel.size(); di++) {
                        if (msgModel.value(di).msgId === msg.msgId) return;
                    }
                    msgModel.append(msg);
                }

                chatViewPage.rebuildGroups();
                msgList.scrollToPosition(ScrollPosition.End, ScrollAnimation.Smooth);

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
                    chatViewPage.applyImageUpdate(msgId, localPath, width, height);
                } else {
                    var pending = chatViewPage.pendingImageUpdates;
                    pending.push({ msgId: msgId, localPath: localPath, imgWidth: width, imgHeight: height });
                    chatViewPage.pendingImageUpdates = pending;
                }
            }

            onMessageRecalled: {
                if (threadId !== chatViewPage.threadId) return;
                chatViewPage.applyRecall(msgId);
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

        FilePicker {
            id: filePicker
            type: FileType.Other
            mode: FilePickerMode.Picker
            title: "Select File"
            onFileSelected: {
                var path = selectedFiles[0];
                chatViewPage.pendingAttachPath = path;

                var ext = path.substring(path.lastIndexOf('.') + 1).toLowerCase();
                var isImg = (ext === "jpg" || ext === "jpeg" || ext === "png"
                             || ext === "gif" || ext === "webp" || ext === "bmp");

                if (isImg) {
                    var dim = zService.getImageDimensions(path);
                    var m = {
                        msgId:      "local_img_" + new Date().getTime(),
                        content:    "",
                        msgType:    2,
                        localImage: "file://" + path,
                        imgWidth:   dim.width  || 0,
                        imgHeight:  dim.height || 0,
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
