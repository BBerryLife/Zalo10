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
    // Reply staging: set by doReply() when the user taps "Reply" on a bubble,
    // cleared on send/cancel. Mirrors pendingAttachPath's role for photos —
    // both are mutually exclusive "something is staged above the input bar"
    // states, so starting a reply while a photo is staged clears the photo
    // (and vice versa) rather than trying to send both at once.
    property string pendingReplyMsgId:      ""
    property string pendingReplyCliMsgId:   ""
    property string pendingReplyOwnerId:    ""
    property string pendingReplySenderName: ""
    property string pendingReplyContent:    ""
    property int    pendingReplyMsgType:    0   // our local msgType (1=text,2=photo) — used for preview only
    property string pendingReplyTs:         ""
    property variant qmMatch: null
    property variant dbIsMineCache: ({})
    property bool   isMuted: false
    property bool   isBlocked: false
    property bool   popRequested: false
    property bool   qmRequested: false
    // Same "flip a bool, the owning Nav watches for it and pushes the real
    // page" pattern qmRequested uses (see ChatsTab.qml/GroupsTab.qml's
    // onQmRequestedChanged) — ChatView itself can't push into the
    // NavigationPane that owns it directly.
    property bool   groupBoardRequested: false
    property variant pendingImageUpdates: ([])
    property bool   pageVisible: false
    property bool   isDark: app.getDarkTheme()
    property bool   showRecalledMessages: app.getShowRecalledMessages()
    property bool   searchVisible: false
    property string searchText: ""
    property variant searchMatches: []   // indices into msgModel that contain the current query
    property int      searchMatchPos: -1 // which entry in searchMatches is currently focused
    // Set briefly when the user taps a pinned-message entry (or the quote
    // strip inside a reply bubble) to jump to the original message. The
    // delegate's rowRoot.isJumpHighlighted compares its own msgId against
    // this to show the yellow highlight; jumpHighlightTimer clears it back
    // to "" after a couple seconds so the highlight is transient like the
    // search-match one, not a permanent state change on the row.
    property string jumpHighlightMsgId: ""

    // Device clock vs. server clock can differ by hours (confirmed in the
    // field: ~4h drift). Every OUTGOING message starts life as a "local_"
    // placeholder timestamped with the DEVICE clock (new Date().getTime()),
    // then gets swapped for the real row once the server confirms it — that
    // confirmed row's ts is the SERVER clock instead. rebuildGroups()'s
    // 5-minute grouping window compares ts across adjacent rows; as long as
    // one neighbour is still an unconfirmed device-clock placeholder while
    // the other has already been confirmed to server-clock ts, the raw
    // difference between them is off by the full device/server drift, not
    // just the real few-second gap — pushing it past the 300000ms window and
    // splitting bubbles that were sent seconds apart. clockOffsetMs is
    // (server ts - device ts) measured from the first confirmed outgoing
    // message (its cliMsgId is the original device timestamp the placeholder
    // was created with), then added to every still-local placeholder's ts
    // before any grouping comparison — see toMs() in rebuildGroups() — so
    // both clocks are normalized to the same reference before comparing.
    property real clockOffsetMs: 0
    property bool clockOffsetSet: false

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
        chatViewPage.flushPendingRebuild();
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

    // Measures clockOffsetMs (see its declaration above) from the first
    // outgoing message that carries both a cliMsgId (the device-clock ts the
    // "local_" placeholder was created with — Zalo echoes this back
    // unchanged) and a confirmed server ts. Cheap sanity bounds (a few
    // minutes of noise is normal network latency, not clock drift) avoid
    // latching onto a bogus offset from a malformed/missing cliMsgId.
    function updateClockOffset(cliMsgId, serverTs) {
        var cli = parseInt(cliMsgId || "0", 10);
        var srv = parseInt(serverTs || "0", 10);
        if (srv > 0 && srv < 1e12) srv *= 1000;
        if (cli <= 0 || srv <= 0) return;
        var diff = srv - cli;
        if (Math.abs(diff) < 60000) return; // negligible — normal latency, not drift
        chatViewPage.clockOffsetMs = diff;
        chatViewPage.clockOffsetSet = true;
    }

    // "Delete for me" (as opposed to applyRecall's "recall/undo"): the message
    // must vanish from OUR OWN view completely, with no placeholder text —
    // unlike recall, which stays visible as "(this message was recalled)".
    // Only ever called for deletions WE performed (see the backend's
    // extractDeleteInfo() self-only guard) — the other participant's
    // "delete for me" never reaches this function.
    function applyLocalDelete(msgId) {
        chatViewPage.flushPendingRebuild();
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

    // Companion to applyLocalDelete/applyRecall below — see
    // onMessageTsCorrected's comment for why this exists. Patches an
    // already-loaded row's ts to the just-arrived server value and re-runs
    // grouping, since the 5-minute grouping window in rebuildGroups()
    // compares ts across rows and a stale device-clock value can throw that
    // comparison off by hours.
    function applyTsCorrection(msgId, newTs) {
        chatViewPage.flushPendingRebuild();
        var size = msgModel.size();
        for (var j = 0; j < size; j++) {
            var d = msgModel.value(j);
            if ((d.msgId || "") === msgId) {
                if (d.ts === newTs) return; // already correct — avoid a pointless rebuild
                d.ts = newTs;
                msgModel.replace(j, d);
                chatViewPage.rebuildGroups();
                return;
            }
        }
    }

    function applyRecall(msgId) {
        chatViewPage.flushPendingRebuild();
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

    // Jump to any message by id (used by: tapping a pinned-message entry in
    // the pinboard bar/dim overlay, and tapping the quote strip inside a
    // reply bubble) — scrolls it into view and gives it the same yellow
    // highlight search matches get, for a couple seconds.
    function jumpToMessage(msgId) {
        if (!msgId || msgId.length === 0) return;
        var size = msgModel.size();
        for (var i = 0; i < size; i++) {
            if ((msgModel.value(i).msgId || "") === msgId) {
                msgList.scrollToItem([i], ScrollAnimation.Default);
                chatViewPage.jumpHighlightMsgId = msgId;
                jumpHighlightTimer.restart();
                return;
            }
        }
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

        // Cancel (not apply) any deferred rebuild flush left over from
        // whatever thread was open before this one — see
        // flushPendingRebuild()'s comment for the general race this guards
        // against. Applying a stale pendingItems here would be wrong in a
        // different way than the original bug: it would leak the PREVIOUS
        // thread's messages into this one's freshly-cleared msgModel. The
        // clear() + dbLoadMessages()/fetchMessages() below already fully
        // repopulate this thread's own messages, so the old pending flush
        // is simply discarded.
        rebuildFlushTimer.stop();
        rebuildFlushTimer.pendingItems = null;
        rebuildFlushTimer.pendingScroll = false;

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
            chatViewPage.rebuildGroups(true);
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
            property bool   isGroupChat: chatViewPage.isGroup
            property string threadNameProxy: chatViewPage.threadName
            property string selfUidProxy: zService.selfUid
            property string selfNameProxy: chatViewPage.selfName
            property string jumpHighlightMsgId: chatViewPage.jumpHighlightMsgId
            property string searchQuery: chatViewPage.searchVisible ? chatViewPage.searchText.toLowerCase().trim() : ""
            property int    searchCurrentMsgIndex: (chatViewPage.searchMatchPos >= 0 && chatViewPage.searchMatchPos < chatViewPage.searchMatches.length)
                                                     ? chatViewPage.searchMatches[chatViewPage.searchMatchPos] : -1
            // Proxies for chatViewPage's own functions: ListItemComponent delegates
            // (rowRoot and everything inside it) are a SEPARATE Cascades visual-root
            // scope from the rest of the Page — a plain "chatViewPage.foo()" call from
            // inside the delegate throws "ReferenceError: Can't find variable:
            // chatViewPage" at runtime even though it looks like valid, in-scope QML
            // (confirmed on-device; see doReply/doPin etc. above, which already work
            // precisely because they go through "rowRoot.ListItem.view.doX(...)"
            // instead of calling chatViewPage directly). highlightMatches()/
            // jumpToMessage() need the same indirection.
            function highlightMatchesProxy(text, query, color) { return chatViewPage.highlightMatches(text, query, color); }
            function jumpToMessageProxy(msgId) { chatViewPage.jumpToMessage(msgId); }
            // Reliable group-member uid->name lookup (see m_memberNames in
            // ZaloService — built from getmg-v2's currentMems, NOT the
            // per-message wire dName field, which is unreliable for incoming
            // messages in both 1-1 and group threads). Returns "" if unknown.
            function memberDisplayNameProxy(uid) { return zService.memberDisplayName(uid); }
            horizontalAlignment: HorizontalAlignment.Fill
            layoutProperties: StackLayoutProperties { spaceQuota: 1 }
            dataModel: msgModel
            bottomPadding: ui.du(1.5)
            flickMode: FlickMode.Momentum

            attachedObjects: [
                ArrayDataModel { id: msgModel },
                // Pairs with the clear()+deferred-append rebuild path in
                // rebuildGroups() below: clear() runs synchronously, then
                // this timer's zero-interval singleShot fire on the NEXT
                // event loop turn is what gives Cascades an actual empty-
                // dataModel layout pass in between, forcing it to drop
                // pooled item heights instead of recycling them stale.
                Timer {
                    id: rebuildFlushTimer
                    property variant pendingItems: null
                    property bool    pendingScroll: false
                    interval: 0
                    repeat: false
                    onTriggered: {
                        if (rebuildFlushTimer.pendingItems) {
                            msgModel.append(rebuildFlushTimer.pendingItems);
                            rebuildFlushTimer.pendingItems = null;
                        }
                        if (rebuildFlushTimer.pendingScroll) {
                            msgList.scrollToPosition(ScrollPosition.End, ScrollAnimation.Smooth);
                            rebuildFlushTimer.pendingScroll = false;
                        }
                    }
                }
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
            // Stages a reply above the input bar (see replyPreviewBar below),
            // exactly like tapping an attachment stages a photo. A photo and a
            // reply can't both be staged at once — starting a reply while a
            // photo is pending clears the photo attach first, matching the
            // "replace, don't stack" behaviour Jim asked for.
            //
            // senderName resolution happens HERE rather than at the ActionItem
            // call site in the delegate: rowRoot.otherDisplayName (computed via
            // ListItem.view.threadNameProxy) looked right on paper but still
            // came back "Unknown" on-device — ActionSet/ActionItem.onTriggered
            // turned out to be yet another Cascades scope boundary, on top of
            // the delegate-body one already worked around for
            // highlightMatches/jumpToMessage. doReply() itself lives on
            // msgList (confirmed reachable — chatViewPage.pendingAttachPath
            // above already works), so resolving the name here sidesteps the
            // problem instead of chasing another scope workaround.
            function doReply(msgId, cliMsgId, senderId, isMine, rawDName, content, msgType, ts) {
                if (chatViewPage.pendingAttachPath.length > 0) {
                    chatViewPage.pendingAttachPath = "";
                    chatViewPage.pendingAttachName = "";
                }
                var resolvedName;
                if (isMine) {
                    resolvedName = chatViewPage.selfName || "Me";
                } else if (!chatViewPage.isGroup && chatViewPage.threadName.length > 0) {
                    // 1-1 thread: Zalo's wire "dName" on an incoming message is
                    // unreliable (confirmed on-device carrying OUR OWN name
                    // instead of the sender's) — threadName is the contact's
                    // real name from their profile, not from the message wire.
                    resolvedName = chatViewPage.threadName;
                } else if (chatViewPage.isGroup) {
                    // Group: same wire-dName unreliability, confirmed on-device
                    // in groups too (not just 1-1 as first assumed) — look the
                    // real sender up by uid in zService's member-name cache
                    // (built from getmg-v2's currentMems, not the message wire).
                    var memName = zService.memberDisplayName(senderId || "");
                    resolvedName = (memName && memName.length > 0) ? memName : (rawDName || "Unknown");
                } else {
                    resolvedName = rawDName || "Unknown";
                }
                chatViewPage.pendingReplyMsgId      = msgId || "";
                chatViewPage.pendingReplyCliMsgId   = cliMsgId || "";
                chatViewPage.pendingReplyOwnerId    = senderId || "";
                chatViewPage.pendingReplySenderName = resolvedName;
                chatViewPage.pendingReplyContent    = content || "";
                chatViewPage.pendingReplyMsgType    = msgType || 0;
                chatViewPage.pendingReplyTs         = String(ts || "");
                sendAction.enabled = (inputField.text.trim().length > 0 || chatViewPage.pendingReplyMsgId.length > 0);
                inputField.requestFocus();
            }
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
                        // The yellow-background diagnostic (see git history) proved
                        // this: even with dividerVisible:false and topPadding:0 here,
                        // and even with every inner Container's height/y confirmed
                        // correct via layoutFrame diagnostics, a real gray gap (the
                        // Page's own background, not any Container this file draws)
                        // still cut across the FULL WIDTH of the row at every boundary
                        // between two CustomListItems. That location — outside every
                        // Container we control, but between rows — is exactly where
                        // CustomListItem's built-in divider reserves space even when
                        // it isn't painted. There's no direct QML property to zero out
                        // that reserved space. Forcing rowRoot's own preferredHeight to
                        // exactly match its content's real measured height overrides
                        // whatever extra room CustomListItem was leaving beyond that
                        // content for its (invisible but still-reserved) divider.
                        preferredHeight: rowLUH.layoutFrame.height > 0 ? rowLUH.layoutFrame.height : -1
                        // (Reverted a diagnostic "background: Color.create(...)" that
                        // was here — CustomListItem has no `background` property, same
                        // class of error documented for ActionItem/visible right below:
                        // it extends UIObject-level API, not Control, so this crashed
                        // the ENTIRE QML document on load with "Cannot assign to
                        // non-existent property background" and made every ChatView
                        // tap fail. Lesson: CustomListItem's 3 stylable surfaces are
                        // highlightAppearance, dividerVisible, and its `content` — no
                        // raw background paint. Any future "make the row itself a
                        // color" diagnostic needs to go on the content Container
                        // instead (the ones already declared just below/inside).

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
                                    onTriggered: {
                                        rowRoot.ListItem.view.doReply(ListItemData.msgId, ListItemData.cliMsgId,
                                            ListItemData.senderId, rowRoot.mine, ListItemData.dName,
                                            ListItemData.content, ListItemData.msgType, ListItemData.ts);
                                    }
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
                                    onTriggered: {
                                        rowRoot.ListItem.view.doReply(ListItemData.msgId, ListItemData.cliMsgId,
                                            ListItemData.senderId, rowRoot.mine, ListItemData.dName,
                                            ListItemData.content, ListItemData.msgType, ListItemData.ts);
                                    }
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

                        // Reply/quote: true when this row is a reply to an earlier message
                        // (quoteMsgId populated by dbSaveMessage/WS parsing — see doReply()/
                        // sendMessageQuote() and the WS quote-object parsing in
                        // ZaloService_WebSocket.cpp). Drives the separate dark quote-strip
                        // block rendered above the message text below.
                        property bool hasQuote: !!(ListItemData.quoteMsgId && ListItemData.quoteMsgId.length > 0)

                        // Same "wire dName/quoteSenderName can be wrong" issue as
                        // otherDisplayName below, but for the person being QUOTED —
                        // which isn't necessarily "the other party": replying to your
                        // own earlier message quotes yourself. quoteOwnerId (persisted
                        // alongside quoteMsgId — see sendMessageQuote()'s qmsgOwner and
                        // the WS quote.ownerId parsing) tells us which case this is;
                        // ListItemData.selfUid is this device's own uid (already exposed
                        // on ListView for the mine/theirs bubble-side logic elsewhere).
                        property bool quoteIsMine: !!(ListItemData.quoteOwnerId && ListItemData.quoteOwnerId === ListItem.view.selfUidProxy)
                        property string quoteMemberName: ListItem.view.isGroupChat
                            ? ListItem.view.memberDisplayNameProxy(ListItemData.quoteOwnerId || "")
                            : ""
                        property string quoteSenderResolved: rowRoot.quoteIsMine
                            ? (ListItem.view.selfNameProxy || "Me")
                            : ((!ListItem.view.isGroupChat && ListItem.view.threadNameProxy.length > 0)
                               ? ListItem.view.threadNameProxy
                               : ((ListItem.view.isGroupChat && rowRoot.quoteMemberName.length > 0)
                                  ? rowRoot.quoteMemberName
                                  : (ListItemData.quoteSenderName || "Unknown")))

                        // Zalo's WS payload has a quirk on incoming messages: the
                        // "dName" field is not reliably the sender's own name —
                        // confirmed from a device log where a message actually sent
                        // by another person carried dName="Berrylife" (OUR OWN
                        // name) instead of theirs. Confirmed in BOTH 1-1 and group
                        // threads (not just 1-1, as first assumed) — dbSaveMessage()
                        // persists whatever the wire sent, so this is wrong both
                        // live and after reload.
                        // - 1-1: there's only one possible "other" person, so
                        //   chatViewPage.threadName (from the contact's profile,
                        //   not the message wire) is always correct.
                        // - Group: every member needs their own per-message name,
                        //   which threadName can't provide — memberDisplayNameProxy
                        //   looks the sender up in m_memberNames (built from
                        //   getmg-v2's currentMems, a reliable per-member source
                        //   completely separate from the message wire).
                        // If neither source has an answer (e.g. group details
                        // haven't loaded this session yet), falls back to the
                        // wire dName rather than showing nothing.
                        property string otherMemberName: ListItem.view.isGroupChat
                            ? ListItem.view.memberDisplayNameProxy(ListItemData.senderId || "")
                            : ""
                        property string otherDisplayName: (!ListItem.view.isGroupChat && ListItem.view.threadNameProxy.length > 0)
                            ? ListItem.view.threadNameProxy
                            : ((ListItem.view.isGroupChat && rowRoot.otherMemberName.length > 0)
                               ? rowRoot.otherMemberName
                               : (ListItemData.dName || "Unknown"))

                        // Yellow highlight: true either while this row is the active
                        // in-chat search match (isCurrentSearchMatch, declared above) or
                        // while it's the target of a "jump to pinned message" tap (see
                        // chatViewPage.jumpHighlightMsgId, set by the pinboard bar's
                        // scrollToMsgIndex+highlight call and cleared after a short delay).
                        property bool isJumpHighlighted: ListItem.view.jumpHighlightMsgId.length > 0
                                                          && ListItem.view.jumpHighlightMsgId === (ListItemData.msgId || "")
                        property bool isHighlighted: rowRoot.isCurrentSearchMatch || rowRoot.isJumpHighlighted

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
                                            + " y=" + layoutFrame.y
                                            + " height=" + layoutFrame.height
                                            + " index=" + ListItem.indexPath[0]);
                                    }
                                }
                            ]

                        Container {
                            preferredWidth: rowRoot.mine ? 18 : 60
                            minWidth:       rowRoot.mine ? 18 : 60
                            maxWidth:       rowRoot.mine ? 18 : 60
                        }

                        Container {
                            id: bubbleWrap
                            layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                            // Reverted the nine-patch bubble-PNG rendering
                            // attempt (DockLayout + ImageView using
                            // rowRoot.bubbleImage) — on device the bubble
                            // PNGs stretched into a blurry white/blue smear
                            // instead of a clean bubble shape. These source
                            // images are mostly transparent/very thin-
                            // bordered, so force-scaling them up to fill an
                            // entire row is the wrong technique for this
                            // asset set regardless of the ScalingMethod
                            // used. Back to the flat solid-color fill that
                            // was there originally. The real "gap between
                            // messages 1-2 and 3-4" bug is a SEPARATE issue
                            // from bubble rendering — grouped/bubblePos/
                            // layoutFrame height all already compute
                            // correctly per the diagnostics (all 4 rows
                            // here are grp=true past the first, all
                            // "middle" rows measure height=55), so the gap
                            // is coming from somewhere else — being
                            // investigated separately rather than papered
                            // over here.
                            background: rowRoot.isHighlighted
                                ? Color.create("#fff3b0")
                                : (rowRoot.isDark
                                    ? (rowRoot.mine ? Color.create("#1e3a5f") : Color.create("#2a2a2a"))
                                    : Color.White)

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
                                          : rowRoot.otherDisplayName
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
                                              ? "<html>" + rowRoot.ListItem.view.highlightMatchesProxy(rowRoot.recalledOriginal, rowRoot.searchQuery, rowRoot.isCurrentSearchMatch ? "#ff9800" : "#ffeb3b") + "</html>"
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

                                // Reply/quote block — separate visual chunk sitting above the
                                // actual message text, exactly like the photo-attachment bubble
                                // is its own chunk rather than inline with text. Background is
                                // darker than the bubble itself and colored by WHOSE bubble
                                // this is (mine=gray strip color, theirs=blue strip color — the
                                // same two colors the existing bottom accent-strip Container
                                // already uses for mine/theirs, so the reply block visually
                                // matches that established color language instead of inventing
                                // a third color). Tapping it jumps to + highlights the original
                                // quoted message (chatViewPage.jumpToMessage()).
                                Container {
                                    visible: !rowRoot.recalled && rowRoot.hasQuote
                                    horizontalAlignment: HorizontalAlignment.Fill
                                    background: rowRoot.isDark
                                        ? (rowRoot.mine ? Color.create("#3a3a3a") : Color.create("#1c3450"))
                                        : (rowRoot.mine ? Color.create("#d9d9d9") : Color.create("#cfe3fa"))
                                    topPadding: 6; bottomPadding: 6; leftPadding: 8; rightPadding: 8
                                    bottomMargin: 4

                                    gestureHandlers: [
                                        TapHandler {
                                            onTapped: {
                                                rowRoot.ListItem.view.jumpToMessageProxy(ListItemData.quoteMsgId || "");
                                            }
                                        }
                                    ]

                                    Label {
                                        text: rowRoot.quoteSenderResolved
                                        multiline: false
                                        textStyle {
                                            fontSize:   FontSize.XSmall
                                            fontWeight: FontWeight.Bold
                                            color: rowRoot.mine
                                                ? (rowRoot.isDark ? Color.create("#cccccc") : Color.create("#444444"))
                                                : (rowRoot.isDark ? Color.create("#8ec2ff") : Color.create("#0073BC"))
                                        }
                                        topMargin: 0; bottomMargin: 0
                                    }
                                    Label {
                                        text: (ListItemData.quoteMsgType === 2 || ListItemData.quoteMsgType === "2")
                                              ? "[Photo]" : (ListItemData.quoteContent || "")
                                        multiline: false
                                        textStyle {
                                            fontSize: FontSize.XSmall
                                            color: rowRoot.isDark ? Color.create("#bbbbbb") : Color.create("#555555")
                                        }
                                        topMargin: 0; bottomMargin: 0
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
                                            return "<html>" + rowRoot.ListItem.view.highlightMatchesProxy(raw, rowRoot.searchQuery, hlColor) + "</html>";
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
                            //
                            // NOT using visible: false to hide this anymore.
                            // visible:false frequently does NOT collapse a
                            // Container's reserved layout space in Cascades/
                            // Qt4-era UI frameworks — the element stops being
                            // painted but its preferredHeight can still get
                            // counted by the parent's layout pass. That matches
                            // exactly what device logs showed: a row that was
                            // briefly "bottom" (strip visible, taller) when it
                            // first arrived, then reclassified to "middle"
                            // moments later once more messages arrived and
                            // pushed it mid-cluster (strip should disappear),
                            // never got a second "row layoutFrame CHANGED" log
                            // line — its height/space from the strip never got
                            // released. Setting height to 0 directly (instead
                            // of toggling visible) forces the reserved space
                            // itself to go to zero, not just the painting.
                            Container {
                                horizontalAlignment: HorizontalAlignment.Fill
                                property bool showStrip: rowRoot.bubblePos === "bottom" || rowRoot.bubblePos === "full"
                                preferredHeight: showStrip ? ui.du(0.8) : 0
                                minHeight: showStrip ? ui.du(0.8) : 0
                                maxHeight: showStrip ? ui.du(0.8) : 0
                                visible: showStrip
                                background: rowRoot.mine ? Color.create("#999999") : Color.create("#0073BC")
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

        // Reply staging bar — shown while a reply is pending, above the input.
        // Same visual language as attachPreviewBar (same background colors,
        // same [X] cancel pattern) so the two "something is queued to send"
        // states read as one consistent affordance rather than two different
        // UI languages. Layout: [X]  [colored quote strip]  [sender + snippet]
        Container {
            id: replyPreviewBar
            visible: chatViewPage.pendingReplyMsgId.length > 0
            horizontalAlignment: HorizontalAlignment.Fill
            background: chatViewPage.isDark ? Color.create("#1e2a38") : Color.create("#dce8f5")
            topPadding:    ui.du(1.0)
            bottomPadding: ui.du(1.0)
            leftPadding:   ui.du(1.0)
            rightPadding:  ui.du(1.5)
            layout: StackLayout { orientation: LayoutOrientation.LeftToRight }

            // X — cancel pending reply
            Container {
                verticalAlignment: VerticalAlignment.Center
                preferredWidth:  ui.du(6)
                preferredHeight: ui.du(6)
                layout: DockLayout {}
                gestureHandlers: [
                    TapHandler {
                        onTapped: {
                            chatViewPage.pendingReplyMsgId      = "";
                            chatViewPage.pendingReplyCliMsgId   = "";
                            chatViewPage.pendingReplyOwnerId    = "";
                            chatViewPage.pendingReplySenderName = "";
                            chatViewPage.pendingReplyContent    = "";
                            chatViewPage.pendingReplyMsgType    = 0;
                            chatViewPage.pendingReplyTs         = "";
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

            // Colored accent strip — echoes the quote-block strip rendered
            // inside the bubble itself, so the preview and the eventual sent
            // bubble visually match.
            Container {
                verticalAlignment: VerticalAlignment.Fill
                preferredWidth: ui.du(0.5)
                minWidth: ui.du(0.5); maxWidth: ui.du(0.5)
                leftMargin: ui.du(0.5); rightMargin: ui.du(1.0)
                background: Color.create("#2575fc")
            }

            Container {
                layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                verticalAlignment: VerticalAlignment.Center

                Label {
                    text: chatViewPage.pendingReplySenderName
                    multiline: false
                    textStyle {
                        fontSize: FontSize.Small
                        fontWeight: FontWeight.Bold
                        color: Color.create("#2575fc")
                    }
                    topMargin: 0; bottomMargin: 0
                }
                Label {
                    text: (chatViewPage.pendingReplyMsgType === 2 || chatViewPage.pendingReplyMsgType === "2")
                          ? "[Photo]" : chatViewPage.pendingReplyContent
                    multiline: false
                    textStyle {
                        fontSize: FontSize.Small
                        color: chatViewPage.isDark ? Color.create("#cfd8e3") : Color.create("#444444")
                    }
                    topMargin: 0; bottomMargin: 0
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
        // Group Board: shows all pinned messages/notes/polls in this group
        // (GroupBoardSheet.qml, pushed via the groupBoardRequested flag —
        // same "flip a bool, the owning NavigationPane watches and pushes"
        // pattern qmRequested already uses just below in this same file,
        // since ChatView itself can't push into its own parent Nav
        // directly). 1-1 threads have no group board concept server-side,
        // hence disabled (not hidden — same convention "Block user"/"Leave
        // group" already use above for their own thread-type restrictions)
        // outside of groups.
        ActionItem {
            title: "Group board"
            imageSource: "asset:///images/ChatView/ic_sb_notes.png"
            ActionBar.placement: ActionBarPlacement.InOverflow
            enabled: chatViewPage.isGroup
            onTriggered: { chatViewPage.groupBoardRequested = true; }
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
            chatViewPage.rebuildGroups(true);
            zService.sendPhoto(chatViewPage.threadId, imgPath, chatViewPage.isGroup, caption);
            return;
        }

        var isReply = chatViewPage.pendingReplyMsgId.length > 0;
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
        if (isReply) {
            placeholder.quoteMsgId      = chatViewPage.pendingReplyMsgId;
            placeholder.quoteContent    = (chatViewPage.pendingReplyMsgType === 2 || chatViewPage.pendingReplyMsgType === "2")
                                           ? "[Photo]" : chatViewPage.pendingReplyContent;
            placeholder.quoteSenderName = chatViewPage.pendingReplySenderName;
            placeholder.quoteMsgType    = chatViewPage.pendingReplyMsgType;
            placeholder.quoteOwnerId    = chatViewPage.pendingReplyOwnerId;
        }
        msgModel.append(placeholder);
        chatViewPage.rebuildGroups(true);

        chatViewPage.pendingMsg = txt;

        if (isReply) {
            // qmsgType sent to the server is Zalo's own client-message-type code
            // (1=text, 32=photo — see zca-js getClientMessageType()), which is
            // NOT the same numbering as our local msgType (1=text, 2=photo).
            var qServerType = (chatViewPage.pendingReplyMsgType === 2 || chatViewPage.pendingReplyMsgType === "2") ? 32 : 1;
            var qContent = (chatViewPage.pendingReplyMsgType === 2 || chatViewPage.pendingReplyMsgType === "2")
                           ? "[Photo]" : chatViewPage.pendingReplyContent;
            zService.sendMessageQuote(chatViewPage.threadId, txt, chatViewPage.isGroup,
                chatViewPage.pendingReplyMsgId, chatViewPage.pendingReplyCliMsgId,
                chatViewPage.pendingReplyOwnerId, qContent, qServerType, chatViewPage.pendingReplyTs);
            chatViewPage.pendingReplyMsgId      = "";
            chatViewPage.pendingReplyCliMsgId   = "";
            chatViewPage.pendingReplyOwnerId    = "";
            chatViewPage.pendingReplySenderName = "";
            chatViewPage.pendingReplyContent    = "";
            chatViewPage.pendingReplyMsgType    = 0;
            chatViewPage.pendingReplyTs         = "";
        } else {
            zService.sendMessage(chatViewPage.threadId, txt, chatViewPage.isGroup);
        }
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

    // See the detailed race-condition comment inside rebuildGroups() below for
    // why this exists. Call this before ANY direct msgModel.append()/clear()
    // (not just before rebuildGroups() itself) so a pending deferred flush
    // from an earlier rebuildGroups() call always lands, in order, before
    // new data goes in — otherwise a message appended while msgModel is
    // sitting cleared-but-not-yet-reflushed can end up ordered before
    // earlier messages once the flush finally fires, or (worse, the
    // originally reported bug) get silently overwritten and lost entirely
    // if the append is itself followed by another deferring rebuildGroups().
    function flushPendingRebuild() {
        if (rebuildFlushTimer.running || rebuildFlushTimer.pendingItems) {
            rebuildFlushTimer.stop();
            if (rebuildFlushTimer.pendingItems) {
                msgModel.append(rebuildFlushTimer.pendingItems);
                rebuildFlushTimer.pendingItems = null;
            }
            rebuildFlushTimer.pendingScroll = false;
        }
    }

    function rebuildGroups(scrollAfter) {
        // Data-loss race fix: rebuildGroups() below can defer its own update
        // onto rebuildFlushTimer (msgModel.clear() now, real content
        // re-appended on the NEXT event loop turn — see the big comment
        // further down explaining why that deferral exists). If a SECOND
        // message arrives and calls onNewMessage -> msgModel.append() ->
        // rebuildGroups() again before that timer fires, msgModel is still
        // sitting empty from the first call's clear(). The second call's own
        // "var size = msgModel.size()" then sees only whatever it just
        // appended, and if IT also needs to defer, its pendingItems
        // (containing only the second message) overwrites the first call's
        // still-pending pendingItems (containing everything, including the
        // first message) — the first message is silently gone forever once
        // the timer finally fires. Confirmed on-device: a reply sent right
        // after an incoming photo made that photo disappear.
        //
        // Fix: if a flush is already pending when we're called, apply it
        // synchronously RIGHT NOW (stop the timer, append its pendingItems
        // immediately) before doing anything else. This closes the race
        // window entirely — by the time this function reads msgModel.size()
        // below, any earlier deferred update has already landed for real,
        // so nothing gets appended into a still-cleared model or overwrites
        // a not-yet-flushed pendingItems.
        chatViewPage.flushPendingRebuild();

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
        // isLocal rows (still-unconfirmed "local_"/"local_img_"/"local_file_"
        // placeholders) carry a DEVICE-clock ts; confirmed rows carry a
        // SERVER-clock ts. Bump local ones by clockOffsetMs so every ts being
        // compared is on the same clock — see clockOffsetMs declaration above
        // for why this is necessary.
        function toMs(ts, isLocal) {
            var n = (ts || 0) * 1;
            if (n > 0 && n < 1e12) n *= 1000;
            if (isLocal && chatViewPage.clockOffsetSet) n += chatViewPage.clockOffsetMs;
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
            var curId  = cur.msgId  || "";
            var prevId = prev ? (prev.msgId || "") : "";
            var nextId = next ? (next.msgId || "") : "";
            var curLocal  = curId.indexOf("local_")  === 0;
            var prevLocal = prevId.indexOf("local_") === 0;
            var nextLocal = nextId.indexOf("local_") === 0;

            var curTs  = toMs(cur.ts,               curLocal);
            var prevTs = toMs(prev ? prev.ts : null, prevLocal);
            var nextTs = toMs(next ? next.ts : null, nextLocal);
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
            // clear()+append() alone updates the ArrayDataModel's data, but the
            // delegate-binding diagnostics above proved grouped/bubblePos were
            // ALREADY arriving correctly at the delegate for the affected rows
            // even while the gap still showed — meaning the ListView itself was
            // reusing pooled Control instances whose already-measured height
            // Cascades wasn't recomputing off the padding-only change.
            //
            // Several approaches to forcing a hard remeasure were tried and
            // rejected — see git history on this block for the details
            // (empty-dataModel bounce via a second fixed ArrayDataModel done
            // synchronously, deferred onto a Timer, via a fresh per-call
            // ArrayDataModel, an unverified detachAttachedObjects() call, and
            // toggling listItemComponents' `type` per row). Each relied on an
            // unconfirmed Cascades API, broke msgModel's fixed QML id being
            // read/written from ~50 other call sites in this file, or (the
            // type-toggle) risked undefined/crash behavior from pointing rows
            // at a type with no matching ListItemComponent.
            //
            // The dataModel=null;dataModel=msgModel bounce (previous attempt,
            // see git history) turned out to be a no-op in practice: the
            // delegate-binding diagnostics (grouped-binding/bubblePos-binding)
            // fire correctly, but rowLUH's layoutFrameChanged never fires
            // again for a row whose bubblePos changed without its INDEX
            // changing — confirmed by a real device log capture where row
            // 338345 went bubblePos "full"->"top" (a real padding/height
            // change) with a bubblePos-binding log line proving the QML
            // property updated, but no matching "row layoutFrame CHANGED"
            // line, i.e. Cascades kept the row's originally-measured height.
            // Reassigning the SAME msgModel object back to dataModel
            // synchronously never gives Cascades an empty-model layout pass
            // to actually drop its pooled/cached item heights — it's still
            // the same model reference before and after, nothing to diff.
            //
            // Deferring the re-append to the NEXT event loop turn (via a
            // zero-interval Timer) instead gives Cascades a real intervening
            // layout pass over an EMPTY dataModel in between clear() and
            // append(), which is what actually forces it to tear down and
            // discard the pooled item Controls instead of recycling their
            // stale measured heights back onto the new data.
            // TEMP DIAGNOSTIC — investigating the "gap after delete+reopen+resend"
            // bug report. Dumps every row's msgId/sender/content-length/grouped/
            // bubblePos so a fresh log capture shows definitively whether there's
            // a stray zero-content/ghost row still occupying a model slot near the
            // deleted message, or whether bubblePos/grouped themselves are wrong.
            // Reads from items[] (the just-computed values) rather than
            // msgModel.value() here, since msgModel is deferred-empty until
            // rebuildFlushTimer fires on the next event loop turn.
            // Safe to delete once that bug is found — this is not a fix by itself.
            var dbgLine = "[Zalo QML] rebuildGroups: size=" + size;
            for (var di = 0; di < size; di++) {
                var dv = items[di];
                dbgLine += " | [" + di + "] id=" + String(dv.msgId).slice(-6)
                    + " mine=" + chatViewPage.normMine(dv.isMine)
                    + " sender=" + (dv.senderId || "")
                    + " len=" + ((dv.content || "").length)
                    + " grp=" + dv.grouped + " pos=" + dv.bubblePos
                    + " ts=" + dv.ts;
            }
            console.log(dbgLine);

            msgModel.clear();
            rebuildFlushTimer.pendingItems = items;
            rebuildFlushTimer.pendingScroll = !!scrollAfter;
            rebuildFlushTimer.start();
            return;
        } else {
            for (var i2 = 0; i2 < size; i2++) {
                msgModel.replace(i2, items[i2]);
            }
        }
        if (scrollAfter) {
            msgList.scrollToPosition(ScrollPosition.End, ScrollAnimation.Smooth);
        }

        // TEMP DIAGNOSTIC — same dump as above, for the cheap-path (no
        // layout change) branch, where msgModel already holds items[] via
        // the in-place replace() calls just above.
        {
            var dbgLine2 = "[Zalo QML] rebuildGroups: size=" + size;
            for (var di2 = 0; di2 < size; di2++) {
                var dv2 = msgModel.value(di2);
                dbgLine2 += " | [" + di2 + "] id=" + String(dv2.msgId).slice(-6)
                    + " mine=" + chatViewPage.normMine(dv2.isMine)
                    + " sender=" + (dv2.senderId || "")
                    + " len=" + ((dv2.content || "").length)
                    + " grp=" + dv2.grouped + " pos=" + dv2.bubblePos
                    + " ts=" + dv2.ts;
            }
            console.log(dbgLine2);
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

        // Clears jumpHighlightMsgId a couple seconds after jumpToMessage()
        // sets it, so the yellow "just jumped here" highlight is transient
        // (matches the feel of the search-match highlight) instead of
        // sticking on the row forever.
        Timer {
            id: jumpHighlightTimer
            interval: 2000
            repeat: false
            onTriggered: { chatViewPage.jumpHighlightMsgId = ""; }
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
                // See flushPendingRebuild()'s comment for why this must run
                // before any direct msgModel read/append in a handler that
                // can fire from an async C++ signal.
                chatViewPage.flushPendingRebuild();

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
                    chatViewPage.rebuildGroups(true);
                }
            }

            onMessageSent: {
                if (threadId !== chatViewPage.threadId) return;
                chatViewPage.flushPendingRebuild();
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
                // Primary fix point for the reported "reply right after a
                // photo makes the photo vanish" bug — see flushPendingRebuild()'s
                // comment. This handler is exactly what fires back-to-back for
                // two messages arriving close together (an incoming photo, then
                // an outgoing reply's confirmation), and it does its own direct
                // msgModel.append() further down without going through
                // rebuildGroups() first — so without this, a pending deferred
                // flush from the PREVIOUS message could still be sitting on
                // rebuildFlushTimer when this one's append() runs.
                chatViewPage.flushPendingRebuild();

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
                    // This is the server's confirmation of one of our own outgoing
                    // messages — cliMsgId is the device-clock ts its "local_"
                    // placeholder was created with, msg.ts is the real server ts.
                    // See updateClockOffset()/clockOffsetMs for why this is needed.
                    chatViewPage.updateClockOffset(msg.cliMsgId, msg.ts);

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
                            chatViewPage.rebuildGroups(true);
                        }
                        return;
                    }
                    msgModel.append(msg);
                }

                chatViewPage.rebuildGroups(true);

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

            // Companion to the C++-side fix in ZaloService_WebSocket.cpp's
            // m_seenMsgIds branch: an outgoing message saved via the HTTP
            // send-confirm path (device-clock ts, since no server ts is
            // available yet at that point) has just had its DB row corrected
            // to the real server ts once the WS echo arrived. If this
            // message happens to be currently loaded in msgModel, patch its
            // ts in place here too and re-run grouping — otherwise the
            // in-memory row would keep the stale device-clock ts (and the
            // gap it causes) until the thread is closed and reopened, even
            // though the DB itself is now correct.
            onMessageTsCorrected: {
                if (threadId !== chatViewPage.threadId) return;
                chatViewPage.applyTsCorrection(msgId, newTs);
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
                    // Clears any pending reply first: the two staging bars are
                    // mutually exclusive (same reasoning as doReply() clearing
                    // a pending photo when a reply is started).
                    var fname = path.substring(path.lastIndexOf('/') + 1);
                    chatViewPage.pendingReplyMsgId      = "";
                    chatViewPage.pendingReplyCliMsgId   = "";
                    chatViewPage.pendingReplyOwnerId    = "";
                    chatViewPage.pendingReplySenderName = "";
                    chatViewPage.pendingReplyContent    = "";
                    chatViewPage.pendingReplyMsgType    = 0;
                    chatViewPage.pendingReplyTs         = "";
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
                    chatViewPage.rebuildGroups(true);
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
