import bb.cascades 1.4
import bb.cascades.pickers 1.0
import bb.system 1.0
import QtQuick 1.0

Page {
    id: chatViewPage

    property string threadId:    ""
    property string threadName:  ""
    property bool   isGroup:     false
    // TODO: no group-role data source exists yet anywhere in this project
    // (no admin/owner list is fetched/stored for group members). Wired as
    // false for now so "Delete" (delete-for-everyone) stays hidden for
    // everyone until real role data is available — see chat note to Jim.
    property bool   isCurrentUserAdminOrOwner: false
    property string avatarUrl:   ""
    property string selfName:    ""
    property string pendingMsg:  ""
    property bool   initialized: false
    property bool   emojiPanelOpen: false
    property variant emojiPanelRef: null
    property string pendingAttachPath: ""
    property string pendingAttachName: ""
    property variant qmMatch: null
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

    // "Delete for me" (as opposed to applyRecall's "recall/undo"): the message
    // must vanish from OUR OWN view completely, with no placeholder text —
    // unlike recall, which stays visible as "(this message was recalled)".
    // Only ever called for deletions WE performed (see the backend's
    // extractDeleteInfo() self-only guard) — the other participant's
    // "delete for me" never reaches this function.
    function applyLocalDelete(msgId) {
        var size = msgModel.size();
        for (var j = 0; j < size; j++) {
            var d = msgModel.value(j);
            if ((d.msgId || "") === msgId) {
                msgModel.removeAt(j);
                // Removing a row changes who is now adjacent to whom, which
                // changes bubblePos ("top"/"middle"/"bottom"/"full" — and with
                // it, whether the accent strip renders) for the messages that
                // used to sandwich this one. Without this, the neighbor above
                // a deleted message could be left stuck with a stale "top"
                // bubblePos and permanently lose its strip even though it's
                // now the last message in its group.
                rebuildGroups();
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
            property bool   isAdminOrOwner: chatViewPage.isCurrentUserAdminOrOwner
            property string searchQuery: chatViewPage.searchVisible ? chatViewPage.searchText.toLowerCase().trim() : ""
            property int    searchCurrentMsgIndex: (chatViewPage.searchMatchPos >= 0 && chatViewPage.searchMatchPos < chatViewPage.searchMatches.length)
                                                     ? chatViewPage.searchMatches[chatViewPage.searchMatchPos] : -1
            horizontalAlignment: HorizontalAlignment.Fill
            layoutProperties: StackLayoutProperties { spaceQuota: 1 }
            dataModel: msgModel
            bottomPadding: ui.du(1.5)
            flickMode: FlickMode.Momentum

            attachedObjects: [
                ArrayDataModel { id: msgModel }
            ]

            // Bubble hold-menu action stubs. Wired to individual functions
            // (not one generic dispatcher) so each can be implemented and
            // tested independently later. Copy/Share are implemented below;
            // the rest are still no-op besides a console.log.
            // isPhoto + localImage let us branch to the image-aware copy/share
            // path. Previously doCopy/doShare always treated bubble content as
            // plain text, so copying/sharing a photo message copied/shared the
            // {"normalUrl":...} JSON string instead of the picture — pasting
            // it anywhere just showed that text, never an image. Now, when the
            // bubble is a photo AND we already have it cached locally
            // (localImage — the same file the bubble itself renders from),
            // we copy/share the actual image bytes from disk instead.
            // Falls back to the old text behavior if no local copy exists yet
            // (e.g. still downloading) rather than silently doing nothing.
            function doCopy(content, isPhoto, localImage) {
                if (isPhoto) {
                    errorToast.body = "Copy isn't available for photos";
                    errorToast.show();
                    return;
                }
                app.copyToClipboard(content);
                copyToast.show();
            }
            function doReply(msgId)        { console.log("[bubble] Reply " + msgId); }
            function doReaction(msgId)     { console.log("[bubble] Reaction " + msgId); }
            function doRecallMsg(msgId, cliMsgId, isMine) {
                if (!isMine) {
                    errorToast.body = "You can only recall your own messages";
                    errorToast.show();
                    return;
                }
                zService.recallMessage(chatViewPage.threadId, chatViewPage.isGroup, msgId, cliMsgId);
            }
            function doForward(msgId)      { console.log("[bubble] Forward " + msgId); }
            function doPin(msgId)          { console.log("[bubble] Pin " + msgId); }
            function doDownload(msgId, localImage) {
                if (!localImage || localImage.length === 0) {
                    errorToast.body = "Photo not downloaded yet";
                    errorToast.show();
                    return;
                }
                var saved = zService.downloadPhotoToGallery(localImage, msgId);
                if (saved && saved.length > 0) {
                    downloadToast.show();
                } else {
                    errorToast.body = "Failed to save photo";
                    errorToast.show();
                }
            }
            function doShare(content, isPhoto, localImage) {
                if (isPhoto) {
                    errorToast.body = "Share isn't available for photos";
                    errorToast.show();
                    return;
                }
                sharePicker.pendingShareIsImage = false;
                sharePicker.pendingShareText = content;
                shareDimDialog.open();
            }
            function doCreateEvent(msgId)  { createEventUnderDevDialog.show(); }
            function doDeleteForMe(msgId, cliMsgId, senderId) {
                zService.deleteMessage(chatViewPage.threadId, chatViewPage.isGroup, msgId, cliMsgId, senderId, true);
            }
            function doDeleteMsg(msgId)    { console.log("[bubble] Delete " + msgId); }

            listItemComponents: [
                ListItemComponent {
                    type: ""
                    CustomListItem {
                        id: rowRoot
                        highlightAppearance: HighlightAppearance.None
                        dividerVisible: false
                        // CustomListItem (the stock Cascades control, not something this
                        // project defines) carries its own built-in default padding
                        // around its content — separate from, and layered on top of,
                        // the inner Container's own `topPadding: grouped ? 0 : 10`
                        // logic a bit further down. That inherited padding is constant
                        // no matter what grouped/bubblePos end up being, which is
                        // exactly why four different attempts at fixing how rows get
                        // written into msgModel (remove+insert, snapshot comparison,
                        // clear+append, forcing a fresh dataModel) never changed
                        // anything on screen — the diagnostics proved the data and the
                        // bindings were correct the whole time, but this separate,
                        // always-on padding was never touched by any of them. Zeroing
                        // it here lets the inner Container's grouped-based padding be
                        // the only source of vertical spacing between rows.
                        topPadding: 0; bottomPadding: 0; leftPadding: 0; rightPadding: 0

                        // Cascades ActionItem/DeleteActionItem has no "visible" property
                        // (they extend UIObject, not Control) — assigning one is a hard
                        // QML parse error that fails the WHOLE document load, which is
                        // why ChatView.qml refused to open entirely. Fix: build two full
                        // ActionSets (member vs admin/owner) and pick one, instead of
                        // trying to toggle visibility on a single item.
                        contextActions: [
                            rowRoot.isAdminOrOwner ? bubbleActionsAdmin : bubbleActionsMember
                        ]

                        attachedObjects: [
                            ActionSet {
                                id: bubbleActionsMember
                                title: "Message"
                                ActionItem {
                                    title: "Copy"
                                    imageSource: "asset:///images/ChatView/ic_copy.png"
                                    onTriggered: { rowRoot.ListItem.view.doCopy(ListItemData.content, (ListItemData.msgType === 2 || ListItemData.msgType === "2"), ListItemData.localImage); }
                                }
                                ActionItem {
                                    title: "Reply"
                                    imageSource: "asset:///images/ChatView/ic_quote_message.png"
                                    onTriggered: { rowRoot.ListItem.view.doReply(ListItemData.msgId); }
                                }
                                ActionItem {
                                    title: "Reaction"
                                    imageSource: "asset:///images/ChatView/ic_emoticon_enabled_white.png"
                                    onTriggered: { rowRoot.ListItem.view.doReaction(ListItemData.msgId); }
                                }
                                ActionItem {
                                    title: "Recall"
                                    imageSource: "asset:///images/ChatView/ic_recall.png"
                                    onTriggered: { rowRoot.ListItem.view.doRecallMsg(ListItemData.msgId, ListItemData.cliMsgId, rowRoot.mine); }
                                }
                                ActionItem {
                                    title: "Forward"
                                    imageSource: "asset:///images/ChatView/ic_forward_message.png"
                                    onTriggered: { rowRoot.ListItem.view.doForward(ListItemData.msgId); }
                                }
                                ActionItem {
                                    title: "Pin message"
                                    imageSource: "asset:///images/ChatView/ic_pin.png"
                                    onTriggered: { rowRoot.ListItem.view.doPin(ListItemData.msgId); }
                                }
                                ActionItem {
                                    title: "Download"
                                    imageSource: "asset:///images/ChatView/ic_download.png"
                                    onTriggered: { rowRoot.ListItem.view.doDownload(ListItemData.msgId, ListItemData.localImage); }
                                }
                                ActionItem {
                                    title: "Share"
                                    imageSource: "asset:///images/ChatView/ic_share.png"
                                    onTriggered: { rowRoot.ListItem.view.doShare(ListItemData.content, (ListItemData.msgType === 2 || ListItemData.msgType === "2"), ListItemData.localImage); }
                                }
                                ActionItem {
                                    title: "Create event"
                                    imageSource: "asset:///images/ChatView/ic_create_event.png"
                                    onTriggered: { rowRoot.ListItem.view.doCreateEvent(ListItemData.msgId); }
                                }
                                ActionItem {
                                    title: "Delete for me only"
                                    imageSource: "asset:///images/ChatView/action_delete.png"
                                    onTriggered: { rowRoot.ListItem.view.doDeleteForMe(ListItemData.msgId, ListItemData.cliMsgId, ListItemData.senderId); }
                                }
                            },
                            ActionSet {
                                id: bubbleActionsAdmin
                                title: "Message"
                                ActionItem {
                                    title: "Copy"
                                    imageSource: "asset:///images/ChatView/ic_copy.png"
                                    onTriggered: { rowRoot.ListItem.view.doCopy(ListItemData.content, (ListItemData.msgType === 2 || ListItemData.msgType === "2"), ListItemData.localImage); }
                                }
                                ActionItem {
                                    title: "Reply"
                                    imageSource: "asset:///images/ChatView/ic_quote_message.png"
                                    onTriggered: { rowRoot.ListItem.view.doReply(ListItemData.msgId); }
                                }
                                ActionItem {
                                    title: "Reaction"
                                    imageSource: "asset:///images/ChatView/ic_emoticon_enabled_white.png"
                                    onTriggered: { rowRoot.ListItem.view.doReaction(ListItemData.msgId); }
                                }
                                ActionItem {
                                    title: "Recall"
                                    imageSource: "asset:///images/ChatView/ic_recall.png"
                                    onTriggered: { rowRoot.ListItem.view.doRecallMsg(ListItemData.msgId, ListItemData.cliMsgId, rowRoot.mine); }
                                }
                                ActionItem {
                                    title: "Forward"
                                    imageSource: "asset:///images/ChatView/ic_forward_message.png"
                                    onTriggered: { rowRoot.ListItem.view.doForward(ListItemData.msgId); }
                                }
                                ActionItem {
                                    title: "Pin message"
                                    imageSource: "asset:///images/ChatView/ic_pin.png"
                                    onTriggered: { rowRoot.ListItem.view.doPin(ListItemData.msgId); }
                                }
                                ActionItem {
                                    title: "Download"
                                    imageSource: "asset:///images/ChatView/ic_download.png"
                                    onTriggered: { rowRoot.ListItem.view.doDownload(ListItemData.msgId, ListItemData.localImage); }
                                }
                                ActionItem {
                                    title: "Share"
                                    imageSource: "asset:///images/ChatView/ic_share.png"
                                    onTriggered: { rowRoot.ListItem.view.doShare(ListItemData.content, (ListItemData.msgType === 2 || ListItemData.msgType === "2"), ListItemData.localImage); }
                                }
                                ActionItem {
                                    title: "Create event"
                                    imageSource: "asset:///images/ChatView/ic_create_event.png"
                                    onTriggered: { rowRoot.ListItem.view.doCreateEvent(ListItemData.msgId); }
                                }
                                ActionItem {
                                    title: "Delete for me only"
                                    imageSource: "asset:///images/ChatView/action_delete.png"
                                    onTriggered: { rowRoot.ListItem.view.doDeleteForMe(ListItemData.msgId, ListItemData.cliMsgId, ListItemData.senderId); }
                                }
                                DeleteActionItem {
                                    title: "Delete"
                                    imageSource: "asset:///images/ChatView/action_delete.png"
                                    onTriggered: { rowRoot.ListItem.view.doDeleteMsg(ListItemData.msgId); }
                                }
                            }
                        ]

                        property bool isDark: ListItem.view.isDark
                        property bool isAdminOrOwner: ListItem.view.isAdminOrOwner

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
                        // TEMP DIAGNOSTIC — pinpointing whether the "always caps at 2"
                        // grouping bug is a QML binding problem (ListItemData.grouped
                        // never reaching this row as true) or a Cascades-internal
                        // layout/height cache problem (binding IS true but topPadding's
                        // effect on measured height doesn't stick). rebuildGroups()
                        // already logs what it WROTE into msgModel; this logs what this
                        // delegate instance actually READS for the same row, so
                        // diffing the two logs tells us which side is wrong. Safe to
                        // delete once that's answered.
                        onGroupedChanged: {
                            console.log("[Zalo QML] delegate grouped-binding: msgId=" + String(ListItemData.msgId).slice(-6)
                                + " grouped=" + grouped + " bubblePos=" + (ListItemData.bubblePos || "full")
                                + " index=" + ListItem.indexPath[0]);
                        }
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
                        // TEMP DIAGNOSTIC — see onGroupedChanged above for why.
                        onBubblePosChanged: {
                            console.log("[Zalo QML] delegate bubblePos-binding: msgId=" + String(ListItemData.msgId).slice(-6)
                                + " bubblePos=" + bubblePos + " index=" + ListItem.indexPath[0]);
                        }
                        property string bubbleImage: rowRoot.mine
                            ? ("asset:///images/Bubble/outgoing/" + rowRoot.bubblePos + ".png")
                            : ("asset:///images/Bubble/incoming/" + rowRoot.bubblePos + ".png")
                        property real bubbleMaxW: rowLUH.layoutFrame.width > 94
                                                   ? (rowLUH.layoutFrame.width - 94)
                                                   : ui.du(40)

                        Container {
                            horizontalAlignment: HorizontalAlignment.Fill
                            topPadding:    rowRoot.grouped ? 0 : 10
                            bottomPadding: 0
                            layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                            attachedObjects: [
                                LayoutUpdateHandler {
                                    id: rowLUH
                                    // TEMP DIAGNOSTIC — logs the row's REAL measured
                                    // height after layout, next to the grouped value
                                    // that decided its topPadding. Everything logged
                                    // so far (rebuildGroups' own dump, and the
                                    // delegate's grouped/bubblePos bindings) only
                                    // proves the JS-level values were correct — none
                                    // of it proves Cascades actually shrank this
                                    // Container's real height when topPadding dropped
                                    // from 10 to 0. This is that missing proof: if
                                    // height stays the same across a grouped:false→
                                    // true transition, the padding change isn't
                                    // reaching layout at all, confirming a
                                    // Cascades-level measurement bug rather than
                                    // anything left to fix in this file's own JS.
                                    // Safe to delete once that's answered.
                                    onLayoutFrameChanged: {
                                        console.log("[Zalo QML] row layoutFrame CHANGED: msgId=" + String(ListItemData.msgId).slice(-6)
                                            + " grouped=" + rowRoot.grouped + " bubblePos=" + rowRoot.bubblePos
                                            + " height=" + layoutFrame.height
                                            + " index=" + ListItem.indexPath[0]);
                                    }
                                }
                            ]
                        }

                        Container {
                            preferredWidth: rowRoot.mine ? 18 : 60
                            minWidth:       rowRoot.mine ? 18 : 60
                            maxWidth:       rowRoot.mine ? 18 : 60
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

                                // Photo attachment bubble.
                                // Layout: caption (if any) → dashed separator → inline photo (capped
                                // to bubbleMaxW, real aspect ratio) → status text. Shown directly in
                                // the bubble, no separate full-screen viewer page.
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

                                    // Inline photo — shown at real (capped) size directly in the
                                    // bubble, same sizing pattern as the recalled-photo preview
                                    // above (bubbleMaxW + aspect ratio). No tap-to-open viewer,
                                    // no filename/filesize row — just the picture.
                                    Container {
                                        horizontalAlignment: HorizontalAlignment.Left
                                        preferredWidth:  (ListItemData.imgWidth  && ListItemData.imgWidth  > 0)
                                                         ? Math.min(rowRoot.bubbleMaxW, ListItemData.imgWidth) : Math.min(rowRoot.bubbleMaxW, ui.du(30))
                                        preferredHeight: (ListItemData.imgWidth  && ListItemData.imgWidth  > 0
                                                          && ListItemData.imgHeight && ListItemData.imgHeight > 0)
                                                         ? (Math.min(rowRoot.bubbleMaxW, ListItemData.imgWidth) * (ListItemData.imgHeight / ListItemData.imgWidth))
                                                         : ui.du(30)
                                        background: rowRoot.isDark ? Color.create("#3a3a3a") : Color.create("#e0e0e0")
                                        layout: DockLayout {}

                                        ImageView {
                                            visible: !!(ListItemData.localImage && ListItemData.localImage !== "")
                                            horizontalAlignment: HorizontalAlignment.Fill
                                            verticalAlignment:   VerticalAlignment.Fill
                                            scalingMethod: ScalingMethod.AspectFit
                                            imageSource: ListItemData.localImage || ""
                                        }
                                        Label {
                                            visible: !(ListItemData.localImage && ListItemData.localImage !== "")
                                            text: "..."
                                            horizontalAlignment: HorizontalAlignment.Center
                                            verticalAlignment:   VerticalAlignment.Center
                                            textStyle {
                                                fontSize: FontSize.Small
                                                color: rowRoot.isDark ? Color.create("#888888") : Color.Gray
                                            }
                                        }
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
                                        topMargin: 4; bottomMargin: 0
                                    }
                                }
                            }
                        } // bubble content Container

                            // Accent strip along the bottom edge of the bubble —
                            // gray for my own messages, blue for incoming ones.
                            // Only the LAST bubble of a grouped cluster ("bottom")
                            // or a standalone message ("full") gets the strip —
                            // otherwise every message in a group drew its own
                            // line, making a single grouped cluster look like it
                            // kept splitting into separate bubbles.
                            Container {
                                horizontalAlignment: HorizontalAlignment.Fill
                                preferredHeight: ui.du(0.8)
                                background: rowRoot.mine ? Color.create("#999999") : Color.create("#0073BC")
                                visible: rowRoot.bubblePos === "bottom" || rowRoot.bubblePos === "full"
                            }
                        } // bubbleWrap

                        Container {
                            preferredWidth: rowRoot.mine ? 60 : 18
                            minWidth:       rowRoot.mine ? 60 : 18
                            maxWidth:       rowRoot.mine ? 60 : 18
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
            topMargin: ui.du(0.8)
            visible: false
        }

        // Thin single-row quick-message suggestion bar — mirrors
        // attachPreviewBar's structure/height instead of the old tall
        // scrollable list. Only the single best match is shown; typing
        // more narrows which one that is.
        Container {
            id: qmSuggestBar
            visible: !!chatViewPage.qmMatch
            horizontalAlignment: HorizontalAlignment.Fill
            background: chatViewPage.isDark ? Color.create("#1e2a38") : Color.create("#dce8f5")
            topPadding:    ui.du(1.0)
            bottomPadding: ui.du(1.0)
            leftPadding:   ui.du(1.5)
            rightPadding:  ui.du(1.5)
            layout: StackLayout { orientation: LayoutOrientation.LeftToRight }

            gestureHandlers: [
                TapHandler {
                    onTapped: {
                        if (chatViewPage.qmMatch)
                            chatViewPage.applyQuickMessage(chatViewPage.qmMatch.content);
                    }
                }
            ]

            Label {
                verticalAlignment: VerticalAlignment.Center
                text: chatViewPage.qmMatch ? ("/" + chatViewPage.qmMatch.name) : ""
                textStyle { color: Color.create("#2575fc"); fontWeight: FontWeight.Bold }
                multiline: false
                rightMargin: ui.du(1.5)
            }

            Label {
                layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                verticalAlignment: VerticalAlignment.Center
                text: chatViewPage.qmMatch ? chatViewPage.qmMatch.content : ""
                textStyle {
                    color: chatViewPage.isDark ? Color.create("#cfd8e3") : Color.create("#444444")
                }
                multiline: false
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
                        // 23du = dot-row (2.5du) + 2 emoji rows tightly fit to
                        // EmojiButton's own 7du height + grid padding (1du) +
                        // bottom category bar (5.5du). The previous 28du left
                        // ~2.5du of extra vertical space per row that GridLayout
                        // spread out as padding around each button — visually
                        // that read as "emoji look stretched" even though the
                        // icon itself was already locked to a 1:1 box.
                        ep.preferredHeight = ui.du(23);
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
            chatViewPage.qmMatch = null;
            return;
        }
        var all = zService.getQuickMessages();
        var q = tok.token.toLowerCase();
        if (q.length === 0) {
            chatViewPage.qmMatch = null;
            return;
        }
        // Pick the single best match: an exact name match wins outright;
        // otherwise the shortest prefix match (closest to what's typed).
        var best = null;
        for (var i = 0; i < all.length; i++) {
            var name = all[i].name.toLowerCase();
            if (name === q) { best = all[i]; break; }
            if (name.indexOf(q) === 0) {
                if (best === null || name.length < best.name.length) best = all[i];
            }
        }
        chatViewPage.qmMatch = best;
    }

    function applyQuickMessage(content) {
        var tok = chatViewPage.activeSlashToken(inputField.text);
        inputField.text = (tok !== null) ? (inputField.text.substring(0, tok.start) + content) : content;
        chatViewPage.qmMatch = null;
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
        chatViewPage.qmMatch = null;

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
        // Snapshot each row's CURRENT grouped/bubblePos before anything below
        // mutates it. msgModel.value(i) hands back the same underlying object
        // that items[i] then points to and gets its fields overwritten on
        // (cur.bubblePos = ...; cur.grouped = ...; a few dozen lines down) —
        // so comparing "old = msgModel.value(i)" against "items[i]" later was
        // really comparing that object against itself post-mutation, always
        // equal. layoutChanged was effectively always false, so the
        // remove+insert remeasure path documented below never actually ran —
        // this is the real reason grouped bubbles kept rendering with a stale
        // gap even though the recomputed grouped/bubblePos values were
        // correct. Capturing plain old/new values up front (not object
        // references) makes the before/after comparison meaningful again.
        var prevGrouped = [];
        var prevBubblePos = [];
        for (var i = 0; i < size; i++) {
            var v = msgModel.value(i);
            items.push(v);
            prevGrouped.push(v.grouped);
            prevBubblePos.push(v.bubblePos);
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

        // Writing every index back with replace() unconditionally is not just
        // wasteful — it's the actual cause of the "gap that only shows up after
        // the next message arrives" bug: replace() only swaps the item's DATA in
        // Cascades' ArrayDataModel, it does not reliably force the ListView to
        // re-measure that row's height. topPadding/bottomPadding directly depend
        // on `grouped`/`bubblePos` (see the Container bindings a few hundred
        // lines up), so whenever this recompute actually CHANGES those two
        // values for a row — e.g. a message that used to be "grouped" becomes
        // ungrouped because a deleted message that used to sit next to it is
        // gone now — the row's on-screen height silently goes stale at its OLD
        // size until something else (like a brand new append()) forces a full
        // relayout, which is exactly the "looked fine until the next message
        // arrived" symptom. remove+insert forces Cascades to treat the row as a
        // brand new item and re-measure it immediately instead of leaving a
        // stale cached height around.
        //
        // Doing that removeAt+insert PER ROW, though, turned out to have its
        // own failure mode: when several messages arrive close together (a
        // second or less apart), rebuildGroups() runs once per message, and
        // each run can touch multiple earlier rows whose grouped/bubblePos
        // just shifted because a new row joined their cluster. Each of those
        // per-row removeAt+insert calls kicks off Cascades' default
        // remove/insert item animation; with runs stacking up faster than an
        // animation can finish, a later run's removeAt+insert on the same
        // index interrupts the still-running animation from the previous
        // run, and the row is left with whatever half-finished height that
        // interruption produced — visually stuck looking "split" even though
        // the underlying grouped/bubblePos data is correct (confirmed by the
        // diagnostic dump below matching the intended grouping while the
        // screen still showed separate bubbles).
        //
        // Collapsing every row's update into a single clear()+append() avoids
        // that entirely: it's one atomic model reset instead of N
        // independently-animated per-row mutations, so there's nothing left
        // to race. Only pay that (heavier) cost when something actually
        // needs to re-layout; a run where nothing's grouping changed (e.g. a
        // photo URL just finished downloading) still uses cheap in-place
        // replace() per row, same as before.
        var anyLayoutChanged = false;
        for (var i = 0; i < size; i++) {
            if (prevGrouped[i] !== items[i].grouped || prevBubblePos[i] !== items[i].bubblePos) {
                anyLayoutChanged = true;
                break;
            }
        }
        if (anyLayoutChanged) {
            msgModel.clear();
            msgModel.append(items);
            // clear()+append() alone updates the ArrayDataModel's data, but the
            // delegate-binding diagnostics above proved grouped/bubblePos were
            // ALREADY arriving correctly at the delegate for the affected rows
            // even while the gap still showed — meaning the ListView itself was
            // reusing pooled Control instances whose already-measured height
            // Cascades wasn't recomputing off the padding-only change.
            //
            // The previous attempt to fix that detached and reattached
            // msgList.dataModel (set to null, then back to msgModel) to force
            // Cascades to drop every pooled item Control. That had zero visible
            // effect, which — combined with the diagnostics proving the data
            // itself was right all along — points at that specific trick
            // silently not doing anything: `dataModel` is typed as a
            // (non-nullable) DataModel, so assigning `null` to it from QML/JS
            // is plausibly just ignored, and the very next line re-assigns the
            // exact same msgModel reference it already had, which QML property
            // bindings treat as "unchanged" and skip re-notifying entirely. So
            // that whole reset was likely a no-op both times.
            //
            // Toggling the ListView's own visibility off and back on is a much
            // blunter, harder-to-swallow way to force the same outcome: it's a
            // plain bool property (no nullability ambiguity), going false then
            // true is guaranteed to be two real, distinct value changes, and
            // Cascades tears down and reinstantiates a Control's rendered
            // presentation on a visibility flip regardless of what's happening
            // one level down in its data model.
            msgList.visible = false;
            msgList.visible = true;
        } else {
            for (var i2 = 0; i2 < size; i2++) {
                msgModel.replace(i2, items[i2]);
            }
        }

        // TEMP DIAGNOSTIC — investigating the "gap after delete+reopen+resend"
        // bug report. Dumps every row's msgId/sender/content-length/grouped/
        // bubblePos so a fresh log capture shows definitively whether there's
        // a stray zero-content/ghost row still occupying a model slot near the
        // deleted message, or whether bubblePos/grouped themselves are wrong.
        // Safe to delete once that bug is found — this is not a fix by itself.
        {
            var dbgLine = "[Zalo QML] rebuildGroups: size=" + size;
            for (var di = 0; di < size; di++) {
                var dv = msgModel.value(di);
                dbgLine += " | [" + di + "] id=" + String(dv.msgId).slice(-6)
                    + " mine=" + chatViewPage.normMine(dv.isMine)
                    + " sender=" + (dv.senderId || "")
                    + " len=" + ((dv.content || "").length)
                    + " grp=" + dv.grouped + " pos=" + dv.bubblePos
                    + " ts=" + dv.ts;
            }
            console.log(dbgLine);
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

        // ---- Copy & Share (bubble hold-menu) — ported from SmartList10 ----
        SystemToast {
            id: copyToast
            body: "Copied to clipboard"
            position: SystemUiPosition.MiddleCenter
        },

        // Generic error toast for Delete/Recall/Download failures — body is set
        // by the caller right before show().
        SystemToast {
            id: errorToast
            position: SystemUiPosition.MiddleCenter
        },

        // Separate from copyToast: copyToast's body is a static binding
        // ("Copied to clipboard"), so reusing it here for Download would
        // permanently overwrite that binding for every future Copy too.
        SystemToast {
            id: downloadToast
            body: "Saved to Downloads"
            position: SystemUiPosition.MiddleCenter
        },

        SharePickerSheet { id: sharePicker },

        // "app" is a stable context property set once at startup (unlike
        // chatsNav.activeChatPage/etc. in ChatsTab.qml, which start null) —
        // safe to bind target directly here, no target:null dance needed.
        Connections {
            target: app
            onShareTargetsReady: {
                shareDimFadeOut.play();
                sharePicker.openWithTargets(targets);
            }
        },

        // Dim overlay shown while querying share targets — same pattern as
        // SmartList10's main.qml dimDialog.
        Dialog {
            id: shareDimDialog
            Container {
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment:   VerticalAlignment.Fill
                background: Color.create(0, 0, 0, 0.5)
                opacity: 0.0
                animations: [
                    FadeTransition {
                        id: shareDimFadeIn
                        duration: 150; toOpacity: 1.0
                        onEnded: {
                            if (sharePicker.pendingShareIsImage && sharePicker.pendingShareImagePath.length > 0) {
                                app.queryShareTargetsForImage(sharePicker.pendingShareImagePath);
                            } else {
                                app.queryShareTargets(sharePicker.pendingShareText);
                            }
                        }
                    },
                    FadeTransition {
                        id: shareDimFadeOut
                        duration: 150; toOpacity: 0.0
                        onEnded: { shareDimDialog.close(); }
                    }
                ]
            }
            onOpened: { shareDimFadeIn.play(); }
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

        InfoDialog {
            id: createEventUnderDevDialog
            title: "Create Event"
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
                    var dupIdx = -1;
                    for (var di = 0; di < msgModel.size(); di++) {
                        if (msgModel.value(di).msgId === msg.msgId) { dupIdx = di; break; }
                    }
                    if (dupIdx >= 0) {
                        // Same msgId already recorded — typically the optimistic row
                        // created right after the HTTP send-confirm (which only knows
                        // the local device clock, not the server's real ts) getting a
                        // second echo from the WS confirm (which carries the real,
                        // authoritative server ts). Previously this duplicate was
                        // silently dropped, so the row stayed stuck on local-clock ts
                        // forever. If the local device clock drifts from server time
                        // (as it does here, ~4h off), that stale ts both displays the
                        // wrong time AND falls outside the 5-minute grouping window,
                        // splitting a bubble that should still be merged with its
                        // neighbours. Adopt the newer ts when it actually differs.
                        var existingRow = msgModel.value(dupIdx);
                        if (msg.ts && String(msg.ts) !== String(existingRow.ts)) {
                            msgModel.replace(dupIdx, msg);
                            chatViewPage.rebuildGroups();
                            msgList.scrollToPosition(ScrollPosition.End, ScrollAnimation.Smooth);
                        }
                        return;
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

            // Result of our OWN recallMessage() call. On success, apply the same
            // in-place bubble update as an incoming chat.undo notification
            // (applyRecall) rather than waiting for a WS echo/re-fetch. On
            // failure, surface why via a toast instead of failing silently.
            onMessageRecalledDone: {
                if (threadId !== chatViewPage.threadId) return;
                if (success) {
                    chatViewPage.applyRecall(msgId);
                } else {
                    errorToast.body = error.length > 0 ? error : "Failed to recall message";
                    errorToast.show();
                }
            }

            // Result of our OWN deleteMessage() call ("Delete for me only", or
            // "delete for everyone" in a group). On success, remove the bubble
            // entirely from our own view (applyLocalDelete) — delete-for-me has
            // no placeholder, unlike recall. The actual WS confirmation (the
            // chat.delete event) arrives separately via onMessageDeletedLocally
            // below; doing it here too means the bubble disappears immediately
            // without waiting for that round-trip. On failure, show why.
            onMessageDeleted: {
                if (threadId !== chatViewPage.threadId) return;
                if (success) {
                    chatViewPage.applyLocalDelete(msgId);
                } else {
                    errorToast.body = error.length > 0 ? error : "Failed to delete message";
                    errorToast.show();
                }
            }

            // WS-side confirmation of OUR OWN "delete for me" (chat.delete),
            // already filtered server/backend-side to only ever fire for
            // deletions we performed (see extractDeleteInfo() in
            // ZaloServiceUtils.hpp) — another participant's delete-for-me never
            // reaches this signal. Re-applies applyLocalDelete as a harmless
            // no-op if onMessageDeleted above already removed the bubble.
            onMessageDeletedLocally: {
                if (threadId !== chatViewPage.threadId) return;
                chatViewPage.applyLocalDelete(msgId);
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
