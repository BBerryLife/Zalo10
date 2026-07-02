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
    property variant emojiPanelRef: null
    property string pendingAttachPath: ""
    property string pendingAttachName: ""
    property variant dbIsMineCache: ({})
    property bool   isMuted: false
    property bool   isBlocked: false
    property bool   popRequested: false
    property bool   qmRequested: false
    property variant pendingImageUpdates: ([])
    property bool   pageVisible: false
    property bool   isDark: app.getDarkTheme()
    property bool   showRecalledMessages: app.getShowRecalledMessages()
    property bool   searchVisible: false
    property string searchText: ""
    property variant searchMatches: []   // indices into msgModel that contain the current query
    property int      searchMatchPos: -1 // which entry in searchMatches is currently focused

    titleBar: TitleBar {
        // Sticky keeps the title bar (and the chat header inside it) pinned and
        // visible while the message list scrolls. Trade-off: once the user has
        // scrolled into history, Sticky can intercept touch input meant for
        // controls inside the title bar, such as the search field — NonSticky
        // avoids that at the cost of the bar scrolling away with the list.
        scrollBehavior: TitleBarScrollBehavior.Sticky
        kind: TitleBarKind.FreeForm
        kindProperties: FreeFormTitleBarKindProperties {
            content: Container {
                background: Color.create("#2575fc")
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment:   VerticalAlignment.Fill
                layout: DockLayout {}

                // Normal header: avatar, thread name, call buttons. Hidden while
                // the in-chat message search box (below) is active.
                Container {
                    visible: !chatViewPage.searchVisible
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

                // In-chat message search box: toggled on by the search icon above.
                // Browser-style find: matches are highlighted yellow inline (see
                // rowRoot.searchHtml() in the delegate below) and the list scrolls
                // to each match in turn via the Prev/Next arrows, rather than
                // hiding non-matching messages.
                Container {
                    visible: chatViewPage.searchVisible
                    horizontalAlignment: HorizontalAlignment.Fill
                    verticalAlignment:   VerticalAlignment.Center
                    leftPadding: ui.du(1.5)
                    rightPadding: ui.du(1.5)
                    layout: StackLayout { orientation: LayoutOrientation.LeftToRight }

                    TextField {
                        id: chatSearchField
                        hintText: "Search in this chat..."
                        verticalAlignment: VerticalAlignment.Center
                        textStyle { color: Color.White }
                        layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                        onTextChanging: {
                            chatViewPage.searchText = text;
                            chatViewPage.updateSearchMatches(true);
                        }
                        onCreationCompleted: {
                            inputMode.type = TextInputFlag.AutoCapitalizationOff | TextInputFlag.AutoCorrectionOff | TextInputFlag.SpellCheckOff | TextInputFlag.PredictionOff;
                        }
                    }
                    Label {
                        text: chatViewPage.searchMatches.length > 0
                              ? (chatViewPage.searchMatchPos + 1) + "/" + chatViewPage.searchMatches.length
                              : (chatViewPage.searchText.length > 0 ? "0/0" : "")
                        verticalAlignment: VerticalAlignment.Center
                        rightMargin: ui.du(0.8)
                        textStyle { color: Color.create("#cfe2ff"); fontSize: FontSize.Small }
                    }
                    Button {
                        text: "\u25B2"
                        preferredWidth: ui.du(7)
                        verticalAlignment: VerticalAlignment.Center
                        enabled: chatViewPage.searchMatches.length > 0
                        onClicked: chatViewPage.gotoSearchMatch(-1)
                    }
                    Button {
                        text: "\u25BC"
                        preferredWidth: ui.du(7)
                        verticalAlignment: VerticalAlignment.Center
                        rightMargin: ui.du(0.8)
                        enabled: chatViewPage.searchMatches.length > 0
                        onClicked: chatViewPage.gotoSearchMatch(1)
                    }
                    Button {
                        text: "Cancel"
                        preferredWidth: ui.du(12)
                        verticalAlignment: VerticalAlignment.Center
                        onClicked: {
                            chatSearchField.text = "";
                            chatViewPage.searchText = "";
                            chatViewPage.searchVisible = false;
                            chatViewPage.searchMatches = [];
                            chatViewPage.searchMatchPos = -1;
                        }
                    }
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
                // NOTE: localImage is intentionally left untouched here. It's the
                // path to the already-downloaded photo file on disk; clearing it
                // would make a recalled photo impossible to show again even when
                // "Show Recalled Messages" is enabled. The delegate below decides
                // whether to actually display it based on that setting.
                msgModel.replace(j, d);
                return;
            }
        }
    }

    // In-chat message search ("find in page" style): scans the already-loaded
    // messages for the query and records which indices match, without hiding
    // any messages. The delegate (rowRoot.searchHtml) highlights the matched
    // substring inline; gotoSearchMatch() scrolls the list to each result.
    function updateSearchMatches(resetToFirst) {
        var q = (chatViewPage.searchText || "").toLowerCase().trim();
        var found = [];
        if (q.length > 0) {
            var size = msgModel.size();
            for (var i = 0; i < size; i++) {
                var d = msgModel.value(i);
                var hay = "";
                if (typeof d.content === "string") hay += d.content.toLowerCase();
                if (typeof d.recalledOriginalContent === "string" && d.recalledOriginalContent.charAt(0) !== "{")
                    hay += " " + d.recalledOriginalContent.toLowerCase();
                if (hay.indexOf(q) !== -1) found.push(i);
            }
        }
        chatViewPage.searchMatches = found;
        if (resetToFirst) {
            chatViewPage.searchMatchPos = found.length > 0 ? 0 : -1;
            if (found.length > 0) chatViewPage.scrollToMsgIndex(found[0]);
        } else if (chatViewPage.searchMatchPos >= found.length) {
            chatViewPage.searchMatchPos = found.length > 0 ? 0 : -1;
        }
    }

    // dir: +1 for next match, -1 for previous (wraps around both ends).
    function gotoSearchMatch(dir) {
        var n = chatViewPage.searchMatches.length;
        if (n === 0) return;
        var pos = chatViewPage.searchMatchPos + dir;
        if (pos < 0) pos = n - 1;
        if (pos >= n) pos = 0;
        chatViewPage.searchMatchPos = pos;
        chatViewPage.scrollToMsgIndex(chatViewPage.searchMatches[pos]);
    }

    function scrollToMsgIndex(idx) {
        if (idx < 0 || idx >= msgModel.size()) return;
        msgList.scrollToItem([idx], ScrollAnimation.Default);
    }

    // Escapes HTML-sensitive characters, then wraps every case-insensitive
    // occurrence of `query` in a yellow <span>, preserving the original
    // casing of the matched text. Returns plain (non-html) text unchanged
    // when there's no active query, so callers can use it unconditionally.
    function highlightMatches(text, query, color) {
        var esc = String(text)
            .replace(/&/g, "&amp;")
            .replace(/</g, "&lt;")
            .replace(/>/g, "&gt;");
        var q = (query || "").trim();
        if (q.length === 0) return esc;
        var qEsc = q.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
        var lowerEsc = esc.toLowerCase();
        var lowerQ = qEsc.toLowerCase();
        var hl = color || "#ffeb3b";
        var out = "";
        var pos = 0;
        var idx;
        while ((idx = lowerEsc.indexOf(lowerQ, pos)) !== -1) {
            out += esc.substring(pos, idx);
            out += "<span style='background-color:" + hl + ";color:#000000;'>" + esc.substring(idx, idx + qEsc.length) + "</span>";
            pos = idx + qEsc.length;
        }
        out += esc.substring(pos);
        return out;
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

        // Wire the photo-tap Connections to the ListView now that it exists.
        if (photoTapCon.target === null) photoTapCon.target = msgList;

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
            property bool   isDark: chatViewPage.isDark
            property bool   showRecalledMessages: chatViewPage.showRecalledMessages
            property string searchQuery: chatViewPage.searchVisible ? chatViewPage.searchText.toLowerCase().trim() : ""
            property int    searchCurrentMsgIndex: (chatViewPage.searchMatchPos >= 0 && chatViewPage.searchMatchPos < chatViewPage.searchMatches.length)
                                                     ? chatViewPage.searchMatches[chatViewPage.searchMatchPos] : -1
            // Set by photo attachment tap handler inside the delegate; watched by
            // the Connections block in attachedObjects to open PhotoViewerSheet.
            property string tappedPhotoPath: ""
            horizontalAlignment: HorizontalAlignment.Fill
            layoutProperties: StackLayoutProperties { spaceQuota: 1 }
            dataModel: msgModel
            bottomPadding: ui.du(1.5)
            flickMode: FlickMode.Momentum

            attachedObjects: [
                ArrayDataModel { id: msgModel }
            ]

            listItemComponents: [
                ListItemComponent {
                    type: ""
                    CustomListItem {
                        id: rowRoot
                        highlightAppearance: HighlightAppearance.None
                        dividerVisible: false

                        property bool isDark: ListItem.view.isDark

                        // Browser-style find-in-page: query text and whether this exact
                        // row is the currently-focused match (for a slightly stronger
                        // highlight / outline than other, non-focused matches).
                        property string searchQuery: ListItem.view.searchQuery
                        property bool   isCurrentSearchMatch: rowRoot.searchQuery.length > 0
                                                               && ListItem.view.searchCurrentMsgIndex === ListItem.indexPath[0]

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
                        // Recalled photo/sticker: detected either from the preserved original
                        // content being a photo JSON blob, or simply from a cached local image
                        // file still being on disk for this message (localImage is no longer
                        // cleared on recall — see applyRecall()). Either signal means this was
                        // an image message, so it should be recoverable as a photo, not text.
                        property bool recalledIsPhoto: (rowRoot.recalledOriginal.length > 0
                                                         && rowRoot.recalledOriginal.charAt(0) === "{"
                                                         && (rowRoot.recalledOriginal.indexOf("normalUrl") >= 0
                                                             || rowRoot.recalledOriginal.indexOf("thumbUrl") >= 0
                                                             || rowRoot.recalledOriginal.indexOf("thumb") >= 0
                                                             || rowRoot.recalledOriginal.indexOf("href") >= 0))
                                                        || !!(ListItemData.localImage && ListItemData.localImage !== "")
                        property bool recalledHasOriginalText: rowRoot.recalledOriginal.length > 0 && !rowRoot.recalledIsPhoto
                        // True when we should fall back to the plain "This message was
                        // recalled" placeholder bubble (setting off, or no recoverable
                        // text/photo).
                        property bool recalledHidden: rowRoot.recalled
                                                       && !(rowRoot.showRecalledSetting
                                                            && (rowRoot.recalledHasOriginalText || rowRoot.recalledIsPhoto))

                        // Used to size photo bubbles to the image's real aspect ratio
                        // without ever exceeding the bubble's own width. 94 = the two
                        // side spacer Containers below (6+60) + bubble left/right padding (14+14).
                        property string bubblePos: ListItemData.bubblePos || "full"
                        property string bubbleImage: rowRoot.mine
                            ? ("asset:///images/Bubble/outgoing/" + rowRoot.bubblePos + ".png")
                            : ("asset:///images/Bubble/incoming/" + rowRoot.bubblePos + ".png")
                        property real bubbleMaxW: rowLUH.layoutFrame.width > 94
                                                   ? (rowLUH.layoutFrame.width - 94)
                                                   : ui.du(40)

                        Container {
                            horizontalAlignment: HorizontalAlignment.Fill
                            topPadding:    rowRoot.grouped ? 0 : 6
                            bottomPadding: 0
                            layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                            attachedObjects: [ LayoutUpdateHandler { id: rowLUH } ]

                        Container {
                            preferredWidth: rowRoot.mine ? 10 : 60
                            minWidth:       rowRoot.mine ? 10 : 60
                            maxWidth:       rowRoot.mine ? 10 : 60
                        }

                        Container {
                            id: bubbleWrap
                            layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                            background: rowRoot.isDark
                                ? (rowRoot.mine ? Color.create("#1e3a5f") : Color.create("#2a2a2a"))
                                : Color.White

                            Container {
                                background: Color.Transparent
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
                                    visible: rowRoot.recalled && !rowRoot.recalledHidden && !rowRoot.recalledIsPhoto
                                    topMargin: 0; bottomMargin: 0

                                    Label {
                                        text: rowRoot.searchQuery.length > 0
                                              ? "<html>" + chatViewPage.highlightMatches(rowRoot.recalledOriginal, rowRoot.searchQuery, rowRoot.isCurrentSearchMatch ? "#ff9800" : "#ffeb3b") + "</html>"
                                              : rowRoot.recalledOriginal
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

                                // Recovered photo bubble for a recalled image message, shown only
                                // when "Show Recalled Messages" is on and the cached file is still
                                // on disk (see recalledIsPhoto / applyRecall()).
                                Container {
                                    visible: rowRoot.recalled && !rowRoot.recalledHidden && rowRoot.recalledIsPhoto
                                    topMargin: 0; bottomMargin: 0

                                    ImageView {
                                        visible: !!(ListItemData.localImage && ListItemData.localImage !== "")
                                        horizontalAlignment: HorizontalAlignment.Left
                                        preferredWidth:  (ListItemData.imgWidth  && ListItemData.imgWidth  > 0)
                                                         ? Math.min(rowRoot.bubbleMaxW, ListItemData.imgWidth) : Math.min(rowRoot.bubbleMaxW, ui.du(30))
                                        preferredHeight: (ListItemData.imgWidth  && ListItemData.imgWidth  > 0
                                                          && ListItemData.imgHeight && ListItemData.imgHeight > 0)
                                                         ? (Math.min(rowRoot.bubbleMaxW, ListItemData.imgWidth) * (ListItemData.imgHeight / ListItemData.imgWidth))
                                                         : ui.du(30)
                                        scalingMethod: ScalingMethod.AspectFit
                                        imageSource: ListItemData.localImage
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
                                    text: {
                                        var raw = (typeof ListItemData.content === "string" && ListItemData.content.length > 0)
                                              ? ListItemData.content
                                              : ((ListItemData.msgType === 2 || ListItemData.msgType === "2")
                                                 ? "[Photo]"
                                                 : ((ListItemData.msgType === 6 || ListItemData.msgType === "6")
                                                    ? "[Sticker]" : "[Photo]"));
                                        if (rowRoot.searchQuery.length > 0) {
                                            var hlColor = rowRoot.isCurrentSearchMatch ? "#ff9800" : "#ffeb3b";
                                            return "<html>" + chatViewPage.highlightMatches(raw, rowRoot.searchQuery, hlColor) + "</html>";
                                        }
                                        return raw;
                                    }
                                    textStyle {
                                        base:  SystemDefaults.TextStyles.BodyText
                                        color: rowRoot.isDark ? Color.create("#eeeeee") : Color.create("#111111")
                                    }
                                    multiline: true
                                    topMargin: 0; bottomMargin: 0
                                }

                                // BBM-style photo attachment bubble.
                                // Layout: caption (if any) → dashed separator → thumbnail + file info row.
                                // Tapping the row opens PhotoViewerSheet via msgList.tappedPhotoPath.
                                Container {
                                    id: photoBubble
                                    visible: !rowRoot.recalled
                                             && ((ListItemData.msgType === 2 || ListItemData.msgType === "2")
                                                 || (typeof ListItemData.content === "string"
                                                     && ListItemData.content.length > 1
                                                     && ListItemData.content.charAt(0) === "{"
                                                     && (ListItemData.content.indexOf("normalUrl") >= 0
                                                         || ListItemData.content.indexOf("thumbUrl") >= 0
                                                         || ListItemData.content.indexOf("thumb") >= 0
                                                         || ListItemData.content.indexOf("href") >= 0)))
                                    horizontalAlignment: HorizontalAlignment.Fill
                                    topMargin: 2; bottomMargin: 2

                                    // Caption label — sits above the separator line.
                                    // Only shown when the photo content JSON contains a "caption" key.
                                    Label {
                                        id: photoCaptionLbl
                                        visible: {
                                            var c = ListItemData.content || "";
                                            if (c.length === 0 || c.charCodeAt(0) !== 123) return false;
                                            return c.indexOf('"caption":"') >= 0;
                                        }
                                        text: {
                                            var c = ListItemData.content || "";
                                            if (c.length === 0) return "";
                                            var key = '"caption":"';
                                            var si = c.indexOf(key);
                                            if (si < 0) return "";
                                            si += key.length;
                                            var ei = si;
                                            while (ei < c.length) {
                                                var code = c.charCodeAt(ei);
                                                if (code === 92) { ei += 2; continue; }
                                                if (code === 34) break;
                                                ei++;
                                            }
                                            return c.substring(si, ei);
                                        }
                                        textStyle {
                                            base:  SystemDefaults.TextStyles.BodyText
                                            color: rowRoot.isDark ? Color.create("#eeeeee") : Color.create("#111111")
                                        }
                                        multiline: true
                                        topMargin: 0; bottomMargin: 4
                                    }

                                    // Separator line — only present when caption is showing.
                                    Container {
                                        visible: photoCaptionLbl.visible
                                        horizontalAlignment: HorizontalAlignment.Fill
                                        preferredHeight: 1
                                        background: rowRoot.isDark ? Color.create("#555555") : Color.create("#cccccc")
                                        bottomMargin: 6
                                    }

                                    // Tappable row: thumbnail on the left, file info on the right.
                                    Container {
                                        layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                                        horizontalAlignment: HorizontalAlignment.Fill
                                        gestureHandlers: [
                                            TapHandler {
                                                onTapped: {
                                                    var lp = ListItemData.localImage || "";
                                                    if (lp.length > 0) {
                                                        rowRoot.ListItem.view.tappedPhotoPath = lp;
                                                    }
                                                }
                                            }
                                        ]

                                        // Square thumbnail (~100 × 100 dp).
                                        Container {
                                            preferredWidth:  ui.du(18)
                                            preferredHeight: ui.du(18)
                                            minWidth:        ui.du(18)
                                            minHeight:       ui.du(18)
                                            maxWidth:        ui.du(18)
                                            maxHeight:       ui.du(18)
                                            background: rowRoot.isDark ? Color.create("#3a3a3a") : Color.create("#e0e0e0")
                                            layout: DockLayout {}

                                            ImageView {
                                                visible: !!(ListItemData.localImage && ListItemData.localImage !== "")
                                                horizontalAlignment: HorizontalAlignment.Fill
                                                verticalAlignment:   VerticalAlignment.Fill
                                                scalingMethod: ScalingMethod.AspectFill
                                                imageSource: ListItemData.localImage || ""
                                            }
                                            Label {
                                                visible: !(ListItemData.localImage && ListItemData.localImage !== "")
                                                text: "..."
                                                horizontalAlignment: HorizontalAlignment.Center
                                                verticalAlignment:   VerticalAlignment.Center
                                                textStyle {
                                                    fontSize: FontSize.XSmall
                                                    color: rowRoot.isDark ? Color.create("#888888") : Color.Gray
                                                }
                                            }
                                        }

                                        // File info column: filename, size, status.
                                        Container {
                                            layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                                            layout: StackLayout { orientation: LayoutOrientation.TopToBottom }
                                            verticalAlignment: VerticalAlignment.Center
                                            leftPadding: ui.du(1.5)

                                            // Filename: prefer the real fileName saved in content JSON
                                            // (self-sent photos: exact original name; received photos with
                                            // a server-provided name), then fall back to deriving one from
                                            // the CDN URL, then the local cache path, then "Photo".
                                            Label {
                                                text: {
                                                    var c = ListItemData.content || "";
                                                    if (c.length > 0 && c.charCodeAt(0) === 123) {
                                                        var fnKey = '"fileName":"';
                                                        var fnIdx = c.indexOf(fnKey);
                                                        if (fnIdx >= 0) {
                                                            var fs = fnIdx + fnKey.length;
                                                            var fe = fs;
                                                            while (fe < c.length) {
                                                                var fc = c.charCodeAt(fe);
                                                                if (fc === 92) { fe += 2; continue; }
                                                                if (fc === 34) break;
                                                                fe++;
                                                            }
                                                            var realName = c.substring(fs, fe).replace(/\\"/g, '"').replace(/\\\\/g, '\\');
                                                            if (realName.length > 0) return realName;
                                                        }
                                                        var urlKeys = ['"normalUrl":"', '"hdUrl":"', '"thumbUrl":"'];
                                                        for (var ki = 0; ki < urlKeys.length; ki++) {
                                                            var uIdx = c.indexOf(urlKeys[ki]);
                                                            if (uIdx < 0) continue;
                                                            var us = uIdx + urlKeys[ki].length;
                                                            var ue = us;
                                                            while (ue < c.length && c.charCodeAt(ue) !== 34) ue++;
                                                            if (ue > us) {
                                                                var url = c.substring(us, ue);
                                                                var sl = url.lastIndexOf('/');
                                                                var fn = sl >= 0 ? url.substring(sl + 1) : url;
                                                                var qi = fn.indexOf('?');
                                                                if (qi >= 0) fn = fn.substring(0, qi);
                                                                if (fn.length > 0) return fn;
                                                            }
                                                        }
                                                    }
                                                    var limg = ListItemData.localImage || "";
                                                    if (limg.length > 0) {
                                                        var lp2 = limg.indexOf("file://") === 0 ? limg.substring(7) : limg;
                                                        var ls = lp2.lastIndexOf('/');
                                                        var lf = ls >= 0 ? lp2.substring(ls + 1) : lp2;
                                                        if (lf.length > 0) return lf;
                                                    }
                                                    return "Photo";
                                                }
                                                textStyle {
                                                    fontSize: FontSize.Small
                                                    fontWeight: FontWeight.Bold
                                                    color: rowRoot.isDark ? Color.create("#eeeeee") : Color.create("#111111")
                                                }
                                                multiline: false
                                                topMargin: 0; bottomMargin: 0
                                            }

                                            // File size — hidden when the JSON has no fileSize field.
                                            Label {
                                                visible: {
                                                    var c = ListItemData.content || "";
                                                    if (c.length === 0 || c.charCodeAt(0) !== 123) return false;
                                                    return c.indexOf('"fileSize":') >= 0;
                                                }
                                                text: {
                                                    var c = ListItemData.content || "";
                                                    var key2 = '"fileSize":';
                                                    var fsi = c.indexOf(key2);
                                                    if (fsi < 0) return "";
                                                    var fss = fsi + key2.length;
                                                    var fse = fss;
                                                    while (fse < c.length) {
                                                        var dc = c.charCodeAt(fse);
                                                        if (dc < 48 || dc > 57) break;
                                                        fse++;
                                                    }
                                                    var fsize = parseInt(c.substring(fss, fse)) || 0;
                                                    if (fsize <= 0) return "";
                                                    if (fsize >= 1073741824) return (Math.round(fsize / 1073741824 * 10) / 10) + " GB";
                                                    if (fsize >= 1048576) return (Math.round(fsize / 1048576 * 10) / 10) + " MB";
                                                    return (Math.round(fsize / 1024 * 10) / 10) + " KB";
                                                }
                                                textStyle {
                                                    fontSize: FontSize.XSmall
                                                    color: rowRoot.isDark ? Color.create("#aaaaaa") : Color.create("#666666")
                                                }
                                                topMargin: 2; bottomMargin: 0
                                            }

                                            // Status text: gray, shows "Sending..." for unconfirmed outgoing.
                                            Label {
                                                text: {
                                                    var mid = ListItemData.msgId || "";
                                                    if (rowRoot.mine && mid.indexOf("local_img_") === 0) return "Sending...";
                                                    return rowRoot.mine ? "Picture sent" : "Photo";
                                                }
                                                textStyle {
                                                    fontSize: FontSize.XSmall
                                                    fontStyle: FontStyle.Italic
                                                    color: Color.create("#888888")
                                                }
                                                topMargin: 2; bottomMargin: 0
                                            }
                                        }
                                    }
                                }
                            }
                        } // bubble content Container

                            // Accent strip along the bottom edge of the bubble —
                            // gray for my own messages, blue for incoming ones.
                            Container {
                                horizontalAlignment: HorizontalAlignment.Fill
                                preferredHeight: ui.du(0.8)
                                background: rowRoot.mine ? Color.create("#999999") : Color.create("#0073BC")
                            }
                        } // bubbleWrap

                        Container {
                            preferredWidth: rowRoot.mine ? 60 : 10
                            minWidth:       rowRoot.mine ? 60 : 10
                            maxWidth:       rowRoot.mine ? 60 : 10
                        }
                        } // inner row Container
                    }
                }
            ]
        }

        // EmojiPanel is created lazily on first open to avoid its ~800ms
        // object-creation cost delaying every chat tap. The slot container
        // is always in the layout so the panel snaps into the correct position.
        Container {
            id: emojiPanelSlot
            horizontalAlignment: HorizontalAlignment.Fill
            visible: false
        }

        Container {
            id: qmSuggestBar
            visible: false
            horizontalAlignment: HorizontalAlignment.Fill
            background: chatViewPage.isDark ? Color.create("#1c1c1c") : Color.White
            layout: StackLayout {}

            Divider {}

            ListView {
                id: qmSuggestList
                horizontalAlignment: HorizontalAlignment.Fill
                preferredHeight: ui.du(34)
                maxHeight: ui.du(34)

                dataModel: ArrayDataModel { id: qmSuggestModel }

                listItemComponents: [
                    ListItemComponent {
                        CustomListItem {
                            id: qmSugRow
                            highlightAppearance: HighlightAppearance.Full
                            dividerVisible: true

                            Container {
                                layout: StackLayout { orientation: LayoutOrientation.TopToBottom }
                                horizontalAlignment: HorizontalAlignment.Fill
                                background: chatViewPage.isDark ? Color.create("#13335c") : Color.create("#eaf1ff")
                                leftPadding: ui.du(2); rightPadding: ui.du(2)
                                topPadding: ui.du(1); bottomPadding: ui.du(1)

                                Label {
                                    text: "/" + ListItemData.name
                                    textStyle { color: Color.create("#2575fc"); fontWeight: FontWeight.Bold }
                                    multiline: false
                                }
                                Label {
                                    text: ListItemData.content
                                    textStyle {
                                        color: chatViewPage.isDark ? Color.create("#cfd8e3") : Color.create("#444444")
                                        fontSize: FontSize.Small
                                    }
                                    multiline: false
                                    topMargin: ui.du(0.2)
                                }
                            }
                        }
                    }
                ]

                onTriggered: {
                    var item = dataModel.data(indexPath);
                    if (item === null || item === undefined) return;
                    chatViewPage.applyQuickMessage(item.content);
                }
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

        // Attachment preview bar — shown when an image is staged for sending.
        // Layout: [X]  [thumbnail]  [filename]
        // User can optionally type a caption in the input field before pressing Send.
        Container {
            id: attachPreviewBar
            visible: chatViewPage.pendingAttachPath.length > 0
            horizontalAlignment: HorizontalAlignment.Fill
            background: chatViewPage.isDark ? Color.create("#1e2a38") : Color.create("#dce8f5")
            topPadding:    ui.du(1.0)
            bottomPadding: ui.du(1.0)
            leftPadding:   ui.du(1.0)
            rightPadding:  ui.du(1.5)
            layout: StackLayout { orientation: LayoutOrientation.LeftToRight }

            // X — cancel pending attachment
            Container {
                verticalAlignment: VerticalAlignment.Center
                preferredWidth:  ui.du(6)
                preferredHeight: ui.du(6)
                layout: DockLayout {}
                gestureHandlers: [
                    TapHandler {
                        onTapped: {
                            chatViewPage.pendingAttachPath = "";
                            chatViewPage.pendingAttachName = "";
                            sendAction.enabled = (inputField.text.trim().length > 0);
                        }
                    }
                ]
                Label {
                    text: "✕"
                    horizontalAlignment: HorizontalAlignment.Center
                    verticalAlignment:   VerticalAlignment.Center
                    textStyle {
                        fontSize: FontSize.Medium
                        fontWeight: FontWeight.Bold
                        color: chatViewPage.isDark ? Color.create("#aaaaaa") : Color.create("#555555")
                    }
                }
            }

            // Thumbnail preview
            Container {
                verticalAlignment: VerticalAlignment.Center
                preferredWidth:  ui.du(7)
                preferredHeight: ui.du(7)
                minWidth:        ui.du(7)
                minHeight:       ui.du(7)
                maxWidth:        ui.du(7)
                maxHeight:       ui.du(7)
                leftMargin:  ui.du(0.5)
                rightMargin: ui.du(1.0)
                background: chatViewPage.isDark ? Color.create("#3a3a3a") : Color.create("#cccccc")
                layout: DockLayout {}
                ImageView {
                    horizontalAlignment: HorizontalAlignment.Fill
                    verticalAlignment:   VerticalAlignment.Fill
                    scalingMethod: ScalingMethod.AspectFill
                    imageSource: chatViewPage.pendingAttachPath.length > 0
                                 ? ("file://" + chatViewPage.pendingAttachPath) : ""
                }
            }

            // Filename
            Label {
                layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                verticalAlignment: VerticalAlignment.Center
                text: chatViewPage.pendingAttachName.length > 0
                      ? chatViewPage.pendingAttachName : "Photo"
                multiline: false
                textStyle {
                    fontSize: FontSize.Small
                    color: chatViewPage.isDark ? Color.create("#dddddd") : Color.create("#222222")
                }
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
                    sendAction.enabled = (text.trim().length > 0 || chatViewPage.pendingAttachPath.length > 0);
                    chatViewPage.refreshQuickMessageSuggestions(text);
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
                onClicked: { chatViewPage.qmRequested = true; }
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
                    if (emojiPanelOpen && !chatViewPage.emojiPanelRef) {
                        var ep = emojiPanelDef.createObject();
                        ep.isDark = chatViewPage.isDark;
                        ep.horizontalAlignment = HorizontalAlignment.Fill;
                        ep.preferredHeight = ui.du(19);
                        ep.minHeight = ui.du(16);
                        emojiPanelSlot.add(ep);
                        emojiSignalCon.target = ep;
                        chatViewPage.emojiPanelRef = ep;
                    }
                    emojiPanelSlot.visible = emojiPanelOpen;
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
            id: searchAction
            title: "Search"
            imageSource: "asset:///images/ChatView/action_icon_search.png"
            ActionBar.placement: ActionBarPlacement.InOverflow
            onTriggered: {
                chatViewPage.searchVisible = !chatViewPage.searchVisible;
                if (!chatViewPage.searchVisible) {
                    chatViewPage.searchText = "";
                    chatViewPage.searchMatches = [];
                    chatViewPage.searchMatchPos = -1;
                }
            }
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

    // --- Quick Messages "/command" autocomplete --------------------------
    // Finds the "active" slash command at the end of the current text, if
    // any: the LAST "/" that starts the string or follows whitespace, with
    // no whitespace after it (i.e. the person is still typing that token).
    function activeSlashToken(text) {
        var slashIdx = text.lastIndexOf("/");
        if (slashIdx === -1) return null;
        if (slashIdx > 0) {
            var prevCh = text.charAt(slashIdx - 1);
            if (prevCh !== " " && prevCh !== "\n" && prevCh !== "\t") return null;
        }
        var rest = text.substring(slashIdx + 1);
        if (rest.indexOf(" ") !== -1 || rest.indexOf("\n") !== -1) return null;
        return { start: slashIdx, token: rest };
    }

    function refreshQuickMessageSuggestions(text) {
        var tok = chatViewPage.activeSlashToken(text);
        if (tok === null) {
            qmSuggestBar.visible = false;
            qmSuggestModel.clear();
            return;
        }
        var all = zService.getQuickMessages();
        var q = tok.token.toLowerCase();
        var matches = [];
        for (var i = 0; i < all.length && matches.length < 5; i++) {
            if (q.length === 0 || all[i].name.toLowerCase().indexOf(q) === 0) {
                matches.push(all[i]);
            }
        }
        qmSuggestModel.clear();
        qmSuggestModel.insertList(matches);
        qmSuggestBar.visible = (matches.length > 0);
    }

    function applyQuickMessage(content) {
        var tok = chatViewPage.activeSlashToken(inputField.text);
        inputField.text = (tok !== null) ? (inputField.text.substring(0, tok.start) + content) : content;
        qmSuggestBar.visible = false;
        qmSuggestModel.clear();
        inputField.requestFocus();
    }

    function doSend() {
        var txt = inputField.text.trim();
        var hasPhoto = (chatViewPage.pendingAttachPath.length > 0);
        if (txt.length === 0 && !hasPhoto) return;
        if (!chatViewPage.threadId || chatViewPage.threadId === "") return;
        sendAction.enabled = false;
        var caption = txt;
        inputField.text = "";
        qmSuggestBar.visible = false;
        qmSuggestModel.clear();

        if (hasPhoto) {
            var imgPath = chatViewPage.pendingAttachPath;
            var imgName = chatViewPage.pendingAttachName;
            chatViewPage.pendingAttachPath = "";
            chatViewPage.pendingAttachName = "";
            var dim = zService.getImageDimensions(imgPath);
            var fsize = zService.getFileSize(imgPath);
            var baseContent = caption.length > 0 ? JSON.stringify({caption: caption}) : "";
            var localContent = baseContent;
            if (fsize > 0) {
                if (localContent.length > 0)
                    localContent = localContent.slice(0, -1) + ',"fileSize":' + fsize + '}';
                else
                    localContent = '{"fileSize":' + fsize + '}';
            }
            if (imgName.length > 0) {
                if (localContent.length > 0)
                    localContent = localContent.slice(0, -1) + ',"fileName":' + JSON.stringify(imgName) + '}';
                else
                    localContent = '{"fileName":' + JSON.stringify(imgName) + '}';
            }
            var imgPlaceholder = {
                msgId:      "local_img_" + new Date().getTime(),
                content:    localContent,
                msgType:    2,
                localImage: "file://" + imgPath,
                imgWidth:   dim.width  || 0,
                imgHeight:  dim.height || 0,
                isMine:     true,
                isGroup:    chatViewPage.isGroup,
                senderId:   "self",
                dName:      chatViewPage.selfName,
                ts:         String(new Date().getTime()),
                selfName:   chatViewPage.selfName
            };
            msgModel.append(imgPlaceholder);
            chatViewPage.rebuildGroups();
            msgList.scrollToPosition(ScrollPosition.End, ScrollAnimation.Smooth);
            zService.sendPhoto(chatViewPage.threadId, imgPath, chatViewPage.isGroup, caption);
            return;
        }

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

    // A photo sent together with a text caption from the official Zalo app
    // carries that caption as a "caption" key inside the photo's content JSON
    // (see normalizePhotoContent() on the C++ side). Same regex-extraction
    // style as extractPhotoUrl() above, kept separate since this one needs to
    // un-escape \", \\, \n, \r, \t the C++ side escaped when building the JSON.
    function extractPhotoCaption(content) {
        if (typeof content !== "string" || content.length === 0) return "";
        if (content.charAt(0) !== "{") return "";
        var m = content.match(/"caption"\s*:\s*"((?:[^"\\]|\\.)*)"/);
        if (!m || !m[1]) return "";
        return m[1].replace(/\\n/g, "\n").replace(/\\r/g, "\r")
                   .replace(/\\t/g, "\t").replace(/\\"/g, "\"").replace(/\\\\/g, "\\");
    }

    function rebuildGroups() {
        var size = msgModel.size();
        if (size === 0) return;

        var items = [];
        for (var i = 0; i < size; i++) {
            items.push(msgModel.value(i));
        }

        // Normalise a Zalo timestamp (may be seconds or ms) to milliseconds.
        function toMs(ts) {
            var n = (ts || 0) * 1;
            if (n > 0 && n < 1e12) n *= 1000;
            return n;
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

            // Photo messages (msgType 2) always stand alone — never merged into
            // an adjacent text bubble regardless of sender or timing.
            var curIsPhoto  = (cur.msgType  === 2 || cur.msgType  === "2");
            var prevIsPhoto = prev ? (prev.msgType === 2 || prev.msgType === "2") : false;
            var nextIsPhoto = next ? (next.msgType === 2 || next.msgType === "2") : false;

            // 5-minute grouping window: messages more than 5 min apart from the
            // same sender start a fresh bubble even with no reply in between.
            var curTs  = toMs(cur.ts);
            var prevTs = toMs(prev ? prev.ts : null);
            var nextTs = toMs(next ? next.ts : null);
            var withinPrev = prev !== null && (Math.abs(curTs - prevTs) < 300000);
            var withinNext = next !== null && (Math.abs(nextTs - curTs) < 300000);

            var samePrev = !curIsPhoto && !prevIsPhoto && (prev !== null)
                           && (prevMine === curMine) && withinPrev
                           && (!chatViewPage.isGroup || curMine || curSender === prevSender);
            var sameNext = !curIsPhoto && !nextIsPhoto && (next !== null)
                           && (nextMine === curMine) && withinNext
                           && (!chatViewPage.isGroup || curMine || curSender === nextSender);

            var pos;
            if      ( samePrev &&  sameNext) pos = "middle";
            else if ( samePrev && !sameNext) pos = "bottom";
            else if (!samePrev &&  sameNext) pos = "top";
            else                             pos = "full";

            cur.bubblePos = pos;
            cur.grouped   = samePrev;
            cur.isMine    = curMine;

            // latestTs: the timestamp shown on the group header is always the
            // time of the last message in the group. Back-propagate to the
            // group start, stopping at the group boundary (grouped === false).
            if (!sameNext) {
                cur.latestTs = cur.ts;
                if (cur.grouped) {
                    var k = i - 1;
                    while (k >= 0) {
                        items[k].latestTs = cur.ts;
                        if (!items[k].grouped) break;
                        k--;
                    }
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
        ComponentDefinition {
            id: emojiPanelDef
            source: "EmojiPanel.qml"
        },

        // Receives emojiPicked from the lazily-created EmojiPanel instance.
        // target is set explicitly (not via .connect()) to avoid the BB10
        // dynamic-signal-connection reliability issue.
        // target: null disables validation until we assign the real instance.
        Connections {
            id: emojiSignalCon
            target: null
            onEmojiPicked: {
                inputField.text = inputField.text + charStr
            }
        },

        // Full-screen photo viewer — opened when the user taps a photo attachment.
        PhotoViewerSheet {
            id: photoViewerSheet
        },

        // Watches msgList.tappedPhotoPath set by the delegate's TapHandler and
        // opens the viewer. Connections target must be assigned after creation
        // (onCreationCompleted of the Page) to avoid construction-time issues.
        Connections {
            id: photoTapCon
            target: null
            onTappedPhotoPathChanged: {
                var p = msgList.tappedPhotoPath;
                if (p.length > 0) {
                    photoViewerSheet.imagePath = p;
                    photoViewerSheet.open();
                    msgList.tappedPhotoPath = "";
                }
            }
        },

        InfoDialog {
            id: voiceCallUnderDevDialog
            title: "Voice Call"
            body: "This feature is still under development."
        },

        InfoDialog {
            id: videoCallUnderDevDialog
            title: "Video Call"
            body: "This feature is still under development."
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
                        // Early dedup: if the model already has a confirmed row for this
                        // msgId, skip entirely (HTTP confirm + WS echo can both fire).
                        if (msg.msgId) {
                            for (var di0 = 0; di0 < msgModel.size(); di0++) {
                                var dv0 = msgModel.value(di0);
                                if (dv0.msgId === msg.msgId && dv0.msgId.indexOf("local_") !== 0) {
                                    // Already stored — only patch localImage if newly available.
                                    if (msg.localImage && msg.localImage.length > 0
                                        && (!dv0.localImage || dv0.localImage.length === 0)) {
                                        dv0.localImage = msg.localImage;
                                        msgModel.replace(di0, dv0);
                                    }
                                    return;
                                }
                            }
                        }

                        var savedLocalImage = "";
                        var savedImgWidth = 0, savedImgHeight = 0, savedFileSize = 0;
                        var placeholderIdx = -1;
                        for (var pi = msgModel.size() - 1; pi >= 0; pi--) {
                            var pitem = msgModel.value(pi);
                            if (pitem.msgId && pitem.msgId.indexOf("local_img_") === 0) {
                                savedLocalImage = pitem.localImage || "";
                                savedImgWidth  = pitem.imgWidth  || 0;
                                savedImgHeight = pitem.imgHeight || 0;
                                var fsM = (pitem.content || "").match(/"fileSize"\s*:\s*(\d+)/);
                                savedFileSize = fsM ? parseInt(fsM[1], 10) : 0;
                                placeholderIdx = pi;
                                break;
                            }
                        }
                        if (savedLocalImage.length > 0) msg.localImage = savedLocalImage;
                        if (!msg.imgWidth  && savedImgWidth  > 0) msg.imgWidth  = savedImgWidth;
                        if (!msg.imgHeight && savedImgHeight > 0) msg.imgHeight = savedImgHeight;
                        // The server confirmation (HTTP contentJson / WS normalizePhotoContent)
                        // never carries a file-size field — only the QML-built placeholder did —
                        // so re-inject it here or the size disappears from the bubble the moment
                        // "Sending..." flips to "Picture sent".
                        if (savedFileSize > 0 && (msg.content || "").indexOf('"fileSize"') < 0) {
                            if (msg.content && msg.content.charAt(0) === "{")
                                msg.content = msg.content.slice(0, -1) + ',"fileSize":' + savedFileSize + '}';
                            else
                                msg.content = '{"fileSize":' + savedFileSize + '}';
                        }

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

                        // Last-resort dedup: the HTTP send-msg confirmation and the WS echo
                        // for the SAME photo can carry two different msgId values (the HTTP
                        // response's id field is occasionally 0/missing, so the C++ side falls
                        // back to a locally-generated id) — neither the msgId nor localImage
                        // match above would catch that case, since the WS copy never carries
                        // a localImage. Treat any other already-stored mine/photo row sent
                        // within the last 8s with the same caption as the same physical send
                        // and merge into it instead of appending a visual duplicate.
                        if (!handledInPlace) {
                            var myTs = parseInt(msg.ts || "0", 10);
                            var myCap = chatViewPage.extractPhotoCaption(msg.content || "");
                            for (var di2 = msgModel.size() - 1; di2 >= 0; di2--) {
                                var dv2 = msgModel.value(di2);
                                if (!dv2.isMine) continue;
                                if (dv2.msgType !== 2 && dv2.msgType !== "2") continue;
                                if (dv2.msgId === msg.msgId) continue;
                                var otherTs = parseInt(dv2.ts || "0", 10);
                                if (Math.abs(otherTs - myTs) > 8000) continue;
                                if (chatViewPage.extractPhotoCaption(dv2.content || "") !== myCap) continue;
                                if ((!msg.localImage || msg.localImage.length === 0) && dv2.localImage && dv2.localImage.length > 0)
                                    msg.localImage = dv2.localImage;
                                msgModel.replace(di2, msg);
                                handledInPlace = true;
                                break;
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

                var ext = path.substring(path.lastIndexOf('.') + 1).toLowerCase();
                var isImg = (ext === "jpg" || ext === "jpeg" || ext === "png"
                             || ext === "gif" || ext === "webp" || ext === "bmp");

                if (isImg) {
                    // Copy into the persistent "/tmp/zalo_img_local_<ts>.<ext>" cache right away
                    // (not just on Send) so the original picked photo survives even if the
                    // picker's own path is transient/sandboxed, and is never lost regardless
                    // of what happens with the upload/WS echo afterwards.
                    var cachedPath = zService.cacheLocalImage(path);
                    // Stage the image — user can type a caption then press Send.
                    var fname = path.substring(path.lastIndexOf('/') + 1);
                    chatViewPage.pendingAttachPath = cachedPath;
                    chatViewPage.pendingAttachName = fname;
                    inputField.requestFocus();
                    sendAction.enabled = true;
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

        ConfirmDialog {
            id: blockDialog
            title: "Block user"
            body: "Block " + chatViewPage.threadName + "? They won't be able to message you."
            confirmLabel: "Block"
            onConfirmed: zService.blockUser(chatViewPage.threadId)
        },

        ConfirmDialog {
            id: clearHistoryDialog
            title: "Clear history"
            body: "Delete all messages in this conversation? This only removes them for you."
            confirmLabel: "Clear"
            onConfirmed: zService.clearHistory(chatViewPage.threadId, chatViewPage.isGroup)
        },

        ConfirmDialog {
            id: leaveGroupDialog
            title: "Leave group"
            body: "Leave " + chatViewPage.threadName + "? You won't be able to receive messages from this group."
            confirmLabel: "Leave"
            onConfirmed: zService.leaveGroup(chatViewPage.threadId)
        }
    ]
}
