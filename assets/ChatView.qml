import bb.cascades 1.4
import bb.cascades.pickers 1.0
import bb.system 1.0
import QtQuick 1.0

Page {
    id: chatViewPage

    property string threadId:    ""
    property string threadName:  ""
    property bool   isGroup:     false
    // TODO: no group-role data source exists yet, so this is hardcoded false —
    // "Delete for everyone" stays hidden until admin/owner data is available
    property bool   isCurrentUserAdminOrOwner: false
    property string avatarUrl:   ""
    property string selfName:    ""
    property string pendingMsg:  ""
    property bool   initialized: false
    property bool   emojiPanelOpen: false
    property variant emojiPanelRef: null
    property string pendingAttachPath: ""
    property string pendingAttachName: ""
    // Set by doReply() when tapping "Reply" on a bubble, cleared on send/cancel
    // Mutually exclusive with pendingAttachPath — starting one clears the other
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
    // Same flip-a-bool-and-let-the-owning-Nav-react pattern as qmRequested
    // (see ChatsTab.qml/GroupsTab.qml) — ChatView can't push into its own NavigationPane
    property bool   groupBoardRequested: false
    // Backs the PinboardBar strip below the header. Refreshed on thread open and
    // after any pin/note/poll action — see loadBoardItems() and groupBoardReady below
    property variant boardItems: []
    property variant pendingImageUpdates: ([])
    property bool   pageVisible: false
    property bool   isDark: app.getDarkTheme()
    property bool   showRecalledMessages: app.getShowRecalledMessages()
    property bool   searchVisible: false
    property string searchText: ""
    property variant searchMatches: []   // indices into msgModel that contain the current query
    property int      searchMatchPos: -1 // which entry in searchMatches is currently focused
    // Set briefly to jump to a message (pinned entry tap, reply quote tap) — the
    // delegate highlights the matching row yellow, jumpHighlightTimer clears it after a few seconds
    property string jumpHighlightMsgId: ""

    // Device clock can drift hours from the server clock. Outgoing messages start as
    // "local_" placeholders timestamped by the device, then get swapped for the server-
    // timestamped row once confirmed — mixing the two in rebuildGroups()'s 5-minute
    // grouping window can wrongly split bubbles sent seconds apart. clockOffsetMs
    // (server ts - device ts) is measured once and applied to placeholders before comparing.
    property real clockOffsetMs: 0
    property bool clockOffsetSet: false

    titleBar: TitleBar {
        // Sticky keeps the header pinned while scrolling, at the cost of sometimes
        // intercepting touches meant for controls in the title bar (e.g. search field)
        scrollBehavior: TitleBarScrollBehavior.Sticky
        kind: TitleBarKind.FreeForm
        kindProperties: FreeFormTitleBarKindProperties {
            content: Container {
                background: Color.create("#2575fc")
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment:   VerticalAlignment.Fill
                layout: DockLayout {}

                // Normal header: avatar, thread name, call buttons — hidden while search is active
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
                    // Fixed-width spacer to push the icons off the right edge — a plain
                    // rightMargin isn't enough since spaceQuota reclaims that space first
                    Container {
                        preferredWidth: ui.du(0.2)
                    }
                }

                // In-chat search, browser-find style: matches highlight yellow inline
                // (see rowRoot.searchHtml() below) and Prev/Next scroll between them
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
                        textStyle { color: Color.create("#FFFFFF") }
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
        var items = [];
        var found = false;
        for (var j = 0; j < size; j++) {
            var d = msgModel.value(j);
            if (!found && (d.msgId || "") === msgId) {
                d.localImage = localPath;
                if (imgWidth  > 0) d.imgWidth  = imgWidth;
                if (imgHeight > 0) d.imgHeight = imgHeight;
                found = true;
            }
            items.push(d);
        }
        if (!found) return;
        // Plain msgModel.replace(j, d) doesn't make the ImageView delegate re-bind
        // imageSource on this Cascades version — a broken/blank image stays stuck even
        // after the data is fixed. Full clear() + deferred re-append (via rebuildFlushTimer)
        // forces Cascades through an empty layout pass so it drops the stale pooled Controls.
        msgModel.clear();
        rebuildFlushTimer.pendingItems = items;
        rebuildFlushTimer.pendingScroll = false;
        rebuildFlushTimer.start();
    }

    // Sticker counterpart of applyImageUpdate above — same model-patch-then-
    // rebuild approach, applied to every row whose content references this
    // stickerId (a sticker can appear more than once in the same thread,
    // unlike a photo/video message which is inherently 1:1 with its msgId).
    // stickerBubble binds ListItemData.stickerLocalPath as a plain property
    // (same pattern as selfUidProxy near the top of this file) instead of
    // calling zService itself or holding Connections in its own
    // attachedObjects — calling zService directly from deep inside a nested
    // delegate Container's Component.onCompleted was unreliable on device
    // ("Can't find variable: zService", "Cannot assign to non-existent
    // property onStickerReady"); doDownloadSticker() on msgList (this
    // Component, much closer to the Page root) is the call site, and this
    // function is what threads the async result back down to the row.
    function applyStickerUpdate(stickerId, localPath) {
        chatViewPage.flushPendingRebuild();
        var size = msgModel.size();
        if (size === 0) return;
        var items = [];
        var found = false;
        var needle = '"stickerId":' + stickerId;
        for (var j = 0; j < size; j++) {
            var d = msgModel.value(j);
            if ((d.msgType === 5 || d.msgType === "5") && (d.content || "").indexOf(needle) >= 0) {
                d.stickerLocalPath = localPath;
                found = true;
            }
            items.push(d);
        }
        if (!found) return;
        msgModel.clear();
        rebuildFlushTimer.pendingItems = items;
        rebuildFlushTimer.pendingScroll = false;
        rebuildFlushTimer.start();
    }

    // Measures clockOffsetMs from the first outgoing message with both a cliMsgId
    // (device-clock ts) and a confirmed server ts. Sanity-bounded to avoid a bogus
    // offset from a malformed cliMsgId.
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

    // "Delete for me": message vanishes from our view entirely, unlike recall which
    // stays as a placeholder. Only called for deletions we performed ourselves.
    function applyLocalDelete(msgId) {
        chatViewPage.flushPendingRebuild();
        var size = msgModel.size();
        for (var j = 0; j < size; j++) {
            var d = msgModel.value(j);
            if ((d.msgId || "") === msgId) {
                msgModel.removeAt(j);
                // Re-run grouping — removing a row changes bubblePos for its former
                // neighbors, which would otherwise be left with a stale accent strip
                rebuildGroups();
                return;
            }
        }
    }

    // Patches an already-loaded row's ts to the server value and re-runs grouping,
    // since a stale device-clock ts can throw off the 5-minute grouping window
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
                // Preserve the original content so it can still be shown when
                // "Show Recalled Messages" is enabled. Idempotency guard: the server
                // can redeliver the same recall event, so only capture the original
                // text the first time — otherwise a second call overwrites it with "".
                if (!d.recalledOriginalContent || d.recalledOriginalContent.length === 0) {
                    d.recalledOriginalContent = d.content || "";
                }
                d.content    = "";
                d.msgType    = 99;
                // localImage is left untouched so a recalled photo can still be shown
                // when "Show Recalled Messages" is enabled — the delegate decides whether to display it
                msgModel.replace(j, d);
                return;
            }
        }
    }

    // "Find in page" style: scans loaded messages for matches without hiding any.
    // rowRoot.searchHtml() highlights matches inline; gotoSearchMatch() scrolls to each
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

    // Jump to a message by id (pinned entry tap, reply quote tap) — scrolls it into
    // view and gives it the same yellow highlight as search matches, briefly
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

    // Refreshes boardItems (pin/note/poll items), feeds PinboardBar
    // No-op for 1-1 threads — no board concept exists there server-side
    function loadBoardItems() {
        if (!chatViewPage.isGroup || chatViewPage.threadId.length === 0) {
            chatViewPage.boardItems = [];
            return;
        }
        zService.fetchGroupBoard(chatViewPage.threadId, 1, 50);
    }

    // ---- Inline poll cards in msgModel (kind: "poll" rows) -----------------
    // Same vote-toggle logic as GroupBoardSheet.qml's doVoteOption, kept in sync
    // manually since there's no shared QML module to factor it into
    function doVotePollOption(pollId, optionId, allowMulti, options) {
        if (!pollId || pollId.length === 0 || !options) return;
        var newIds = [];
        if (allowMulti) {
            var alreadyVoted = false;
            for (var i = 0; i < options.length; i++) {
                if (options[i].optionId === optionId) alreadyVoted = !!options[i].voted;
            }
            for (var j = 0; j < options.length; j++) {
                var thisId = options[j].optionId;
                var voted = !!options[j].voted;
                if (thisId === optionId) { if (!alreadyVoted) newIds.push(thisId); }
                else if (voted) newIds.push(thisId);
            }
        } else {
            var currentlyVotedHere = false;
            for (var k = 0; k < options.length; k++) {
                if (options[k].optionId === optionId && options[k].voted) currentlyVotedHere = true;
            }
            if (!currentlyVotedHere) newIds = [optionId];
        }
        zService.voteGroupPoll(chatViewPage.threadId, pollId, newIds);
    }

    function openPollVoters(pollId) {
        if (!pollId || pollId.length === 0) return;
        pollVotersSheet.isDark = chatViewPage.isDark;
        pollVotersSheet.openFor(pollId);
    }

    function findPollRowIndex(pollId) {
        for (var i = 0; i < msgModel.size(); i++) {
            var r = msgModel.value(i);
            if (r && r.kind === "poll" && r.pollId === pollId) return i;
        }
        return -1;
    }

    function buildPollRow(item) {
        return {
            kind: "poll",
            msgId: "poll_" + item.id,
            pollId: item.id,
            ts: item.createTime || Date.now(),
            pollQuestion: item.title || "",
            pollOptions: item.options || [],
            pollAllowMulti: !!item.allowMultiChoices,
            pollCreatorId: item.creatorId || ""
        };
    }

    // Inserts a synthetic row at the position matching its ts among the other
    // chronological rows. Falls back to the end when nothing newer is found
    // (the common case for a poll created "now")
    function insertPollAtChronological(row) {
        var insertIdx = msgModel.size();
        for (var i = 0; i < msgModel.size(); i++) {
            var r = msgModel.value(i);
            var rts = r ? (r.latestTs || r.ts || 0) : 0;
            if (rts && rts > row.ts) { insertIdx = i; break; }
        }
        msgModel.insert(insertIdx, row);
    }

    // Called from groupBoardReady for every poll on the board. Never repositions
    // an already-known row (a board refresh isn't necessarily a vote) — only inserts
    // newly-seen polls at their chronological slot. bumpPollToBottom() below is what
    // actually moves a card, and only runs on an explicit vote.
    function upsertPollRow(item) {
        var idx = chatViewPage.findPollRowIndex(item.id);
        var row = chatViewPage.buildPollRow(item);
        if (idx >= 0) { msgModel.removeAt(idx); msgModel.insert(idx, row); }
        else chatViewPage.insertPollAtChronological(row);
    }

    // The one place that actually moves a poll card: removes it and re-appends it
    // at the bottom of msgModel with fresh option data, so a voted poll jumps to
    // the bottom like a new message. detail is only needed to seed a brand-new row.
    function bumpPollToBottom(pollId, updatedOptions, detail) {
        var idx = chatViewPage.findPollRowIndex(pollId);
        var row;
        if (idx >= 0) {
            row = msgModel.value(idx);
            msgModel.removeAt(idx);
            // Shallow clone so ArrayDataModel sees a distinct object — it compares
            // by reference for change notification on append/insert
            row = JSON.parse(JSON.stringify(row));
        } else if (detail) {
            row = {
                kind: "poll", msgId: "poll_" + pollId, pollId: pollId, ts: Date.now(),
                pollQuestion: detail.question || "", pollAllowMulti: !!detail.allowMultiChoices,
                pollCreatorId: detail.creator || ""
            };
        } else {
            return; // nothing to update and nothing to seed a new row with
        }
        row.pollOptions = updatedOptions || [];
        row.ts = Date.now();
        msgModel.append(row);
    }



    // Escapes HTML, wraps every case-insensitive match of `query` in a yellow
    // <span>. Returns plain text unchanged when there's no query.
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

        // Cancel any deferred rebuild flush left from the previous thread — applying
        // it here would leak the old thread's messages into this one's fresh model
        rebuildFlushTimer.stop();
        rebuildFlushTimer.pendingItems = null;
        rebuildFlushTimer.pendingScroll = false;

        msgModel.clear();
        zService.setActiveThread(chatViewPage.threadId, chatViewPage.isGroup);

        // Bulk-load every reaction for this thread in one call instead of one
        // query per message. Replaces reactionsByMsg entirely for the new thread.
        msgList.reactionsByMsg = zService.dbLoadThreadReactions(chatViewPage.threadId) || {};

        var cached = zService.dbLoadMessages(chatViewPage.threadId);
        if (cached && cached.length > 0) {
            var newCache = {};
            for (var i = 0; i < cached.length; i++) {
                var c = cached[i];
                c.selfName = chatViewPage.selfName || "Me";
                c.isMine = (c.isMine === "true" || c.isMine === 1 || c.isMine === true);
                if (c.msgId) newCache[c.msgId] = c.isMine;
                // Compute pills up front, before the row is in msgModel, to avoid a
                // flash of "no reactions" before a second pass
                c.reactions = c.msgId ? msgList.summarizePills(c.msgId) : [];
                msgModel.append(c);

                var isPhoto = (c.msgType === 2 || c.msgType === "2");
                var hasLocal = (c.localImage && c.localImage.length > 0);
                if (isPhoto && !hasLocal && c.msgId) {
                    var photoUrl1 = chatViewPage.extractPhotoUrl(c.content || "");
                    if (photoUrl1.length > 0)
                        zService.downloadImageMessage(c.msgId, photoUrl1, chatViewPage.threadId);
                }
                var isLink = (c.msgType === 6 || c.msgType === "6");
                if (isLink && !hasLocal && c.msgId) {
                    // Same reasoning as the photo branch above — link thumbnail is a
                    // remote https:// URL that Cascades' ImageView can't load directly,
                    // so route it through the download-to-local-file pipeline too.
                    var linkThumbUrl1 = chatViewPage.extractJsonField(c.content || "", "linkThumb");
                    if (linkThumbUrl1.length > 0)
                        zService.downloadImageMessage(c.msgId, linkThumbUrl1, chatViewPage.threadId);
                }
            }
            chatViewPage.dbIsMineCache = newCache;
            chatViewPage.rebuildGroups(true);
        }

        zService.fetchMessages(chatViewPage.threadId, chatViewPage.isGroup);
        chatViewPage.loadBoardItems();
    }

    onThreadIdChanged: {
        chatViewPage.initialized = false;
        chatViewPage.pageVisible = false;
        chatViewPage.pendingImageUpdates = [];
        msgModel.clear();
        chatViewPage.dbIsMineCache = {};
        chatViewPage.boardItems = [];
    }

    content: Container {
        layout: StackLayout { orientation: LayoutOrientation.TopToBottom }
        horizontalAlignment: HorizontalAlignment.Fill
        verticalAlignment:   VerticalAlignment.Fill
        background: chatViewPage.isDark ? Color.create("#1a1a1a") : Color.create("#d6d6d6")

        // Pinned-items strip (see PinboardBar.qml). Only relevant for groups —
        // boardItems stays [] for 1-1 threads so the bar collapses itself
        PinboardBar {
            id: pinboardBar
            items: chatViewPage.boardItems
            isDark: chatViewPage.isDark
            horizontalAlignment: HorizontalAlignment.Fill
            onItemTapped: {
                // Args match PinboardBar's `signal itemTapped(...)`
                if (boardType === "pin") {
                    chatViewPage.jumpToMessage(itemId);
                } else {
                    // Notes/polls fall back to the same "Group board" entry point,
                    // still under development
                    groupBoardUnderDevDialog.show();
                }
            }
            onMoreRequested: { groupBoardUnderDevDialog.show(); }
        }

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
            // Proxies for chatViewPage's functions — ListItemComponent delegates are a
            // separate Cascades scope, so calling chatViewPage.foo() directly from inside
            // one throws "ReferenceError: Can't find variable: chatViewPage" at runtime
            function highlightMatchesProxy(text, query, color) { return chatViewPage.highlightMatches(text, query, color); }
            function jumpToMessageProxy(msgId) { chatViewPage.jumpToMessage(msgId); }
            // Reliable uid->name lookup (built from getmg-v2's currentMems, not the
            // per-message wire dName field, which is unreliable). Returns "" if unknown.
            function memberDisplayNameProxy(uid) { return zService.memberDisplayName(uid); }
            function votePollOptionProxy(pollId, optionId, allowMulti, options) { chatViewPage.doVotePollOption(pollId, optionId, allowMulti, options); }
            function viewPollVotersProxy(pollId) { chatViewPage.openPollVoters(pollId); }
            function openGroupBoardProxy() { groupBoardUnderDevDialog.show(); }
            // Video download progress tracking for tap-to-play bubbles. Only one
            // video can be downloading at a time (see ZaloService::m_videoDownloadReply),
            // so a single msgId+percent pair is enough — the delegate for that msgId
            // binds videoBubble.vDownloading/vProgress off these two properties.
            property string pendingVideoOpenMsgId: ""
            property string videoProgressMsgId: ""
            property int    videoProgressPercent: 0
            function updateVideoBubbleProgress(msgId, percent, done) {
                if (done) {
                    msgList.videoProgressMsgId = "";
                    msgList.videoProgressPercent = 0;
                } else {
                    msgList.videoProgressMsgId = msgId;
                    msgList.videoProgressPercent = percent;
                }
            }
            // Upload-side counterpart — sendVideo() has no msgId yet while
            // uploading (only cliMsgId), so keyed by threadId instead; only
            // one video can be uploading per chat at a time, matching the
            // "local_video_..." placeholder that's showing "Sending..." at
            // that moment.
            property int uploadVideoPercent: 0
            property bool uploadVideoActive: false
            // sendFile() (document attachments) giờ dùng chung chunked-upload
            // pipeline với sendVideo() — có progress % thật qua
            // fileUploadProgress, tách riêng khỏi uploadVideoActive/Percent
            // để 1 chat gửi file không làm nhảy % của video đang gửi ở chat
            // khác (và ngược lại). Cùng lý do keyed-by-threadId như video:
            // chỉ 1 file gửi cùng lúc/thread.
            property bool uploadFileActive: false
            property int  uploadFilePercent: 0
            horizontalAlignment: HorizontalAlignment.Fill
            layoutProperties: StackLayoutProperties { spaceQuota: 1 }
            dataModel: msgModel
            bottomPadding: ui.du(1.5)
            flickMode: FlickMode.Momentum

            attachedObjects: [
                ArrayDataModel { id: msgModel },
                // Pairs with the clear()+deferred-append rebuild path below — the
                // zero-interval singleShot fires on the next event loop turn, giving
                // Cascades an empty layout pass so it drops stale pooled item heights
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

            // Bubble hold-menu actions, wired to individual functions rather than one
            // dispatcher so each can be tested independently. Copy/Share are implemented;
            // the rest are still console.log-only stubs.
            // isPhoto + localImage let Copy/Share use the actual image bytes for a photo
            // message instead of copying the raw {"normalUrl":...} JSON text. Falls back
            // to text if no local copy exists yet (e.g. still downloading).
            function doCopy(content, isPhoto, localImage) {
                if (isPhoto) {
                    errorToast.body = "Copy isn't available for photos";
                    errorToast.show();
                    return;
                }
                app.copyToClipboard(content);
                copyToast.show();
            }
            // Stages a reply above the input bar, same as tapping an attachment stages
            // a photo — the two are mutually exclusive, starting one clears the other.
            //
            // senderName is resolved here rather than at the delegate's ActionItem call
            // site, since ActionSet/ActionItem.onTriggered is yet another Cascades scope
            // boundary where rowRoot.otherDisplayName came back "Unknown" on-device.
            function doReply(msgId, cliMsgId, senderId, isMine, rawDName, content, msgType, ts) {
                if (chatViewPage.pendingAttachPath.length > 0) {
                    chatViewPage.pendingAttachPath = "";
                    chatViewPage.pendingAttachName = "";
                }
                var resolvedName;
                if (isMine) {
                    resolvedName = chatViewPage.selfName || "Me";
                } else if (!chatViewPage.isGroup && chatViewPage.threadName.length > 0) {
                    // 1-1 thread: wire "dName" is unreliable (can carry our own name
                    // instead of the sender's) — use threadName (the contact's real name) instead
                    resolvedName = chatViewPage.threadName;
                } else if (chatViewPage.isGroup) {
                    // Group: same wire-dName unreliability — look the sender up by uid
                    // in zService's member-name cache instead
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
            // ---- Reactions ------------------------------------------------
            // 6 fixed icons, same order as ReactionPickerSheet.qml. Kept as plain id
            // strings for local bookkeeping; reactRType() converts to the numeric
            // wire format only at the one call site that needs it.
            property variant reactionAssets: ({
                "like":  "asset:///images/emoji/people/emoji_1f44d_64.png",
                "heart": "asset:///images/emoji/people/emoji_2764_64.png",
                "haha":  "asset:///images/emoji/people/emoji_1f604_64.png",
                "wow":   "asset:///images/emoji/people/emoji_1f631_64.png",
                "cry":   "asset:///images/emoji/people/emoji_1f62d_64.png",
                "angry": "asset:///images/emoji/people/emoji_1f621_64.png"
            })
            property variant reactionRTypes: ({ "like": 0, "heart": 1, "haha": 2, "wow": 3, "cry": 4, "angry": 5 })
            function reactionAssetFor(iconId) { return msgList.reactionAssets[iconId] || ""; }
            function reactionRType(iconId)    { return (iconId in msgList.reactionRTypes) ? msgList.reactionRTypes[iconId] : 0; }

            // msgId -> { uid: { icon, ts } }. Loaded from zService.dbLoadThreadReactions()
            // on thread open, kept live by our own taps and incoming WS updates.
            property variant reactionsByMsg: ({})

            function findMsgRowIndexById(msgId) {
                for (var i = 0; i < msgModel.size(); i++) {
                    var r = msgModel.value(i);
                    if (r && r.msgId === msgId) return i;
                }
                return -1;
            }

            // Pure computation: reactionsByMsg[msgId] -> [{icon, asset, count, mine}],
            // ordered by whoever reacted with that icon first, so new reactions add a
            // pill on the right instead of reordering existing ones. Doesn't touch msgModel.
            function summarizePills(msgId) {
                var byUid = msgList.reactionsByMsg[msgId] || {};
                var uids = Object.keys(byUid);
                uids.sort(function(a, b) { return (byUid[a].ts || 0) - (byUid[b].ts || 0); });

                var order = [];
                var counts = {};
                var mineIcon = "";
                for (var i = 0; i < uids.length; i++) {
                    var uid = uids[i];
                    var icon = byUid[uid].icon;
                    if (!(icon in counts)) { counts[icon] = 0; order.push(icon); }
                    counts[icon]++;
                    if (uid === zService.selfUid) mineIcon = icon;
                }

                var pills = [];
                for (var j = 0; j < order.length; j++) {
                    var ic = order[j];
                    pills.push({ icon: ic, asset: msgList.reactionAssetFor(ic), count: counts[ic], mine: (ic === mineIcon) });
                }
                return pills;
            }

            // Recomputes the pill list for one message already in msgModel and
            // writes it back into that row's `reactions` field
            function refreshReactionsRow(msgId) {
                var idx = msgList.findMsgRowIndexById(msgId);
                if (idx < 0) return;
                var row = msgModel.value(idx);
                row = JSON.parse(JSON.stringify(row)); // distinct object so ArrayDataModel notices the change
                row.reactions = msgList.summarizePills(msgId);
                msgModel.replace(idx, row);
            }

            // Merges one (uid, icon) reaction into reactionsByMsg (icon === "" removes
            // it) then refreshes the pills. Used for both our own tap and incoming WS updates.
            function applyReactionRecord(msgId, uid, icon) {
                // `property variant` returns a fresh copy on every read in this engine,
                // not a live reference — mutating a nested path directly silently no-ops.
                // Read into a local var once, mutate that, write back with one assignment.
                var all = msgList.reactionsByMsg;
                if (!all[msgId]) all[msgId] = {};
                if (!icon || icon.length === 0) {
                    delete all[msgId][uid];
                } else {
                    all[msgId][uid] = { icon: icon, ts: Date.now() };
                }
                msgList.reactionsByMsg = all;
                msgList.refreshReactionsRow(msgId);
            }

            // Entry point for both the picker sheet and a direct tap on a pill —
            // tapping our own icon again removes it, tapping another switches to it
            function doSendReaction(msgId, cliMsgId, msgType, iconId) {
                if (!msgId || msgId.length === 0) return;
                var mine = (msgList.reactionsByMsg[msgId] && msgList.reactionsByMsg[msgId][zService.selfUid])
                           ? msgList.reactionsByMsg[msgId][zService.selfUid].icon : "";
                var removing = (mine === iconId);
                var newIcon  = removing ? "" : iconId;
                msgList.applyReactionRecord(msgId, zService.selfUid, newIcon); // optimistic
                zService.reactMessage(chatViewPage.threadId, chatViewPage.isGroup, msgId, cliMsgId || "",
                                       msgType || 0, removing ? -1 : msgList.reactionRType(iconId), newIcon);
            }

            function doReaction(msgId) {
                var idx = msgList.findMsgRowIndexById(msgId);
                var row = idx >= 0 ? msgModel.value(idx) : null;
                var cliMsgId = row ? (row.cliMsgId || "") : "";
                var msgType  = row ? (row.msgType || 0)  : 0;
                var mine = (msgList.reactionsByMsg[msgId] && msgList.reactionsByMsg[msgId][zService.selfUid])
                           ? msgList.reactionsByMsg[msgId][zService.selfUid].icon : "";
                reactionPickerSheet.openFor(msgId, cliMsgId, msgType, mine);
            }
            function doRecallMsg(msgId, cliMsgId, isMine) {
                if (!isMine) {
                    errorToast.body = "You can only recall your own messages";
                    errorToast.show();
                    return;
                }
                zService.recallMessage(chatViewPage.threadId, chatViewPage.isGroup, msgId, cliMsgId);
            }
            function doForward(msgId, content, msgType, ts) { forwardPickerSheet.openFor(content || "", msgType || 0, msgId || "", ts || ""); }
            // Pin message: ported from zlapi's pinGroupMsg (zca-js has no equivalent).
            // Group-only, same as Zalo's own UI — no 1-1 "pin" exists.
            function doPin(msgId, cliMsgId, senderId, isMine, rawDName, content, msgType) {
                if (!chatViewPage.isGroup) {
                    errorToast.body = "Pinning is only available in group chats";
                    errorToast.show();
                    return;
                }
                var resolvedName;
                if (isMine) {
                    resolvedName = chatViewPage.selfName || "Me";
                } else {
                    var memName = zService.memberDisplayName(senderId || "");
                    resolvedName = (memName && memName.length > 0) ? memName : (rawDName || "Unknown");
                }
                // Same local(1/2)->wire(1/32) msgType conversion sendMessageQuote() does for quotes
                var wireMsgType = (msgType === 2 || msgType === "2") ? 32 : 1;
                var pinContent  = (msgType === 2 || msgType === "2") ? "[Photo]" : (content || "");
                zService.pinGroupMessage(chatViewPage.threadId, msgId || "", cliMsgId || "",
                    senderId || "", resolvedName, pinContent, wireMsgType);
            }
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
            // Tap bubble: tải video về /tmp rồi mở ngay bằng app ngoài (video
            // player) qua app.openLocalFile(). Progress/finish/fail đến async
            // qua zService.videoDownload* signals (xem Connections ở dưới,
            // matched bằng msgList.pendingVideoOpenMsgId, declared above with
            // the other msgList proxy properties).
            function playVideoMsg(msgId, href, fileName) {
                msgList.pendingVideoOpenMsgId = msgId;
                zService.downloadVideoMessage(msgId, href, fileName);
            }
            // Nút "Download" trong context menu (long-press) — giống ảnh:
            // chỉ lưu về /tmp, KHÔNG tự mở.
            function doDownloadVideo(msgId, href, fileName) {
                if (!href || href.length === 0) {
                    errorToast.body = "Video not available";
                    errorToast.show();
                    return;
                }
                msgList.pendingVideoOpenMsgId = ""; // không mở, chỉ báo toast khi xong
                zService.downloadVideoMessage(msgId, href, fileName);
            }
            // Note: the sticker bubble (msgType=5) does NOT route its download
            // trigger through msgList the way videoBubble's doDownloadVideo above
            // does — that was tried first and hit "Can't find variable: zService" /
            // "Cannot assign to non-existent property onStickerReady" from a
            // Component.onCompleted / onVisibleChanged inside the nested delegate
            // Container regardless of the indirection layer. The reliable fix ended
            // up being eager: ZaloService_WebSocket.cpp / ZaloService_Messages.cpp
            // call downloadSticker() themselves the moment they normalize a
            // chat.sticker message's content (search "Eager download" there), so
            // there's nothing left for QML to trigger — stickerBubble only reads
            // ListItemData.stickerLocalPath once chatViewPage.applyStickerUpdate()
            // patches it in.
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

                        // Helper functions for bindings that would otherwise need a block body
                        // ({ var x; ...; return x; } inline in a property/text binding) — that
                        // pattern is a hard parse-time error on this QtQuick1/Cascades engine
                        // (confirmed on-device: "Expected token `numeric literal'" at the first
                        // such binding the parser reached deep in this delegate). Every
                        // multi-statement binding in this delegate must go through a named
                        // function like these instead of an inline block.
                        function timestampLabel(latestTs, ts) {
                            var t = latestTs || ts;
                            if (!t) return "";
                            var n = t * 1;
                            if (n > 0 && n < 1e12) n *= 1000;
                            var d   = new Date(n);
                            var dow = ["Sun","Mon","Tue","Wed","Thu","Fri","Sat"][d.getDay()];
                            var h   = d.getHours();
                            var m2  = d.getMinutes();
                            var ap  = h >= 12 ? "PM" : "AM";
                            var h12 = h % 12; if (h12 === 0) h12 = 12;
                            return dow + " " + h12 + ":" + (m2 < 10 ? "0" : "") + m2 + " " + ap;
                        }
                        function bubbleText(content, msgType, searchQuery, isCurrentSearchMatch) {
                            var raw = (typeof content === "string" && content.length > 0)
                                  ? content
                                  : ((msgType === 2 || msgType === "2")
                                     ? "[Photo]"
                                     : ((msgType === 6 || msgType === "6")
                                        ? "[Sticker]" : "[Photo]"));
                            if (searchQuery && searchQuery.length > 0) {
                                var hlColor = isCurrentSearchMatch ? "#ff9800" : "#ffeb3b";
                                return "<html>" + rowRoot.ListItem.view.highlightMatchesProxy(raw, searchQuery, hlColor) + "</html>";
                            }
                            return raw;
                        }
                        function hasCaption(content) {
                            var c = content || "";
                            if (c.length === 0 || c.charCodeAt(0) !== 123) return false;
                            return c.indexOf('"caption":"') >= 0;
                        }
                        function extractCaption(content) {
                            var c = content || "";
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
                        function photoStatusText(mine, msgId) {
                            var mid = msgId || "";
                            if (mine && mid.indexOf("local_img_") === 0) return "Sending...";
                            return mine ? "Picture sent" : "Photo";
                        }

                        // Do NOT set preferredHeight on rowRoot, even via a binding to a child's
                        // layoutFrame.height — that's self-referential: once Cascades measures one
                        // height, it locks in, and later content that needs more lines gets clipped
                        // instead of wrapping. rowRoot must stay fully auto-sized (not even -1,
                        // which still counts as "set" and blocks the real auto-size default).
                        //
                        // CustomListItem also doesn't support topPadding/bottomPadding/etc (logs a
                        // "Padding is not supported" warning) or a `background` property — it only
                        // exposes highlightAppearance, dividerVisible, and its content child. The
                        // divider/highlight gap between rows is instead cancelled with a fixed
                        // negative bottomMargin on rowContentRoot below.

                        // ActionItem/DeleteActionItem has no "visible" property (extends UIObject,
                        // not Control) — assigning one is a hard QML parse error. Build two full
                        // ActionSets (member vs admin/owner) and pick one instead.
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
                                    onTriggered: { rowRoot.ListItem.view.doForward(ListItemData.msgId, ListItemData.content, ListItemData.msgType, ListItemData.ts); }
                                }
                                ActionItem {
                                    title: "Pin message"
                                    imageSource: "asset:///images/ChatView/ic_pin.png"
                                    onTriggered: {
                                        rowRoot.ListItem.view.doPin(ListItemData.msgId, ListItemData.cliMsgId,
                                            ListItemData.senderId, rowRoot.mine, ListItemData.dName,
                                            ListItemData.content, ListItemData.msgType);
                                    }
                                }
                                ActionItem {
                                    title: "Download"
                                    imageSource: "asset:///images/ChatView/ic_download.png"
                                    onTriggered: {
                                        if (ListItemData.msgType === 3 || ListItemData.msgType === "3") {
                                            var c = ListItemData.content || "";
                                            var hKey = '"href":"';
                                            var hi = c.indexOf(hKey);
                                            var href = "";
                                            if (hi >= 0) {
                                                hi += hKey.length;
                                                var he = hi;
                                                while (he < c.length && c.charAt(he) !== '"') he++;
                                                href = c.substring(hi, he);
                                            }
                                            var nKey = '"fileName":"';
                                            var ni = c.indexOf(nKey);
                                            var fname = "video.mp4";
                                            if (ni >= 0) {
                                                ni += nKey.length;
                                                var ne = ni;
                                                while (ne < c.length && c.charAt(ne) !== '"') ne++;
                                                fname = c.substring(ni, ne) || "video.mp4";
                                            }
                                            rowRoot.ListItem.view.doDownloadVideo(ListItemData.msgId, href, fname);
                                        } else {
                                            rowRoot.ListItem.view.doDownload(ListItemData.msgId, ListItemData.localImage);
                                        }
                                    }
                                }
                                ActionItem {
                                    title: "Share"
                                    imageSource: "asset:///images/ChatView/ic_share.png"
                                    onTriggered: { rowRoot.ListItem.view.doShare(ListItemData.content, (ListItemData.msgType === 2 || ListItemData.msgType === "2"), ListItemData.localImage); }
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
                                    onTriggered: { rowRoot.ListItem.view.doForward(ListItemData.msgId, ListItemData.content, ListItemData.msgType, ListItemData.ts); }
                                }
                                ActionItem {
                                    title: "Pin message"
                                    imageSource: "asset:///images/ChatView/ic_pin.png"
                                    onTriggered: {
                                        rowRoot.ListItem.view.doPin(ListItemData.msgId, ListItemData.cliMsgId,
                                            ListItemData.senderId, rowRoot.mine, ListItemData.dName,
                                            ListItemData.content, ListItemData.msgType);
                                    }
                                }
                                ActionItem {
                                    title: "Download"
                                    imageSource: "asset:///images/ChatView/ic_download.png"
                                    onTriggered: {
                                        if (ListItemData.msgType === 3 || ListItemData.msgType === "3") {
                                            var c = ListItemData.content || "";
                                            var hKey = '"href":"';
                                            var hi = c.indexOf(hKey);
                                            var href = "";
                                            if (hi >= 0) {
                                                hi += hKey.length;
                                                var he = hi;
                                                while (he < c.length && c.charAt(he) !== '"') he++;
                                                href = c.substring(hi, he);
                                            }
                                            var nKey = '"fileName":"';
                                            var ni = c.indexOf(nKey);
                                            var fname = "video.mp4";
                                            if (ni >= 0) {
                                                ni += nKey.length;
                                                var ne = ni;
                                                while (ne < c.length && c.charAt(ne) !== '"') ne++;
                                                fname = c.substring(ni, ne) || "video.mp4";
                                            }
                                            rowRoot.ListItem.view.doDownloadVideo(ListItemData.msgId, href, fname);
                                        } else {
                                            rowRoot.ListItem.view.doDownload(ListItemData.msgId, ListItemData.localImage);
                                        }
                                    }
                                }
                                ActionItem {
                                    title: "Share"
                                    imageSource: "asset:///images/ChatView/ic_share.png"
                                    onTriggered: { rowRoot.ListItem.view.doShare(ListItemData.content, (ListItemData.msgType === 2 || ListItemData.msgType === "2"), ListItemData.localImage); }
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

                        // Query text and whether this row is the currently-focused match
                        // (gets a slightly stronger highlight than other matches)
                        property string searchQuery: ListItem.view.searchQuery
                        property bool   isCurrentSearchMatch: rowRoot.searchQuery.length > 0
                                                               && ListItem.view.searchCurrentMsgIndex === ListItem.indexPath[0]

                        property bool mine: (ListItemData.isMine === true
                                             || ListItemData.isMine === "true"
                                             || ListItemData.isMine === 1)
                        // Row kind: "message" (default chat bubble), "poll" (inline poll card,
                        // see pollCardRow), or "boardEvent" (system-notice pill, see boardEventRow).
                        // A plain visible-toggle on sibling Containers, not a second `type:` in
                        // listItemComponents — see the "type-toggle" rejection note further down.
                        property string kind: ListItemData.kind || "message"
                        property bool grouped: ListItemData.grouped === true
                        // Diagnostic: logs what this delegate reads for `grouped`, to compare
                        // against what rebuildGroups() wrote. Safe to remove.
                        onGroupedChanged: {
                            console.log("[Zalo QML] delegate grouped-binding: msgId=" + String(ListItemData.msgId).slice(-6)
                                + " grouped=" + grouped + " bubblePos=" + (ListItemData.bubblePos || "full")
                                + " index=" + rowRoot.ListItem.indexPath[0]);
                        }
                        property bool recalled: (ListItemData.msgType === 99 || ListItemData.msgType === "99")
                        // When "Show Recalled Messages" is on and the original content was
                        // plain text, keep showing it instead of the placeholder banner
                        property bool showRecalledSetting: ListItem.view.showRecalledMessages
                        property string recalledOriginal: ListItemData.recalledOriginalContent || ""
                        // Recalled photo/sticker: detected from either the preserved original
                        // content being a photo JSON blob, or a cached local image still on disk
                        property bool recalledIsPhoto: (rowRoot.recalledOriginal.length > 0
                                                         && rowRoot.recalledOriginal.charAt(0) === "{"
                                                         && (rowRoot.recalledOriginal.indexOf("normalUrl") >= 0
                                                             || rowRoot.recalledOriginal.indexOf("thumbUrl") >= 0
                                                             || rowRoot.recalledOriginal.indexOf("thumb") >= 0
                                                             || rowRoot.recalledOriginal.indexOf("href") >= 0))
                                                        || !!(ListItemData.localImage && ListItemData.localImage !== "")
                        property bool recalledHasOriginalText: rowRoot.recalledOriginal.length > 0 && !rowRoot.recalledIsPhoto
                        // True when falling back to the plain "This message was recalled"
                        // placeholder (setting off, or no recoverable text/photo)
                        property bool recalledHidden: rowRoot.recalled
                                                       && !(rowRoot.showRecalledSetting
                                                            && (rowRoot.recalledHasOriginalText || rowRoot.recalledIsPhoto))

                        // Sizes photo bubbles to the image's real aspect ratio without exceeding
                        // the bubble width. 94 = side spacers (6+60) + bubble padding (14+14).
                        property string bubblePos: ListItemData.bubblePos || "full"
                        // Diagnostic — see onGroupedChanged above
                        onBubblePosChanged: {
                            console.log("[Zalo QML] delegate bubblePos-binding: msgId=" + String(ListItemData.msgId).slice(-6)
                                + " bubblePos=" + bubblePos + " index=" + rowRoot.ListItem.indexPath[0]);
                        }
                        property string bubbleImage: rowRoot.mine
                            ? ("asset:///images/Bubble/outgoing/" + rowRoot.bubblePos + ".png")
                            : ("asset:///images/Bubble/incoming/" + rowRoot.bubblePos + ".png")
                        property real bubbleMaxW: rowLUH.layoutFrame.width > 94
                                                   ? (rowLUH.layoutFrame.width - 94)
                                                   : ui.du(40)

                        // True when this row is a reply to an earlier message (quoteMsgId set
                        // by dbSaveMessage/WS parsing). Drives the quote-strip block above the text.
                        property bool hasQuote: !!(ListItemData.quoteMsgId && ListItemData.quoteMsgId.length > 0)

                        // Same wire dName unreliability as otherDisplayName below, but for the
                        // quoted person, who isn't always "the other party" (you can quote yourself).
                        // quoteOwnerId tells us which case this is.
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

                        // Zalo's WS "dName" field is unreliable — it can carry OUR OWN name
                        // instead of the sender's, in both 1-1 and group threads.
                        // - 1-1: use chatViewPage.threadName (from the contact's profile) instead
                        // - Group: look the sender up via memberDisplayNameProxy (m_memberNames,
                        //   built from getmg-v2's currentMems)
                        // Falls back to the wire dName if neither source has an answer yet.
                        property string otherMemberName: ListItem.view.isGroupChat
                            ? ListItem.view.memberDisplayNameProxy(ListItemData.senderId || "")
                            : ""
                        property string otherDisplayName: (!ListItem.view.isGroupChat && ListItem.view.threadNameProxy.length > 0)
                            ? ListItem.view.threadNameProxy
                            : ((ListItem.view.isGroupChat && rowRoot.otherMemberName.length > 0)
                               ? rowRoot.otherMemberName
                               : (ListItemData.dName || "Unknown"))

                        // Yellow highlight: true for the active search match, or the target of
                        // a "jump to pinned message" tap (jumpHighlightMsgId, cleared after a short delay)
                        property bool isJumpHighlighted: ListItem.view.jumpHighlightMsgId.length > 0
                                                          && ListItem.view.jumpHighlightMsgId === (ListItemData.msgId || "")
                        property bool isHighlighted: rowRoot.isCurrentSearchMatch || rowRoot.isJumpHighlighted

                        // CustomListItem accepts exactly one default-property child — wraps
                        // the three mutually-exclusive per-kind sub-rows (message/poll/boardEvent),
                        // toggled via visible: false, one level deeper than they used to be
                        Container {
                            id: rowContentRoot
                            horizontalAlignment: HorizontalAlignment.Fill
                            layout: StackLayout { orientation: LayoutOrientation.TopToBottom }
                            // Fixed, constant offsets cancelling CustomListItem's invisible
                            // divider gap. Must be plain constants, not a live binding to any
                            // measured height (that clipped long messages instead of wrapping).
                            // Tuned per bubblePos since "bottom" and "middle" rows measure
                            // slightly different heights. Adjust independently if the gap reappears.
                            property int gapCompensation:
                                rowRoot.bubblePos === "middle" ? -6 : -12
                            bottomMargin: rowRoot.kind === "message" ? gapCompensation : 0
                            attachedObjects: [
                                LayoutUpdateHandler {
                                    id: rowContentRootLUH
                                    // Diagnostic only — confirms rowContentRoot measures/wraps
                                    // text correctly as message length grows
                                    onLayoutFrameChanged: {
                                        console.log("[Zalo QML] rowContentRoot layoutFrame CHANGED: msgId="
                                            + String(ListItemData.msgId).slice(-6)
                                            + " height=" + layoutFrame.height
                                            + " grouped=" + rowRoot.grouped
                                            + " bubblePos=" + rowRoot.bubblePos);
                                    }
                                }
                            ]

                        Container {
                            horizontalAlignment: HorizontalAlignment.Fill
                            visible: rowRoot.kind === "message"
                            topPadding:    rowRoot.grouped ? 0 : 10
                            bottomPadding: 0
                            layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                            attachedObjects: [
                                LayoutUpdateHandler {
                                    id: rowLUH
                                    // Diagnostic: logs the row's real measured height next to the
                                    // grouped value that decided its topPadding, to confirm the padding
                                    // change actually reaches layout. Safe to remove.
                                    onLayoutFrameChanged: {
                                        console.log("[Zalo QML] row layoutFrame CHANGED: msgId=" + String(ListItemData.msgId).slice(-6)
                                            + " grouped=" + rowRoot.grouped + " bubblePos=" + rowRoot.bubblePos
                                            + " y=" + layoutFrame.y
                                            + " width=" + layoutFrame.width
                                            + " height=" + layoutFrame.height
                                            + " bubbleMaxW=" + rowRoot.bubbleMaxW
                                            + " index=" + rowRoot.ListItem.indexPath[0]);
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
                            // All bubbles render at a fixed width (bubbleMaxW) regardless of text
                            // length, only height varies. preferredWidth forces this, maxWidth is
                            // kept as a safety ceiling.
                            preferredWidth: rowRoot.bubbleMaxW
                            maxWidth:       rowRoot.bubbleMaxW
                            // Uses a flat solid-color fill, not a nine-patch bubble PNG — the
                            // source PNGs are mostly transparent/thin-bordered and stretched into
                            // a blurry smear when scaled up to fill a row.
                            background: rowRoot.isHighlighted
                                ? Color.create("#fff3b0")
                                : (rowRoot.isDark
                                    ? Color.create("#2a2a2a")
                                    : Color.create("#FFFFFF"))

                            Container {
                                background: Color.Transparent
                                horizontalAlignment: HorizontalAlignment.Fill
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
                                    text: rowRoot.timestampLabel(ListItemData.latestTs, ListItemData.ts)
                                    horizontalAlignment: HorizontalAlignment.Right
                                    textStyle { fontSize: FontSize.XSmall; color: rowRoot.isDark ? Color.create("#888888") : Color.create("#777777") }
                                    topMargin: 0; bottomMargin: 0
                                }
                            }

                            Container {
                                id: msgContentRoot
                                horizontalAlignment: HorizontalAlignment.Fill
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

                                // Recovered photo bubble for a recalled image, shown only when
                                // "Show Recalled Messages" is on and the cached file is still on disk
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

                                // Reply/quote block, a separate chunk above the message text.
                                // Colored by whose bubble it is, matching the bottom accent-strip
                                // colors. Tapping it jumps to and highlights the quoted message.
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
                                             && (ListItemData.msgType !== 3 && ListItemData.msgType !== "3")
                                             && (ListItemData.msgType !== 4 && ListItemData.msgType !== "4")
                                             && (ListItemData.msgType !== 5 && ListItemData.msgType !== "5")
                                             && (ListItemData.msgType !== 6 && ListItemData.msgType !== "6")
                                             && !(typeof ListItemData.content === "string"
                                                  && ListItemData.content.length > 1
                                                  && ListItemData.content.charAt(0) === "{"
                                                  && (ListItemData.content.indexOf("normalUrl") >= 0
                                                      || ListItemData.content.indexOf("thumbUrl") >= 0
                                                      || ListItemData.content.indexOf("thumb") >= 0
                                                      || ListItemData.content.indexOf("href") >= 0))
                                    // maxWidth (not preferredWidth or Fill/spaceQuota) is the right
                                    // pattern here: it caps wrapping width without forcing short
                                    // messages like "Hi" to stretch to bubbleMaxW, while still giving
                                    // long text a ceiling to wrap against instead of clipping.
                                    maxWidth: Math.max(0, rowRoot.bubbleMaxW - 28)
                                    text: rowRoot.bubbleText(ListItemData.content, ListItemData.msgType,
                                                              rowRoot.searchQuery, rowRoot.isCurrentSearchMatch)
                                    textStyle {
                                        base:  SystemDefaults.TextStyles.BodyText
                                        color: rowRoot.isDark ? Color.create("#eeeeee") : Color.create("#111111")
                                    }
                                    multiline: true
                                    topMargin: 0; bottomMargin: 0
                                }

                                // Photo attachment bubble: caption (if any) -> dashed separator ->
                                // inline photo (capped to bubbleMaxW) -> status text. No full-screen viewer.
                                Container {
                                    id: photoBubble
                                    visible: !rowRoot.recalled
                                             && (ListItemData.msgType !== 3 && ListItemData.msgType !== "3")
                                             && (ListItemData.msgType !== 5 && ListItemData.msgType !== "5")
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

                                    // Caption label, shown only when the photo JSON has a "caption" key
                                    Label {
                                        id: photoCaptionLbl
                                        // No preferredWidth/maxWidth pin — photoBubble is already
                                        // Fill, so this inherits a fresh width each layout pass
                                        horizontalAlignment: HorizontalAlignment.Fill
                                        visible: rowRoot.hasCaption(ListItemData.content)
                                        text: rowRoot.extractCaption(ListItemData.content)
                                        textStyle {
                                            base:  SystemDefaults.TextStyles.BodyText
                                            color: rowRoot.isDark ? Color.create("#eeeeee") : Color.create("#111111")
                                        }
                                        multiline: true
                                        topMargin: 0; bottomMargin: 4
                                    }

                                    // Separator line, only present when caption is showing
                                    Container {
                                        visible: photoCaptionLbl.visible
                                        horizontalAlignment: HorizontalAlignment.Fill
                                        preferredHeight: 1
                                        background: rowRoot.isDark ? Color.create("#555555") : Color.create("#cccccc")
                                        bottomMargin: 6
                                    }

                                    // Inline photo at capped size, same pattern as the recalled-photo
                                    // preview above. No tap-to-open viewer, no filename/size row.
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
                                                color: rowRoot.isDark ? Color.create("#888888") : Color.create("#808080")
                                            }
                                        }
                                    }

                                    // Gray status text, shows "Sending..." for unconfirmed outgoing
                                    Label {
                                        text: rowRoot.photoStatusText(rowRoot.mine, ListItemData.msgId)
                                        textStyle {
                                            fontSize: FontSize.XSmall
                                            fontStyle: FontStyle.Italic
                                            color: Color.create("#888888")
                                        }
                                        topMargin: 4; bottomMargin: 0
                                    }
                                }

                                // Video/file attachment bubble (msgType === 3): file-type icon on the
                                // left, filename on the right, tap to download-and-open. Content JSON
                                // shape: {"fileName":"...","href":"...","fileSize":...} — same on both
                                // the outgoing (sendVideo) and incoming (share.file normalization) paths.
                                Container {
                                    id: videoBubble
                                    visible: !rowRoot.recalled
                                             && (ListItemData.msgType === 3 || ListItemData.msgType === "3")
                                    horizontalAlignment: HorizontalAlignment.Fill
                                    topMargin: 2; bottomMargin: 2
                                    layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                                    maxWidth: rowRoot.bubbleMaxW

                                    // FIX: these used to be property bindings that wrapped their
                                    // logic in an immediately-invoked function expression, e.g.
                                    // "property string vFileName: (function(){ ...uses
                                    // ListItemData.content... })()". That pattern is unreliable on
                                    // this QtQuick1/Cascades engine — the binding's dependency on
                                    // ListItemData.content, read from inside the nested closure,
                                    // wasn't always captured, so the very first evaluation (right
                                    // when the row is created) could see a stale/default content
                                    // value and never re-run once the real content arrived, leaving
                                    // the bubble stuck showing a garbled filename/href from tapping
                                    // too soon after the message appeared (self-"fixed" only once
                                    // something else forced a re-bind, e.g. a second tap). Calling a
                                    // plain function with ListItemData.content passed explicitly, as
                                    // the top-level binding expression, makes the dependency
                                    // unambiguous to the binding engine.
                                    function extractJsonStringField(content, key) {
                                        var c = content || "";
                                        if (c.length === 0 || c.charCodeAt(0) !== 123) return "";
                                        var k = '"' + key + '":"';
                                        var si = c.indexOf(k);
                                        if (si < 0) return "";
                                        si += k.length;
                                        var ei = si;
                                        while (ei < c.length) {
                                            var code = c.charCodeAt(ei);
                                            if (code === 92) { ei += 2; continue; }
                                            if (code === 34) break;
                                            ei++;
                                        }
                                        return c.substring(si, ei);
                                    }
                                    property string vFileName: videoBubble.extractJsonStringField(ListItemData.content, "fileName") || "video.mp4"
                                    property string vHref: videoBubble.extractJsonStringField(ListItemData.content, "href")
                                    // Helper functions instead of block-body property declarations
                                    // (property string foo: { var x; return x; } is a parse-time
                                    // error on this QtQuick1/Cascades engine) — plain functions
                                    // called from a single-expression property binding instead.
                                    function extFor(fileName) {
                                        var n = fileName || "";
                                        var di = n.lastIndexOf(".");
                                        return di >= 0 ? n.substring(di + 1).toLowerCase() : "";
                                    }
                                    function iconFor(ext) {
                                        if (ext === "txt")                 return "asset:///images/File Types/File Type - TXT.png";
                                        if (ext === "doc" || ext === "docx") return "asset:///images/File Types/File Type - Document.png";
                                        if (ext === "pdf")                 return "asset:///images/File Types/File Type - PDF.png";
                                        if (ext === "ppt" || ext === "pptx") return "asset:///images/File Types/File Type - PPT.png";
                                        if (ext === "xls" || ext === "xlsx") return "asset:///images/File Types/File Type - XLS (Spreadsheet).png";
                                        if (ext === "epub")                return "asset:///images/File Types/File Type - ePUB.png";
                                        if (ext === "apk")                 return "asset:///images/File Types/File Type - APK.png";
                                        if (ext === "cer")                 return "asset:///images/File Types/File Type - Certificate.png";
                                        if (ext === "zip" || ext === "rar" || ext === "7z") return "asset:///images/File Types/File Type - ZIP (Compressed).png";
                                        if (ext === "vcf")                 return "asset:///images/File Types/File Type - VCF (Contanct).png";
                                        if (ext === "mp3" || ext === "flac") return "asset:///images/File Types/File Type - Music.png";
                                        if (ext === "m4a")                 return "asset:///images/File Types/File Type - Voice Note (Audio Recording).png";
                                        if (ext === "bar")                 return "asset:///images/File Types/File Type - BAR.png";
                                        return "asset:///images/File Types/File Type - Video.png";
                                    }
                                    // Trạng thái hiển thị dưới tên file — cũng phải là function thay vì
                                    // block-body binding trên Label.text (cùng lý do như extFor/iconFor).
                                    // Nhận tham số tường minh thay vì đọc thẳng ListItemData/rowRoot bên
                                    // trong, để binding engine thấy rõ dependency (không bị stale như
                                    // ghi chú extractJsonStringField ở trên).
                                    function statusText(mine, msgId, uploadVideoActive, uploadVideoPercent,
                                                         uploadFileActive, uploadFilePercent,
                                                         downloading, progress, isVideo) {
                                        var mid = msgId || "";
                                        if (mine && mid.indexOf("local_video_") === 0) {
                                            return uploadVideoActive
                                                ? "Sending " + uploadVideoPercent + "%..."
                                                : "Sending...";
                                        }
                                        // sendFile() (document) now shares the same chunked-upload
                                        // progress as video — same "Sending N%..." pattern.
                                        if (mine && mid.indexOf("local_file_") === 0) {
                                            return uploadFileActive
                                                ? "Sending " + uploadFilePercent + "%..."
                                                : "Sending...";
                                        }
                                        if (downloading) return "Downloading " + progress + "%...";
                                        return isVideo ? "Tap to play" : "Tap to open";
                                    }
                                    // Extension đọc từ vFileName để chọn icon đúng loại tài liệu —
                                    // video giữ nguyên File Type - Video.png, các loại tài liệu/định
                                    // dạng khác (doc/docx, ppt/pptx, xls/xlsx, txt, pdf, epub, apk,
                                    // cer, zip/rar/7z, vcf, mp3/flac, m4a, bar) map sang icon tương
                                    // ứng theo yêu cầu; phần mở rộng lạ rơi về icon Video (mặc định cũ).
                                    property string vExt: videoBubble.extFor(videoBubble.vFileName)
                                    property bool vIsVideo: videoBubble.vExt === "mp4" || videoBubble.vExt === "mov"
                                                          || videoBubble.vExt === "3gp" || videoBubble.vExt === "mkv"
                                    property string vIconSource: videoBubble.iconFor(videoBubble.vExt)
                                    // Bound to msgList's single-slot progress tracker (only one video
                                    // downloads at a time) — true only while THIS bubble's msgId matches.
                                    property bool vDownloading: rowRoot.ListItem.view.videoProgressMsgId === (ListItemData.msgId || "")
                                                                 && rowRoot.ListItem.view.videoProgressMsgId !== ""
                                    property int  vProgress: vDownloading ? rowRoot.ListItem.view.videoProgressPercent : 0

                                    ImageView {
                                        imageSource: videoBubble.vIconSource
                                        scalingMethod: ScalingMethod.AspectFit
                                        verticalAlignment: VerticalAlignment.Center
                                        preferredWidth: 68; preferredHeight: 68
                                        minWidth: 68
                                        rightMargin: 10
                                    }

                                    Container {
                                        layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                                        verticalAlignment: VerticalAlignment.Center

                                        Label {
                                            text: videoBubble.vFileName
                                            horizontalAlignment: HorizontalAlignment.Fill
                                            multiline: true
                                            textStyle {
                                                base:  SystemDefaults.TextStyles.BodyText
                                                color: rowRoot.isDark ? Color.create("#eeeeee") : Color.create("#111111")
                                            }
                                        }
                                        Label {
                                            text: videoBubble.statusText(rowRoot.mine, ListItemData.msgId || "",
                                                      rowRoot.ListItem.view.uploadVideoActive, rowRoot.ListItem.view.uploadVideoPercent,
                                                      rowRoot.ListItem.view.uploadFileActive, rowRoot.ListItem.view.uploadFilePercent,
                                                      videoBubble.vDownloading, videoBubble.vProgress, videoBubble.vIsVideo)
                                            textStyle {
                                                fontSize: FontSize.XSmall
                                                fontStyle: FontStyle.Italic
                                                color: Color.create("#888888")
                                            }
                                            topMargin: 2
                                        }
                                    }

                                    gestureHandlers: [
                                        TapHandler {
                                            onTapped: {
                                                if (videoBubble.vDownloading) return;
                                                if (!videoBubble.vHref || videoBubble.vHref === "") return;
                                                // vDownloading/vProgress are computed off msgList's
                                                // shared progress tracker — playVideoMsg() kicks off
                                                // the download, and the first progress signal flips
                                                // vDownloading true for this bubble.
                                                rowRoot.ListItem.view.playVideoMsg(ListItemData.msgId, videoBubble.vHref, videoBubble.vFileName);
                                            }
                                        }
                                    ]
                                }

                                // Call log bubble (msgType === 4): voice/video call summary —
                                // "Call ended"/"Missed call" + duration, icon on the left picked
                                // from the pre-supplied video_voice call-log assets. Content JSON
                                // shape: {"callResult":"ended"|"missed","callKind":"voice"|"video",
                                // "duration":N} — normalized WS-side from chat.recommended
                                // (recommened.calltime / recommened.misscall) in ZaloService_WebSocket.cpp.
                                Container {
                                    id: callBubble
                                    visible: !rowRoot.recalled
                                             && (ListItemData.msgType === 4 || ListItemData.msgType === "4")
                                    horizontalAlignment: HorizontalAlignment.Fill
                                    topMargin: 2; bottomMargin: 2
                                    layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                                    maxWidth: rowRoot.bubbleMaxW

                                    function extractJsonStringField(content, key) {
                                        var c = content || "";
                                        if (c.length === 0 || c.charCodeAt(0) !== 123) return "";
                                        var k = '"' + key + '":"';
                                        var si = c.indexOf(k);
                                        if (si < 0) return "";
                                        si += k.length;
                                        var ei = si;
                                        while (ei < c.length) {
                                            var code = c.charCodeAt(ei);
                                            if (code === 92) { ei += 2; continue; }
                                            if (code === 34) break;
                                            ei++;
                                        }
                                        return c.substring(si, ei);
                                    }
                                    function extractJsonIntField(content, key) {
                                        var c = content || "";
                                        var k = '"' + key + '":';
                                        var si = c.indexOf(k);
                                        if (si < 0) return 0;
                                        si += k.length;
                                        var ei = si;
                                        while (ei < c.length && c.charAt(ei) >= '0' && c.charAt(ei) <= '9') ei++;
                                        var n = c.substring(si, ei);
                                        return n.length > 0 ? parseInt(n, 10) : 0;
                                    }
                                    property string cResult:   callBubble.extractJsonStringField(ListItemData.content, "callResult") || "ended"
                                    property string cKind:     callBubble.extractJsonStringField(ListItemData.content, "callKind") || "voice"
                                    property int    cDuration: callBubble.extractJsonIntField(ListItemData.content, "duration")
                                    property bool   cMissed:   callBubble.cResult === "missed"
                                    property bool   cVideo:    callBubble.cKind === "video"

                                    // ca_{video,voice}_chat_{incoming,outgoing,missed}.png provided
                                    // under assets/images/Bubble/video_voice/. Missed always uses the
                                    // "missed" asset regardless of direction; ended calls use
                                    // incoming/outgoing based on who placed the call.
                                    //
                                    // Plain functions instead of block-body property bindings ("property
                                    // string foo: { var x; return x; }") — same parse-time-error trap on
                                    // this QtQuick1/Cascades engine noted on vFileName/statusText in
                                    // videoBubble above.
                                    function iconForCall(missed, mine, video) {
                                        var kind = video ? "video" : "voice";
                                        var dir  = missed ? "missed" : (mine ? "outgoing" : "incoming");
                                        return "asset:///images/Bubble/video_voice/ca_" + kind + "_chat_" + dir + ".png";
                                    }
                                    function formatDuration(sec) {
                                        var s = sec || 0;
                                        var m = Math.floor(s / 60);
                                        var r = s % 60;
                                        var rStr = r < 10 ? ("0" + r) : ("" + r);
                                        return m + ":" + rStr;
                                    }
                                    function titleForCall(missed, video) {
                                        if (missed) return video ? "Missed video call" : "Missed call";
                                        return video ? "Video call" : "Voice call";
                                    }
                                    property string cIconSource: callBubble.iconForCall(callBubble.cMissed, rowRoot.mine, callBubble.cVideo)
                                    property string cTitle:      callBubble.titleForCall(callBubble.cMissed, callBubble.cVideo)
                                    property string cSubtitle:   callBubble.cMissed
                                                                  ? "No answer"
                                                                  : ("Call ended \u2022 " + callBubble.formatDuration(callBubble.cDuration))

                                    ImageView {
                                        imageSource: callBubble.cIconSource
                                        scalingMethod: ScalingMethod.AspectFit
                                        verticalAlignment: VerticalAlignment.Center
                                        preferredWidth: 56; preferredHeight: 56
                                        minWidth: 56
                                        rightMargin: 10
                                    }

                                    Container {
                                        layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                                        verticalAlignment: VerticalAlignment.Center

                                        Label {
                                            text: callBubble.cTitle
                                            horizontalAlignment: HorizontalAlignment.Fill
                                            multiline: true
                                            textStyle {
                                                base:  SystemDefaults.TextStyles.BodyText
                                                color: callBubble.cMissed
                                                       ? Color.create("#e02020")
                                                       : (rowRoot.isDark ? Color.create("#eeeeee") : Color.create("#111111"))
                                            }
                                        }
                                        Label {
                                            text: callBubble.cSubtitle
                                            textStyle {
                                                fontSize: FontSize.XSmall
                                                fontStyle: FontStyle.Italic
                                                color: Color.create("#888888")
                                            }
                                            topMargin: 2
                                        }
                                    }

                                    // Informational only — no in-app calling is implemented, so this
                                    // bubble doesn't try to re-dial on tap.
                                }

                                // Link-share bubble (msgType === 6): title/description/thumbnail
                                // card for shared URLs — Zalo sends this via the same wire msgType
                                // ("chat.recommended") as the call-log bubble above, disambiguated
                                // server-side by "action" (see ZaloService_WebSocket.cpp / _Messages.cpp,
                                // mt==6). Content JSON shape: {"linkTitle":..,"linkDesc":..,
                                // "linkHref":..,"linkThumb":..}. Tapping opens linkHref in the system
                                // browser via Qt.openUrlExternally (BB10 Cascades has no in-app
                                // browser component available to this project).
                                Container {
                                    id: linkBubble
                                    visible: !rowRoot.recalled
                                             && (ListItemData.msgType === 6 || ListItemData.msgType === "6")
                                    horizontalAlignment: HorizontalAlignment.Fill
                                    topMargin: 2; bottomMargin: 2
                                    maxWidth: rowRoot.bubbleMaxW
                                    background: rowRoot.isDark ? Color.create("#2a2a2a") : Color.create("#f2f2f2")
                                    layout: StackLayout { orientation: LayoutOrientation.TopToBottom }

                                    function extractJsonStringField(content, key) {
                                        var c = content || "";
                                        if (c.length === 0 || c.charCodeAt(0) !== 123) return "";
                                        var k = '"' + key + '":"';
                                        var si = c.indexOf(k);
                                        if (si < 0) return "";
                                        si += k.length;
                                        var ei = si;
                                        while (ei < c.length) {
                                            var code = c.charCodeAt(ei);
                                            if (code === 92) { ei += 2; continue; }
                                            if (code === 34) break;
                                            ei++;
                                        }
                                        return c.substring(si, ei);
                                    }
                                    // Label has no maxLines property in Cascades QML 1.0 (crashed the
                                    // parser when used above) — cap length manually instead so long
                                    // descriptions (e.g. GitHub repo blurbs) don't blow up bubble height.
                                    function truncateDesc(s) {
                                        if (s.length <= 140) return s;
                                        return s.substring(0, 140) + "...";
                                    }
                                    property string lTitle: linkBubble.extractJsonStringField(ListItemData.content, "linkTitle")
                                    property string lDesc:  linkBubble.truncateDesc(linkBubble.extractJsonStringField(ListItemData.content, "linkDesc"))
                                    property string lHref:  linkBubble.extractJsonStringField(ListItemData.content, "linkHref")
                                    property string lThumb: linkBubble.extractJsonStringField(ListItemData.content, "linkThumb")

                                    // Cascades QML 1.0's ImageView can't load a remote https://
                                    // URL directly ("Unsupported scheme (https) ... Image loading
                                    // aborted" — confirmed on-device log). ListItemData.localImage
                                    // is populated once chatViewPage.onNewMessage triggers
                                    // zService.downloadImageMessage(msgId, linkThumb, ...) and the
                                    // resulting imageMsgReady -> applyImageUpdate() round-trip
                                    // lands (asynchronous — this is empty right after the message
                                    // first arrives and fills in a moment later, same lifecycle
                                    // as the photo bubble's localImage). Falling back to lThumb
                                    // directly is deliberate-but-futile here: it will not load,
                                    // consistent with how the plain-URL field is treated elsewhere
                                    // in this file as a last-resort value rather than a guarantee.
                                    ImageView {
                                        visible: (ListItemData.localImage && ListItemData.localImage.length > 0)
                                                 || linkBubble.lThumb.length > 0
                                        imageSource: (ListItemData.localImage && ListItemData.localImage.length > 0)
                                                     ? ListItemData.localImage
                                                     : linkBubble.lThumb
                                        scalingMethod: ScalingMethod.AspectFill
                                        horizontalAlignment: HorizontalAlignment.Fill
                                        preferredHeight: 220
                                        minHeight: 0
                                    }

                                    Container {
                                        topPadding: ui.du(1.0); bottomPadding: ui.du(1.0)
                                        leftPadding: ui.du(1.0); rightPadding: ui.du(1.0)
                                        layout: StackLayout { orientation: LayoutOrientation.TopToBottom }

                                        Label {
                                            text: linkBubble.lTitle.length > 0 ? linkBubble.lTitle : linkBubble.lHref
                                            horizontalAlignment: HorizontalAlignment.Fill
                                            multiline: true
                                            textStyle {
                                                base: SystemDefaults.TextStyles.BodyText
                                                fontWeight: FontWeight.Bold
                                                color: rowRoot.isDark ? Color.create("#eeeeee") : Color.create("#111111")
                                            }
                                        }
                                        Label {
                                            visible: linkBubble.lDesc.length > 0
                                            text: linkBubble.lDesc
                                            horizontalAlignment: HorizontalAlignment.Fill
                                            multiline: true
                                            textStyle {
                                                fontSize: FontSize.Small
                                                color: rowRoot.isDark ? Color.create("#aaaaaa") : Color.create("#666666")
                                            }
                                            topMargin: 4
                                        }
                                        Label {
                                            visible: linkBubble.lHref.length > 0
                                            text: linkBubble.lHref
                                            horizontalAlignment: HorizontalAlignment.Fill
                                            multiline: false
                                            textStyle {
                                                fontSize: FontSize.XSmall
                                                color: Color.create("#4a90d9")
                                            }
                                            topMargin: 6
                                        }
                                    }

                                    gestureHandlers: [
                                        TapHandler {
                                            onTapped: {
                                                if (linkBubble.lHref.length > 0)
                                                    Qt.openUrlExternally(linkBubble.lHref);
                                            }
                                        }
                                    ]
                                }

                                // Sticker bubble (msgType === 5): plain image, no caption/background/
                                // status row — Zalo (and every other chat app) renders stickers "bare"
                                // rather than wrapped in a bubble frame, so this deliberately skips the
                                // padding/background photoBubble uses. Content JSON shape:
                                // {"stickerId":N} — normalized WS/history-side from chat.sticker's
                                // {"id":N,"catId":M,"type":7}. See ZaloService_WebSocket.cpp (mt==5) for
                                // how the eid->image URL was confirmed.
                                Container {
                                    id: stickerBubble
                                    visible: !rowRoot.recalled
                                             && (ListItemData.msgType === 5 || ListItemData.msgType === "5")
                                    horizontalAlignment: HorizontalAlignment.Left
                                    topMargin: 2; bottomMargin: 2
                                    // preferredWidth/Height alone were only a hint — with the ImageView
                                    // child below set to Fill inside a DockLayout, Cascades let the
                                    // container grow past it to the full row width once a real image
                                    // loaded (visible as a full-width blown-up bubble on device). maxWidth/
                                    // maxHeight are hard caps and actually constrain it, same fix pattern
                                    // as photoBubble's inner image Container above.
                                    preferredWidth: ui.du(30)
                                    preferredHeight: ui.du(30)
                                    maxWidth: ui.du(30)
                                    maxHeight: ui.du(30)
                                    layout: DockLayout {}

                                    function extractStickerId(content) {
                                        var c = content || "";
                                        var k = '"stickerId":';
                                        var si = c.indexOf(k);
                                        if (si < 0) return 0;
                                        si += k.length;
                                        var ei = si;
                                        while (ei < c.length && c.charAt(ei) >= '0' && c.charAt(ei) <= '9') ei++;
                                        var n = c.substring(si, ei);
                                        return n.length > 0 ? parseInt(n, 10) : 0;
                                    }
                                    property int sStickerId: stickerBubble.extractStickerId(ListItemData.content)
                                    // ListItemData.stickerLocalPath is a plain model field, patched by
                                    // chatViewPage.applyStickerUpdate() once the async download finishes.
                                    // This bubble only ever *reads* it via ordinary property binding (same
                                    // pattern as selfUidProxy near the top of this file) — it never triggers
                                    // the download itself. The download is kicked off eagerly on the C++
                                    // side (ZaloService_WebSocket.cpp / ZaloService_Messages.cpp, wherever
                                    // msgType=5 content first gets normalized) rather than from here: two
                                    // different QML-side init signals (Component.onCompleted, then
                                    // onVisibleChanged) were both tried and neither fired reliably for this
                                    // nested delegate Container on device.
                                    property string sLocalPath: ListItemData.stickerLocalPath || ""

                                    ImageView {
                                        visible: stickerBubble.sLocalPath !== ""
                                        horizontalAlignment: HorizontalAlignment.Fill
                                        verticalAlignment:   VerticalAlignment.Fill
                                        scalingMethod: ScalingMethod.AspectFit
                                        imageSource: stickerBubble.sLocalPath
                                    }
                                    Label {
                                        visible: stickerBubble.sLocalPath === ""
                                        text: "..."
                                        horizontalAlignment: HorizontalAlignment.Center
                                        verticalAlignment:   VerticalAlignment.Center
                                        textStyle {
                                            fontSize: FontSize.Small
                                            color: rowRoot.isDark ? Color.create("#888888") : Color.create("#808080")
                                        }
                                    }
                                }
                            }
                        } // bubble content Container

                            // Accent strip on the bubble's bottom edge — gray for mine, blue for
                            // theirs. Only the last bubble in a group ("bottom") or a standalone
                            // message ("full") gets it, so a grouped cluster doesn't look split.
                            //
                            // Height set to 0 rather than visible: false — on this Cascades version,
                            // visible:false doesn't reliably release the Container's reserved layout
                            // space, so a strip that should disappear could leave a lingering gap.
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

                        // ---- Reaction pills (kind === "message" only) -----------
                        // One square-cornered chip per distinct icon anyone reacted with,
                        // "+N" for count (see msgList.summarizePills()). Lines up with the
                        // bubble edge using the same left/right spacer widths as bubbleWrap.
                        // 6 fixed slots, not a Repeater — this QtQuick version doesn't have one.
                        Container {
                            id: reactionRow
                            horizontalAlignment: HorizontalAlignment.Fill
                            visible: rowRoot.kind === "message" && !!(ListItemData.reactions && ListItemData.reactions.length > 0)
                            topPadding: 2; bottomPadding: 4
                            layout: StackLayout { orientation: LayoutOrientation.LeftToRight }

                            Container { preferredWidth: rowRoot.mine ? 18 : 60; minWidth: rowRoot.mine ? 18 : 60; maxWidth: rowRoot.mine ? 18 : 60 }

                            Container {
                                horizontalAlignment: rowRoot.mine ? HorizontalAlignment.Right : HorizontalAlignment.Left
                                maxWidth: rowRoot.bubbleMaxW
                                layout: StackLayout { orientation: LayoutOrientation.LeftToRight }

                                // Slot 1/6
                                Container {
                                    visible: !!(ListItemData.reactions && ListItemData.reactions.length > 0)
                                    rightMargin: 4
                                    background: (ListItemData.reactions && ListItemData.reactions[0] && ListItemData.reactions[0].mine)
                                        ? Color.create("#b8c4cc") : (rowRoot.isDark ? Color.create("#3a3a3a") : Color.create("#e2e2e2"))
                                    topPadding: 4; bottomPadding: 4; leftPadding: 9; rightPadding: 9
                                    layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                                    gestureHandlers: [ TapHandler { onTapped: {
                                        if (ListItemData.reactions && ListItemData.reactions[0])
                                            rowRoot.ListItem.view.doSendReaction(ListItemData.msgId, ListItemData.cliMsgId, ListItemData.msgType, ListItemData.reactions[0].icon);
                                    } } ]
                                    ImageView { imageSource: (ListItemData.reactions && ListItemData.reactions[0]) ? ListItemData.reactions[0].asset : ""; preferredWidth: 22; preferredHeight: 22; scalingMethod: ScalingMethod.AspectFit }
                                    Label { text: (ListItemData.reactions && ListItemData.reactions[0]) ? ("+" + ListItemData.reactions[0].count) : ""; textStyle { fontSize: FontSize.Small; color: rowRoot.isDark ? Color.create("#FFFFFF") : Color.create("#444444") } leftMargin: 3 }
                                }
                                // Slot 2/6
                                Container {
                                    visible: !!(ListItemData.reactions && ListItemData.reactions.length > 1)
                                    rightMargin: 4
                                    background: (ListItemData.reactions && ListItemData.reactions[1] && ListItemData.reactions[1].mine)
                                        ? Color.create("#b8c4cc") : (rowRoot.isDark ? Color.create("#3a3a3a") : Color.create("#e2e2e2"))
                                    topPadding: 4; bottomPadding: 4; leftPadding: 9; rightPadding: 9
                                    layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                                    gestureHandlers: [ TapHandler { onTapped: {
                                        if (ListItemData.reactions && ListItemData.reactions[1])
                                            rowRoot.ListItem.view.doSendReaction(ListItemData.msgId, ListItemData.cliMsgId, ListItemData.msgType, ListItemData.reactions[1].icon);
                                    } } ]
                                    ImageView { imageSource: (ListItemData.reactions && ListItemData.reactions[1]) ? ListItemData.reactions[1].asset : ""; preferredWidth: 22; preferredHeight: 22; scalingMethod: ScalingMethod.AspectFit }
                                    Label { text: (ListItemData.reactions && ListItemData.reactions[1]) ? ("+" + ListItemData.reactions[1].count) : ""; textStyle { fontSize: FontSize.Small; color: rowRoot.isDark ? Color.create("#FFFFFF") : Color.create("#444444") } leftMargin: 3 }
                                }
                                // Slot 3/6
                                Container {
                                    visible: !!(ListItemData.reactions && ListItemData.reactions.length > 2)
                                    rightMargin: 4
                                    background: (ListItemData.reactions && ListItemData.reactions[2] && ListItemData.reactions[2].mine)
                                        ? Color.create("#b8c4cc") : (rowRoot.isDark ? Color.create("#3a3a3a") : Color.create("#e2e2e2"))
                                    topPadding: 4; bottomPadding: 4; leftPadding: 9; rightPadding: 9
                                    layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                                    gestureHandlers: [ TapHandler { onTapped: {
                                        if (ListItemData.reactions && ListItemData.reactions[2])
                                            rowRoot.ListItem.view.doSendReaction(ListItemData.msgId, ListItemData.cliMsgId, ListItemData.msgType, ListItemData.reactions[2].icon);
                                    } } ]
                                    ImageView { imageSource: (ListItemData.reactions && ListItemData.reactions[2]) ? ListItemData.reactions[2].asset : ""; preferredWidth: 22; preferredHeight: 22; scalingMethod: ScalingMethod.AspectFit }
                                    Label { text: (ListItemData.reactions && ListItemData.reactions[2]) ? ("+" + ListItemData.reactions[2].count) : ""; textStyle { fontSize: FontSize.Small; color: rowRoot.isDark ? Color.create("#FFFFFF") : Color.create("#444444") } leftMargin: 3 }
                                }
                                // Slot 4/6
                                Container {
                                    visible: !!(ListItemData.reactions && ListItemData.reactions.length > 3)
                                    rightMargin: 4
                                    background: (ListItemData.reactions && ListItemData.reactions[3] && ListItemData.reactions[3].mine)
                                        ? Color.create("#b8c4cc") : (rowRoot.isDark ? Color.create("#3a3a3a") : Color.create("#e2e2e2"))
                                    topPadding: 4; bottomPadding: 4; leftPadding: 9; rightPadding: 9
                                    layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                                    gestureHandlers: [ TapHandler { onTapped: {
                                        if (ListItemData.reactions && ListItemData.reactions[3])
                                            rowRoot.ListItem.view.doSendReaction(ListItemData.msgId, ListItemData.cliMsgId, ListItemData.msgType, ListItemData.reactions[3].icon);
                                    } } ]
                                    ImageView { imageSource: (ListItemData.reactions && ListItemData.reactions[3]) ? ListItemData.reactions[3].asset : ""; preferredWidth: 22; preferredHeight: 22; scalingMethod: ScalingMethod.AspectFit }
                                    Label { text: (ListItemData.reactions && ListItemData.reactions[3]) ? ("+" + ListItemData.reactions[3].count) : ""; textStyle { fontSize: FontSize.Small; color: rowRoot.isDark ? Color.create("#FFFFFF") : Color.create("#444444") } leftMargin: 3 }
                                }
                                // Slot 5/6
                                Container {
                                    visible: !!(ListItemData.reactions && ListItemData.reactions.length > 4)
                                    rightMargin: 4
                                    background: (ListItemData.reactions && ListItemData.reactions[4] && ListItemData.reactions[4].mine)
                                        ? Color.create("#b8c4cc") : (rowRoot.isDark ? Color.create("#3a3a3a") : Color.create("#e2e2e2"))
                                    topPadding: 4; bottomPadding: 4; leftPadding: 9; rightPadding: 9
                                    layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                                    gestureHandlers: [ TapHandler { onTapped: {
                                        if (ListItemData.reactions && ListItemData.reactions[4])
                                            rowRoot.ListItem.view.doSendReaction(ListItemData.msgId, ListItemData.cliMsgId, ListItemData.msgType, ListItemData.reactions[4].icon);
                                    } } ]
                                    ImageView { imageSource: (ListItemData.reactions && ListItemData.reactions[4]) ? ListItemData.reactions[4].asset : ""; preferredWidth: 22; preferredHeight: 22; scalingMethod: ScalingMethod.AspectFit }
                                    Label { text: (ListItemData.reactions && ListItemData.reactions[4]) ? ("+" + ListItemData.reactions[4].count) : ""; textStyle { fontSize: FontSize.Small; color: rowRoot.isDark ? Color.create("#FFFFFF") : Color.create("#444444") } leftMargin: 3 }
                                }
                                // Slot 6/6
                                Container {
                                    visible: !!(ListItemData.reactions && ListItemData.reactions.length > 5)
                                    background: (ListItemData.reactions && ListItemData.reactions[5] && ListItemData.reactions[5].mine)
                                        ? Color.create("#b8c4cc") : (rowRoot.isDark ? Color.create("#3a3a3a") : Color.create("#e2e2e2"))
                                    topPadding: 4; bottomPadding: 4; leftPadding: 9; rightPadding: 9
                                    layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                                    gestureHandlers: [ TapHandler { onTapped: {
                                        if (ListItemData.reactions && ListItemData.reactions[5])
                                            rowRoot.ListItem.view.doSendReaction(ListItemData.msgId, ListItemData.cliMsgId, ListItemData.msgType, ListItemData.reactions[5].icon);
                                    } } ]
                                    ImageView { imageSource: (ListItemData.reactions && ListItemData.reactions[5]) ? ListItemData.reactions[5].asset : ""; preferredWidth: 22; preferredHeight: 22; scalingMethod: ScalingMethod.AspectFit }
                                    Label { text: (ListItemData.reactions && ListItemData.reactions[5]) ? ("+" + ListItemData.reactions[5].count) : ""; textStyle { fontSize: FontSize.Small; color: rowRoot.isDark ? Color.create("#FFFFFF") : Color.create("#444444") } leftMargin: 3 }
                                }
                            }

                            Container { preferredWidth: rowRoot.mine ? 60 : 18; minWidth: rowRoot.mine ? 60 : 18; maxWidth: rowRoot.mine ? 60 : 18 }
                        } // reactionRow

                        // ---- Inline poll card (kind === "poll") -----------------
                        // Mirrors GroupBoardSheet.qml's poll rendering (voted/unvoted colors,
                        // tap-to-vote, fixed 0..5 slots) so it looks the same in both places.
                        // The one addition here is "View voters", which the sheet doesn't have.
                        Container {
                            id: pollCardRoot
                            visible: rowRoot.kind === "poll"
                            horizontalAlignment: HorizontalAlignment.Fill
                            topPadding: 8; bottomPadding: 8; leftPadding: 14; rightPadding: 14

                            Container {
                                horizontalAlignment: HorizontalAlignment.Fill
                                layout: StackLayout { orientation: LayoutOrientation.TopToBottom }
                                background: rowRoot.isDark ? Color.create("#26323d") : Color.create("#eef3f8")
                                topPadding: ui.du(1.2); bottomPadding: ui.du(1.2)
                                leftPadding: ui.du(1.2); rightPadding: ui.du(1.2)

                                Container {
                                    horizontalAlignment: HorizontalAlignment.Fill
                                    layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                                    bottomMargin: ui.du(0.6)
                                    ImageView {
                                        preferredWidth: ui.du(2.6); preferredHeight: ui.du(2.6)
                                        rightMargin: ui.du(0.8)
                                        imageSource: "asset:///images/ChatView/ic_list.png"
                                    }
                                    Label {
                                        layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                                        text: ListItemData.pollQuestion || ""
                                        multiline: true
                                        textStyle { fontWeight: FontWeight.Bold; fontSize: FontSize.Medium
                                                    color: rowRoot.isDark ? Color.White : Color.Black }
                                    }
                                }
                                Label {
                                    text: ListItemData.pollAllowMulti ? "Choose multiple options" : "Choose one option"
                                    textStyle { color: Color.Gray; fontSize: FontSize.XSmall }
                                    bottomMargin: ui.du(0.6)
                                }

                                Container {
                                    visible: !!(ListItemData.pollOptions && ListItemData.pollOptions.length > 0)
                                    horizontalAlignment: HorizontalAlignment.Fill
                                    background: (ListItemData.pollOptions && ListItemData.pollOptions[0] && ListItemData.pollOptions[0].voted) ? Color.create("#cfe3fa") : (rowRoot.isDark ? Color.create("#33404a") : Color.create("#f0f0f0"))
                                    topPadding: ui.du(0.8); bottomPadding: ui.du(0.8)
                                    leftPadding: ui.du(1); rightPadding: ui.du(1)
                                    bottomMargin: ui.du(0.5)
                                    layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                                    gestureHandlers: [ TapHandler { onTapped: {
                                        rowRoot.ListItem.view.votePollOptionProxy(ListItemData.pollId, ListItemData.pollOptions[0].optionId,
                                            !!ListItemData.pollAllowMulti, ListItemData.pollOptions);
                                    } } ]
                                    Label {
                                        layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                                        text: (ListItemData.pollOptions && ListItemData.pollOptions[0]) ? (ListItemData.pollOptions[0].content || "") : ""
                                        multiline: true
                                        textStyle { color: rowRoot.isDark ? Color.White : Color.Black }
                                    }
                                    Label {
                                        text: (ListItemData.pollOptions && ListItemData.pollOptions[0]) ? String(ListItemData.pollOptions[0].votes || 0) : ""
                                        textStyle { color: Color.Gray }
                                    }
                                }
                                Container {
                                    visible: !!(ListItemData.pollOptions && ListItemData.pollOptions.length > 1)
                                    horizontalAlignment: HorizontalAlignment.Fill
                                    background: (ListItemData.pollOptions && ListItemData.pollOptions[1] && ListItemData.pollOptions[1].voted) ? Color.create("#cfe3fa") : (rowRoot.isDark ? Color.create("#33404a") : Color.create("#f0f0f0"))
                                    topPadding: ui.du(0.8); bottomPadding: ui.du(0.8)
                                    leftPadding: ui.du(1); rightPadding: ui.du(1)
                                    bottomMargin: ui.du(0.5)
                                    layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                                    gestureHandlers: [ TapHandler { onTapped: {
                                        rowRoot.ListItem.view.votePollOptionProxy(ListItemData.pollId, ListItemData.pollOptions[1].optionId,
                                            !!ListItemData.pollAllowMulti, ListItemData.pollOptions);
                                    } } ]
                                    Label {
                                        layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                                        text: (ListItemData.pollOptions && ListItemData.pollOptions[1]) ? (ListItemData.pollOptions[1].content || "") : ""
                                        multiline: true
                                        textStyle { color: rowRoot.isDark ? Color.White : Color.Black }
                                    }
                                    Label {
                                        text: (ListItemData.pollOptions && ListItemData.pollOptions[1]) ? String(ListItemData.pollOptions[1].votes || 0) : ""
                                        textStyle { color: Color.Gray }
                                    }
                                }
                                Container {
                                    visible: !!(ListItemData.pollOptions && ListItemData.pollOptions.length > 2)
                                    horizontalAlignment: HorizontalAlignment.Fill
                                    background: (ListItemData.pollOptions && ListItemData.pollOptions[2] && ListItemData.pollOptions[2].voted) ? Color.create("#cfe3fa") : (rowRoot.isDark ? Color.create("#33404a") : Color.create("#f0f0f0"))
                                    topPadding: ui.du(0.8); bottomPadding: ui.du(0.8)
                                    leftPadding: ui.du(1); rightPadding: ui.du(1)
                                    bottomMargin: ui.du(0.5)
                                    layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                                    gestureHandlers: [ TapHandler { onTapped: {
                                        rowRoot.ListItem.view.votePollOptionProxy(ListItemData.pollId, ListItemData.pollOptions[2].optionId,
                                            !!ListItemData.pollAllowMulti, ListItemData.pollOptions);
                                    } } ]
                                    Label {
                                        layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                                        text: (ListItemData.pollOptions && ListItemData.pollOptions[2]) ? (ListItemData.pollOptions[2].content || "") : ""
                                        multiline: true
                                        textStyle { color: rowRoot.isDark ? Color.White : Color.Black }
                                    }
                                    Label {
                                        text: (ListItemData.pollOptions && ListItemData.pollOptions[2]) ? String(ListItemData.pollOptions[2].votes || 0) : ""
                                        textStyle { color: Color.Gray }
                                    }
                                }
                                Container {
                                    visible: !!(ListItemData.pollOptions && ListItemData.pollOptions.length > 3)
                                    horizontalAlignment: HorizontalAlignment.Fill
                                    background: (ListItemData.pollOptions && ListItemData.pollOptions[3] && ListItemData.pollOptions[3].voted) ? Color.create("#cfe3fa") : (rowRoot.isDark ? Color.create("#33404a") : Color.create("#f0f0f0"))
                                    topPadding: ui.du(0.8); bottomPadding: ui.du(0.8)
                                    leftPadding: ui.du(1); rightPadding: ui.du(1)
                                    bottomMargin: ui.du(0.5)
                                    layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                                    gestureHandlers: [ TapHandler { onTapped: {
                                        rowRoot.ListItem.view.votePollOptionProxy(ListItemData.pollId, ListItemData.pollOptions[3].optionId,
                                            !!ListItemData.pollAllowMulti, ListItemData.pollOptions);
                                    } } ]
                                    Label {
                                        layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                                        text: (ListItemData.pollOptions && ListItemData.pollOptions[3]) ? (ListItemData.pollOptions[3].content || "") : ""
                                        multiline: true
                                        textStyle { color: rowRoot.isDark ? Color.White : Color.Black }
                                    }
                                    Label {
                                        text: (ListItemData.pollOptions && ListItemData.pollOptions[3]) ? String(ListItemData.pollOptions[3].votes || 0) : ""
                                        textStyle { color: Color.Gray }
                                    }
                                }
                                Container {
                                    visible: !!(ListItemData.pollOptions && ListItemData.pollOptions.length > 4)
                                    horizontalAlignment: HorizontalAlignment.Fill
                                    background: (ListItemData.pollOptions && ListItemData.pollOptions[4] && ListItemData.pollOptions[4].voted) ? Color.create("#cfe3fa") : (rowRoot.isDark ? Color.create("#33404a") : Color.create("#f0f0f0"))
                                    topPadding: ui.du(0.8); bottomPadding: ui.du(0.8)
                                    leftPadding: ui.du(1); rightPadding: ui.du(1)
                                    bottomMargin: ui.du(0.5)
                                    layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                                    gestureHandlers: [ TapHandler { onTapped: {
                                        rowRoot.ListItem.view.votePollOptionProxy(ListItemData.pollId, ListItemData.pollOptions[4].optionId,
                                            !!ListItemData.pollAllowMulti, ListItemData.pollOptions);
                                    } } ]
                                    Label {
                                        layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                                        text: (ListItemData.pollOptions && ListItemData.pollOptions[4]) ? (ListItemData.pollOptions[4].content || "") : ""
                                        multiline: true
                                        textStyle { color: rowRoot.isDark ? Color.White : Color.Black }
                                    }
                                    Label {
                                        text: (ListItemData.pollOptions && ListItemData.pollOptions[4]) ? String(ListItemData.pollOptions[4].votes || 0) : ""
                                        textStyle { color: Color.Gray }
                                    }
                                }
                                Container {
                                    visible: !!(ListItemData.pollOptions && ListItemData.pollOptions.length > 5)
                                    horizontalAlignment: HorizontalAlignment.Fill
                                    background: (ListItemData.pollOptions && ListItemData.pollOptions[5] && ListItemData.pollOptions[5].voted) ? Color.create("#cfe3fa") : (rowRoot.isDark ? Color.create("#33404a") : Color.create("#f0f0f0"))
                                    topPadding: ui.du(0.8); bottomPadding: ui.du(0.8)
                                    leftPadding: ui.du(1); rightPadding: ui.du(1)
                                    bottomMargin: ui.du(0.5)
                                    layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                                    gestureHandlers: [ TapHandler { onTapped: {
                                        rowRoot.ListItem.view.votePollOptionProxy(ListItemData.pollId, ListItemData.pollOptions[5].optionId,
                                            !!ListItemData.pollAllowMulti, ListItemData.pollOptions);
                                    } } ]
                                    Label {
                                        layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                                        text: (ListItemData.pollOptions && ListItemData.pollOptions[5]) ? (ListItemData.pollOptions[5].content || "") : ""
                                        multiline: true
                                        textStyle { color: rowRoot.isDark ? Color.White : Color.Black }
                                    }
                                    Label {
                                        text: (ListItemData.pollOptions && ListItemData.pollOptions[5]) ? String(ListItemData.pollOptions[5].votes || 0) : ""
                                        textStyle { color: Color.Gray }
                                    }
                                }

                                Label {
                                    text: "View voters"
                                    topMargin: ui.du(0.6)
                                    textStyle { color: Color.create("#2575fc"); fontSize: FontSize.Small }
                                    gestureHandlers: [ TapHandler { onTapped: {
                                        rowRoot.ListItem.view.viewPollVotersProxy(ListItemData.pollId);
                                    } } ]
                                }
                            }
                        }

                        } // rowContentRoot

                    }
                }
            ]
        }

        // EmojiPanel is created lazily on first open to avoid its ~800ms creation
        // cost delaying every chat tap. The slot container stays in the layout so
        // the panel snaps into the right position once created.
        Container {
            id: emojiPanelSlot
            horizontalAlignment: HorizontalAlignment.Fill
            topMargin: ui.du(0.8)
            visible: false
        }

        // Single-row quick-message suggestion bar, mirrors attachPreviewBar's
        // structure/height instead of a tall scrollable list. Shows only the best match.
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
            }
        }

        // Attachment preview bar, shown when an image is staged for sending
        // Layout: [X] [thumbnail] [filename]. Caption is typed in the input field.
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

            // Cancel pending attachment
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

        // Reply staging bar, shown above the input while a reply is pending
        // Same visual language as attachPreviewBar for a consistent "queued to send" look
        // Layout: [X] [colored quote strip] [sender + snippet]
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

            // Cancel pending reply
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

            // Colored accent strip, matches the quote-block strip in the bubble itself
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
            background: chatViewPage.isDark ? Color.create("#272727") : Color.create("#FFFFFF")
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
                onClicked: { attachPickerSheet.open() }
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
                // Without this, Cascades defaults initial focus to the title bar's
                // voice-call button instead of the message input
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
                        // 23du = dot-row (2.5du) + 2 emoji rows sized to EmojiButton's 7du
                        // height + grid padding (1du) + category bar (5.5du). 28du left extra
                        // space that GridLayout spread as padding, making emoji look stretched.
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
        // Group Board: pushed via groupBoardRequested, same flip-a-bool pattern as
        // qmRequested, since ChatView can't push into its own parent Nav directly.
        // Disabled (not hidden) outside groups, same convention as Block/Leave group.
        ActionItem {
            title: "Group board"
            imageSource: "asset:///images/ChatView/ic_sb_notes.png"
            ActionBar.placement: ActionBarPlacement.InOverflow
            enabled: chatViewPage.isGroup
            onTriggered: { groupBoardUnderDevDialog.show(); }
        },
        ActionItem {
            title: "Leave group"
            imageSource: "asset:///images/ChatView/ic_chat_leave.png"
            ActionBar.placement: ActionBarPlacement.InOverflow
            enabled: chatViewPage.isGroup
            onTriggered: { leaveGroupDialog.show() }
        },
        // Writes a real event to the device calendar via app.createTodayEvent()
        // (wraps bb::pim::calendar::CalendarService). Always today's date.
        // Subject defaults to "Meeting with <thread name>", shown for review in
        // createEventDialog before creating (not editable — ConfirmDialog has no text input).
        ActionItem {
            title: "Create today's event"
            imageSource: "asset:///images/ChatView/ic_create_event.png"
            ActionBar.placement: ActionBarPlacement.InOverflow
            onTriggered: {
                createEventDialog.eventSubject = "Meeting with " + chatViewPage.threadName;
                createEventDialog.show();
            }
        }
    ]

    // --- Quick Messages "/command" autocomplete --------------------------
    // Finds the "active" slash command at the end of the current text — the last
    // "/" that starts the string or follows whitespace, with no whitespace after it
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
        // Pick the best match: exact name wins outright, otherwise shortest prefix match
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
            // qmsgType is Zalo's own client-message-type code (1=text, 32=photo),
            // not the same numbering as our local msgType (1=text, 2=photo)
            var qServerType = (chatViewPage.pendingReplyMsgType === 2 || chatViewPage.pendingReplyMsgType === "2") ? 32 : 1;
            var qContent = (chatViewPage.pendingReplyMsgType === 2 || chatViewPage.pendingReplyMsgType === "2")
                           ? "[Photo]" : chatViewPage.pendingReplyContent;
            zService.sendMessageQuote(chatViewPage.threadId, txt, chatViewPage.isGroup,
                chatViewPage.pendingReplyMsgId, chatViewPage.pendingReplyCliMsgId,
                chatViewPage.pendingReplyOwnerId, qContent, qServerType, chatViewPage.pendingReplyTs,
                chatViewPage.pendingReplySenderName);
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

    // Generic single-key string extractor for our own flat JSON content shapes
    // (e.g. link-share's {"linkTitle":..,"linkThumb":..}) — used at chatViewPage
    // scope (onNewMessage) where linkBubble delegate's local
    // extractJsonStringField() isn't reachable.
    function extractJsonField(content, key) {
        if (typeof content !== "string" || content.length === 0) return "";
        var re = new RegExp("\"" + key + "\"\\s*:\\s*\"([^\"]*)\"");
        var m = content.match(re);
        return (m && m[1]) ? m[1] : "";
    }

    // Photo caption is a "caption" key inside the photo's content JSON. Same
    // regex-extraction style as extractPhotoUrl(), but also un-escapes what the
    // C++ side escaped when building the JSON.
    function extractPhotoCaption(content) {
        if (typeof content !== "string" || content.length === 0) return "";
        if (content.charAt(0) !== "{") return "";
        var m = content.match(/"caption"\s*:\s*"((?:[^"\\]|\\.)*)"/);
        if (!m || !m[1]) return "";
        return m[1].replace(/\\n/g, "\n").replace(/\\r/g, "\r")
                   .replace(/\\t/g, "\t").replace(/\\"/g, "\"").replace(/\\\\/g, "\\");
    }

    // Call before any direct msgModel.append()/clear() so a pending deferred flush
    // from an earlier rebuildGroups() call lands first — otherwise new data can end
    // up out of order, or get silently overwritten. See rebuildGroups() below for why.
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

    // Dùng chung bởi filePicker/audioPicker/voiceNoteSheet/contact-vcf —
    // 4 nguồn file khác nhau nhưng cùng 1 luồng: build local echo bubble
    // (msgType=3, "local_file_" prefix) rồi gọi sendFile(). path phải là
    // đường dẫn hệ thống thật, KHÔNG có tiền tố "file://" (khớp cách
    // FilePicker.selectedFiles và VoiceNoteSheet.voiceNoteReady đều trả về).
    function stageAndSendLocalFile(path) {
        var fname = path.substring(path.lastIndexOf('/') + 1);
        var mf = {
            msgId:    "local_file_" + new Date().getTime(),
            content:  JSON.stringify({ fileName: fname, href: "", fileSize: 0 }),
            msgType:  3,
            isMine:   true,
            isGroup:  chatViewPage.isGroup,
            senderId: "self",
            dName:    chatViewPage.selfName,
            ts:       String(new Date().getTime()),
            selfName: chatViewPage.selfName
        };
        msgModel.append(mf);
        chatViewPage.rebuildGroups(true);
        msgList.uploadFileActive = true;
        msgList.uploadFilePercent = 0;
        zService.sendFile(chatViewPage.threadId, path, chatViewPage.isGroup);
    }

    function rebuildGroups(scrollAfter) {
        // Data-loss race fix: if rebuildGroups() deferred an update onto
        // rebuildFlushTimer and a second message arrives before that timer fires,
        // the second call's pendingItems would overwrite the first's — silently
        // losing the first message. Confirmed on-device: a reply sent right after
        // an incoming photo made that photo disappear.
        //
        // Fix: if a flush is already pending, apply it synchronously right now
        // before doing anything else, closing the race window.
        chatViewPage.flushPendingRebuild();

        var size = msgModel.size();
        if (size === 0) return;

        var items = [];
        // Snapshot each row's current grouped/bubblePos before mutating it.
        // msgModel.value(i) returns the same object items[i] points to and later
        // mutates, so comparing them afterward was always comparing an object
        // against itself post-mutation. Capturing plain values up front instead
        // makes the before/after comparison actually work.
        var prevGrouped = [];
        var prevBubblePos = [];
        for (var i = 0; i < size; i++) {
            var v = msgModel.value(i);
            items.push(v);
            prevGrouped.push(v.grouped);
            prevBubblePos.push(v.bubblePos);
        }

        // Normalize a timestamp (seconds or ms) to milliseconds. Unconfirmed "local_"
        // rows carry a device-clock ts; bump those by clockOffsetMs so every ts
        // being compared is on the same clock.
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

            // Photo messages (msgType 2) always stand alone, never merged into a text bubble
            var curIsPhoto  = (cur.msgType  === 2 || cur.msgType  === "2");
            var prevIsPhoto = prev ? (prev.msgType === 2 || prev.msgType === "2") : false;
            var nextIsPhoto = next ? (next.msgType === 2 || next.msgType === "2") : false;

            // Text messages always stand alone now (bubblePos always "full"), no
            // 5-minute grouping window. Photo grouping is untouched by this.
            var curId  = cur.msgId  || "";
            var prevId = prev ? (prev.msgId || "") : "";
            var nextId = next ? (next.msgId || "") : "";
            var curLocal  = curId.indexOf("local_")  === 0;
            var prevLocal = prevId.indexOf("local_") === 0;
            var nextLocal = nextId.indexOf("local_") === 0;

            var curTs  = toMs(cur.ts,               curLocal);
            var prevTs = toMs(prev ? prev.ts : null, prevLocal);
            var nextTs = toMs(next ? next.ts : null, nextLocal);
            // Always false — text messages never merge based on timing
            var withinPrev = false;
            var withinNext = false;

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

            // The group header always shows the last message's time — back-propagate
            // to the group start, stopping at the group boundary
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

        // replace() only swaps data in the ArrayDataModel, it doesn't force a
        // re-measure — so when grouped/bubblePos actually changes for a row (which
        // topPadding/bottomPadding depend on), its height can go stale until
        // something else forces a full relayout.
        //
        // Per-row removeAt+insert fixes that but has its own race: several messages
        // arriving close together each trigger their own remove/insert animation,
        // and a later one can interrupt an earlier still-running one, leaving a row
        // stuck at a half-finished height.
        //
        // Collapsing every row's update into one clear()+append() avoids the race —
        // one atomic reset instead of N animated per-row mutations. Only paid when
        // something actually needs to re-layout; unchanged rows still use cheap
        // in-place replace().
        var anyLayoutChanged = false;
        for (var i = 0; i < size; i++) {
            if (prevGrouped[i] !== items[i].grouped || prevBubblePos[i] !== items[i].bubblePos) {
                anyLayoutChanged = true;
                break;
            }
        }
        // Diagnostic: logs anyLayoutChanged plus each row's prev vs new grouped/
        // bubblePos, to confirm which path actually runs. Safe to remove.
        {
            var diagLine = "[Zalo QML] anyLayoutChanged=" + anyLayoutChanged;
            for (var di3 = 0; di3 < size; di3++) {
                diagLine += " | [" + di3 + "] id=" + String(items[di3].msgId).slice(-6)
                    + " prevGrp=" + prevGrouped[di3] + " newGrp=" + items[di3].grouped
                    + " prevPos=" + prevBubblePos[di3] + " newPos=" + items[di3].bubblePos;
            }
            console.log(diagLine);
        }
        if (anyLayoutChanged) {
            // clear()+append() alone doesn't force a remeasure — the ListView was
            // reusing pooled Control instances with a stale measured height. Several
            // approaches were tried and rejected (see git history): a synchronous
            // dataModel=null;dataModel=msgModel bounce turned out to be a no-op
            // since it's the same model reference, so there's nothing for Cascades
            // to diff.
            //
            // Deferring the re-append to the next event loop turn (via a zero-interval
            // Timer) gives Cascades a real empty-dataModel layout pass in between,
            // which is what actually forces it to drop the pooled item heights.
            //
            // Diagnostic: dumps every row's msgId/sender/content-length/grouped/
            // bubblePos, reading from items[] since msgModel is deferred-empty until
            // rebuildFlushTimer fires. Safe to remove.
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

        // Diagnostic: same dump as above, for the cheap-path (no layout change) branch
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

        // Receives emojiPicked from the lazily-created EmojiPanel instance
        // target is set explicitly (not via .connect()) — dynamic signal connections
        // aren't reliable on BB10. target: null until the real instance is assigned.
        Connections {
            id: emojiSignalCon
            target: null
            onEmojiPicked: {
                inputField.text = inputField.text + charStr
            }
        },

        // ---- Copy & Share (bubble hold-menu) ----
        SystemToast {
            id: copyToast
            body: "Copied to clipboard"
            position: SystemUiPosition.MiddleCenter
        },

        // Clears jumpHighlightMsgId a couple seconds after jumpToMessage() sets it,
        // so the highlight is transient instead of sticking on the row
        Timer {
            id: jumpHighlightTimer
            interval: 2000
            repeat: false
            onTriggered: { chatViewPage.jumpHighlightMsgId = ""; }
        },

        // Generic error toast for Delete/Recall/Download failures, body set by the caller
        SystemToast {
            id: errorToast
            position: SystemUiPosition.MiddleCenter
        },

        // Separate from copyToast — its body is a static "Copied to clipboard" binding
        // that reusing here for Download would permanently overwrite
        SystemToast {
            id: downloadToast
            body: "Saved to Downloads"
            position: SystemUiPosition.MiddleCenter
        },

        // Feedback for doPin()'s zService.pinGroupMessage() call, body set right before show()
        SystemToast {
            id: pinToast
            position: SystemUiPosition.MiddleCenter
        },

        Connections {
            target: zService
            onPinMessageDone: {
                if (success) {
                    pinToast.body = "Message pinned";
                    pinToast.show();
                    chatViewPage.loadBoardItems();
                } else {
                    errorToast.body = (error && error.length > 0) ? ("Pin failed: " + error) : "Pin failed";
                    errorToast.show();
                }
            }
        },

        // Feeds PinboardBar and upserts every poll item into msgModel as an inline
        // card. Filtered by groupId so a stale fetch for a thread we've left can't
        // clobber this one's bar/cards.
        Connections {
            target: zService
            onGroupBoardReady: {
                if (groupId !== chatViewPage.threadId) return;
                chatViewPage.boardItems = items;
                // POLL DISABLED (temporary — keep project lean): uncomment to re-enable
                // inline poll cards in ChatView
                // for (var i = 0; i < items.length; i++) {
                //     if (items[i].boardType === "poll") chatViewPage.upsertPollRow(items[i]);
                // }
            }
        },
        Connections {
            target: zService
            onCreatePollDone: {
                if (success) {
                    chatViewPage.loadBoardItems();
                }
            }
        },
        Connections {
            target: zService
            onCreateNoteDone: {
                if (success) {
                    chatViewPage.loadBoardItems();
                }
            }
        },
        Connections {
            target: zService
            onVotePollDone: {
                if (success) {
                    chatViewPage.loadBoardItems();
                    // POLL DISABLED (temporary): chatViewPage.bumpPollToBottom(pollId, updatedOptions, null);
                }
            }
        },

        // Others' board activity while this thread is open — a separate signal from
        // the Hub OS notification (suppressed for the active thread) to cover that
        // gap in-app. isSelf events are skipped since the *Done handlers above
        // already cover our own actions.
        Connections {
            target: zService
            onBoardEventOccurred: {
                if (groupId !== chatViewPage.threadId || isSelf) return;
                chatViewPage.loadBoardItems();
                // update_board fires for both "poll created" and "someone voted" —
                // fetching full detail covers both cases via bumpPollToBottom
                if (topicType === 3 && topicId.length > 0 && act !== "remove_board" && act !== "remove_topic") {
                    zService.getPollDetail(topicId);
                }
            }
        },
        Connections {
            target: zService
            onPollDetailReady: {
                if (error && error.length > 0) return;
                // POLL DISABLED (temporary): chatViewPage.bumpPollToBottom(pollId, detail.options || [], detail);
            }
        },
        // Real-time reaction from the server — either another member's tap, or the
        // WS echo of our own (harmless no-op re-apply). icon === "" means removed.
        // Filtered by threadId so a stale push can't touch this screen's msgModel.
        Connections {
            target: zService
            onReactionUpdated: {
                if (threadId !== chatViewPage.threadId) return;
                msgList.applyReactionRecord(msgId, uid, icon);
            }
        },

        // Video/file download progress for the tapped bubble or the long-press
        // "Download" action. pendingVideoOpenMsgId (set in playVideoMsg) tells
        // Finished whether to auto-open (tap-to-play) or just toast (Download
        // menu item) — matches doDownloadVideo() clearing it to "".
        Connections {
            target: zService
            onVideoUploadProgress: {
                if (threadId !== chatViewPage.threadId) return;
                msgList.uploadVideoActive = (percent < 100);
                msgList.uploadVideoPercent = percent;
            }
        },
        // sendFile() upload progress — same shape as video's, on its own
        // signal so a file upload's % doesn't drive the video bubble's UI.
        Connections {
            target: zService
            onFileUploadProgress: {
                if (threadId !== chatViewPage.threadId) return;
                msgList.uploadFileActive = (percent < 100);
                msgList.uploadFilePercent = percent;
            }
        },
        Connections {
            target: zService
            onVideoDownloadProgress: {
                msgList.updateVideoBubbleProgress(msgId, percent);
            }
        },
        Connections {
            target: zService
            onVideoDownloadFinished: {
                msgList.updateVideoBubbleProgress(msgId, 100, true);
                if (msgList.pendingVideoOpenMsgId === msgId && msgId.length > 0) {
                    app.openLocalFile(localPath);
                } else {
                    downloadToast.show();
                }
                msgList.pendingVideoOpenMsgId = "";
            }
        },
        Connections {
            target: zService
            onVideoDownloadFailed: {
                msgList.updateVideoBubbleProgress(msgId, 0, true);
                msgList.pendingVideoOpenMsgId = "";
                errorToast.body = "Video download failed" + (errorMsg && errorMsg.length > 0 ? (": " + errorMsg) : "");
                errorToast.show();
            }
        },

        PollVotersSheet { id: pollVotersSheet },
        ForwardPickerSheet { id: forwardPickerSheet; isDark: chatViewPage.isDark },
        ReactionPickerSheet {
            id: reactionPickerSheet
            onReacted: { msgList.doSendReaction(msgId, cliMsgId, msgType, icon); }
        },

        SharePickerSheet { id: sharePicker },

        // "app" is a stable context property set once at startup (unlike
        // chatsNav.activeChatPage, which starts null) — safe to bind target directly
        Connections {
            target: app
            onShareTargetsReady: {
                shareDimFadeOut.play();
                sharePicker.openWithTargets(targets);
            }
        },

        // Dim overlay shown while querying share targets
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
            id: groupBoardUnderDevDialog
            title: "Group Board"
            body: "This feature is still under development."
        },

        Connections {
            target: app

            onShowRecalledMessagesChanged: {
                chatViewPage.showRecalledMessages = show;
            }
            // Cùng cơ chế như showRecalledMessagesChanged ở trên — nếu user
            // bật/tắt dark mode trong Settings trong khi ChatView này vẫn
            // đang mở, isDark (bind 1 lần từ app.getDarkTheme() lúc trang
            // tạo — xem property ở đầu file) sẽ không tự đổi màu; signal
            // này ép nó cập nhật ngay.
            onDarkThemeChanged: {
                chatViewPage.isDark = dark;
            }
        },

        Connections {
            target: zService

            onMessagesReady: {
                if (threadId !== chatViewPage.threadId) return;
                // Must run before any direct msgModel read/append — see flushPendingRebuild()
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
                msgList.uploadVideoActive = false;
                msgList.uploadVideoPercent = 0;
                msgList.uploadFileActive = false;
                msgList.uploadFilePercent = 0;
                if (!success) {
                    chatViewPage.removeLocalPlaceholder(chatViewPage.pendingMsg);
                    inputField.text = chatViewPage.pendingMsg;
                    for (var ri = msgModel.size() - 1; ri >= 0; ri--) {
                        var ritem = msgModel.value(ri);
                        if (ritem.msgId && (ritem.msgId.indexOf("local_img_") === 0
                                         || ritem.msgId.indexOf("local_file_") === 0
                                         || ritem.msgId.indexOf("local_video_") === 0)) {
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
                // Fixes the "reply right after a photo makes the photo vanish" bug —
                // this handler appends directly without going through rebuildGroups(),
                // so a pending deferred flush from the previous message needs to be
                // applied first. See flushPendingRebuild().
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
                    // Server confirmation of our own outgoing message — cliMsgId is the
                    // device-clock ts, msg.ts is the real server ts. See updateClockOffset().
                    chatViewPage.updateClockOffset(msg.cliMsgId, msg.ts);

                    if (msg.msgType === 2 || msg.msgType === "2") {
                        // Early dedup — HTTP confirm and WS echo can both fire for the same msgId
                        if (msg.msgId) {
                            for (var di0 = 0; di0 < msgModel.size(); di0++) {
                                var dv0 = msgModel.value(di0);
                                if (dv0.msgId === msg.msgId && dv0.msgId.indexOf("local_") !== 0) {
                                    // Already stored — only patch localImage if newly available
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
                        // The server confirmation never carries a file-size field, only the
                        // placeholder did — re-inject it or the size disappears from the bubble
                        if (savedFileSize > 0 && (msg.content || "").indexOf('"fileSize"') < 0) {
                            if (msg.content && msg.content.charAt(0) === "{")
                                msg.content = msg.content.slice(0, -1) + ',"fileSize":' + savedFileSize + '}';
                            else
                                msg.content = '{"fileSize":' + savedFileSize + '}';
                        }

                        if (placeholderIdx >= 0) {
                            // Replace in place instead of remove+append — avoids a brief
                            // duplicate-looking flicker when HTTP and WS confirmations land close together
                            msgModel.replace(placeholderIdx, msg);
                            handledInPlace = true;
                        } else if (savedLocalImage.length === 0 && msg.localImage && msg.localImage.length > 0) {
                            // Placeholder already consumed by an earlier confirmation — if we
                            // already have a row with this exact image, update it instead of
                            // adding a duplicate
                            for (var li = msgModel.size() - 1; li >= 0; li--) {
                                if (msgModel.value(li).localImage === msg.localImage) {
                                    msgModel.replace(li, msg);
                                    handledInPlace = true;
                                    break;
                                }
                            }
                        }

                        // Last-resort dedup: HTTP confirm and WS echo for the same photo can
                        // carry different msgIds (HTTP's id is occasionally 0/missing). Merge
                        // any other mine/photo row sent in the last 8s with the same caption
                        // instead of appending a duplicate.
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
                    } else if (msg.msgType === 3 || msg.msgType === "3") {
                        // Video's counterpart to the photo dedup/replace block above — was
                        // previously missing entirely, so a confirmed video message fell
                        // through to the generic text-only removeLocalPlaceholder() (which
                        // never matches, since the placeholder's content differs from the
                        // confirmed one: empty href/fileSize:0 vs the real href/size) and then
                        // got appended as a second row, leaving the "local_video_..." row
                        // stuck forever at "Sending...". Restarting the app "fixed" it only
                        // because the DB never persisted the orphaned placeholder in the
                        // first place — replacing in place here fixes it without a restart.

                        // Early dedup — HTTP confirm and WS echo can both fire for the same msgId
                        if (msg.msgId) {
                            for (var vdi0 = 0; vdi0 < msgModel.size(); vdi0++) {
                                var vdv0 = msgModel.value(vdi0);
                                if (vdv0.msgId === msg.msgId && vdv0.msgId.indexOf("local_") !== 0) {
                                    return;
                                }
                            }
                        }

                        // local_file_ (document attachment) placeholders reconcile the
                        // same way as local_video_ — both are msgType 3 and share the
                        // same {"fileName","href","fileSize"} content shape.
                        var vPlaceholderIdx = -1;
                        for (var vpi = msgModel.size() - 1; vpi >= 0; vpi--) {
                            var vpitem = msgModel.value(vpi);
                            if (vpitem.msgId && (vpitem.msgId.indexOf("local_video_") === 0
                                              || vpitem.msgId.indexOf("local_file_") === 0)) {
                                vPlaceholderIdx = vpi;
                                break;
                            }
                        }
                        if (vPlaceholderIdx >= 0) {
                            // Replace in place instead of remove+append — avoids a brief
                            // duplicate-looking flicker when HTTP and WS confirmations land
                            // close together, same reasoning as the photo branch above.
                            msgModel.replace(vPlaceholderIdx, msg);
                            handledInPlace = true;
                        }
                    } else if (msg.msgType === 6 || msg.msgType === "6") {
                        // Link-share bubble's counterpart to the photo/video dedup blocks
                        // above. Root cause matching the video bug's comment: the plain-text
                        // "local_..." placeholder created at send time (content = the raw
                        // URL the user typed, msgType left unset — see the "sendText"
                        // placeholder builder) never matches removeLocalPlaceholder()'s exact
                        // content === content check against the WS echo's confirmed content
                        // (server rewrites it to {"linkTitle":...,"linkHref":...} JSON, see
                        // mt==6 handling in ZaloService_WebSocket.cpp). So the placeholder was
                        // never removed and a duplicate row was appended — on screen this
                        // could show either as two stacked bubbles or, when rebuildGroups()
                        // grouped them together, only the last-appended (JSON) row visible.
                        //
                        // Fix: replace the newest plain-text "local_" placeholder in place
                        // (same approach as the video/photo branches — match by proximity/
                        // recency, not brittle string equality) instead of leaving both rows.

                        // Early dedup — HTTP confirm and WS echo can both fire for the same msgId
                        if (msg.msgId) {
                            for (var ldi0 = 0; ldi0 < msgModel.size(); ldi0++) {
                                var ldv0 = msgModel.value(ldi0);
                                if (ldv0.msgId === msg.msgId && ldv0.msgId.indexOf("local_") !== 0) {
                                    return;
                                }
                            }
                        }

                        var lPlaceholderIdx = -1;
                        for (var lpi = msgModel.size() - 1; lpi >= 0; lpi--) {
                            var lpitem = msgModel.value(lpi);
                            // Plain-text placeholder only ("local_..." but not the photo/
                            // video/file-specific prefixes, which have their own dedup above).
                            if (lpitem.msgId && lpitem.msgId.indexOf("local_") === 0
                                && lpitem.msgId.indexOf("local_img_") !== 0
                                && lpitem.msgId.indexOf("local_video_") !== 0
                                && lpitem.msgId.indexOf("local_file_") !== 0) {
                                lPlaceholderIdx = lpi;
                                break;
                            }
                        }
                        if (lPlaceholderIdx >= 0) {
                            msgModel.replace(lPlaceholderIdx, msg);
                            handledInPlace = true;
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
                        // Same msgId already recorded — typically the HTTP send-confirm's
                        // optimistic row (device-clock ts) getting a second echo from the WS
                        // confirm (real server ts). Adopt the newer ts when it differs, or the
                        // row stays stuck on a stale device-clock ts (wrong time, wrong grouping).
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
                } else if (msg.msgType === 6 || msg.msgType === "6") {
                    // Link-share thumbnail: ImageView in Cascades QML 1.0 cannot load a
                    // remote https:// URL directly ("Unsupported scheme (https) ... Image
                    // loading aborted" — confirmed on-device), same limitation as the
                    // photo/avatar paths. Reuse the existing downloadImageMessage() pipeline
                    // (fetches to a local temp file, then emits imageMsgReady ->
                    // applyImageUpdate() -> msg.localImage) instead of binding linkThumb
                    // straight to ImageView.imageSource.
                    if (!msg.localImage || msg.localImage.length === 0) {
                        var linkThumbUrl3 = chatViewPage.extractJsonField(msg.content || "", "linkThumb");
                        if (linkThumbUrl3.length > 0)
                            zService.downloadImageMessage(msg.msgId, linkThumbUrl3, chatViewPage.threadId);
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

            onStickerReady: {
                if (chatViewPage.pageVisible)
                    chatViewPage.applyStickerUpdate(stickerId, localPath);
                // No pending-queue fallback for the not-visible case (unlike
                // onImageMsgReady above) — a sticker still on screen when the
                // user navigates away will just re-request the download (now
                // a fast local-file-exists hit) next time this thread opens.
            }

            onMessageRecalled: {
                if (threadId !== chatViewPage.threadId) return;
                chatViewPage.applyRecall(msgId);
            }

            // Companion to the C++-side fix in ZaloService_WebSocket.cpp: once the DB
            // row's device-clock ts gets corrected to the real server ts via the WS echo,
            // patch the in-memory row too if it's currently loaded — otherwise it'd keep
            // the stale ts until the thread is reopened.
            onMessageTsCorrected: {
                if (threadId !== chatViewPage.threadId) return;
                chatViewPage.applyTsCorrection(msgId, newTs);
            }

            // Result of our own recallMessage() call. On success, apply the same
            // update as an incoming chat.undo (applyRecall) instead of waiting for a
            // WS echo. On failure, show why via a toast.
            onMessageRecalledDone: {
                if (threadId !== chatViewPage.threadId) return;
                if (success) {
                    chatViewPage.applyRecall(msgId);
                } else {
                    errorToast.body = error.length > 0 ? error : "Failed to recall message";
                    errorToast.show();
                }
            }

            // Result of our own deleteMessage() call. On success, remove the bubble
            // right away (applyLocalDelete) instead of waiting for the WS confirmation
            // in onMessageDeletedLocally below. On failure, show why.
            onMessageDeleted: {
                if (threadId !== chatViewPage.threadId) return;
                if (success) {
                    chatViewPage.applyLocalDelete(msgId);
                } else {
                    errorToast.body = error.length > 0 ? error : "Failed to delete message";
                    errorToast.show();
                }
            }

            // WS confirmation of our own "delete for me", filtered server-side to
            // only fire for deletions we performed. Harmless no-op if onMessageDeleted
            // above already removed the bubble.
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

        AttachPickerSheet {
            id: attachPickerSheet
            onPictureSelected:  { imagePicker.open(); }
            onVideoSelected:    { videoPicker.open(); }
            onFileSelected:     { filePicker.open(); }
            onAudioSelected:    { audioPicker.open(); }
            onVoiceNoteSelected: { voiceNoteSheet.open(); }
            // pickContact() sống thẳng trên zService (không có class/bridge
            // riêng) — mở ContactPicker, build .vcf, rồi emit
            // contactVcfReady/contactPickError, xử lý ở Connections bên dưới.
            // Truyền threadId để kết quả trả về đúng Page này — bắt buộc,
            // xem ghi chú dài ở khai báo pickContact() trong ZaloService.hpp.
            onContactSelected:  { zService.pickContact(chatViewPage.threadId); }
        },

        // Voice Note recording sheet — ghi ra .m4a rồi gửi qua sendFile()
        // giống mọi file đính kèm khác (msgType=3, cùng bubble/pipeline).
        VoiceNoteSheet {
            id: voiceNoteSheet
            onVoiceNoteReady: {
                chatViewPage.stageAndSendLocalFile(path);
            }
            onVoiceNoteError: {
                errorToast.body = message;
                errorToast.show();
            }
        },

        // Kết quả pickContact() (mở từ AttachPickerSheet.onContactSelected
        // ở trên) — .vcf build xong thì gửi luôn qua sendFile(), y hệt
        // VoiceNoteSheet.onVoiceNoteReady. Không hiện toast khi bị huỷ
        // (contactPickError("canceled")) vì đó là hành động chủ động của
        // người dùng, không phải lỗi; chỉ báo toast cho reason "error".
        //
        // BẮT BUỘC lọc theo threadId ở cả 2 handler: zService là singleton
        // toàn app, và mỗi ChatView Page từng được push vẫn có thể còn sống
        // trong NavigationPane history (chưa bị destroy khi pop) — nếu
        // không lọc, MỌI Page còn sống đều nhận cùng 1 signal và tự gửi
        // file, gây gửi trùng nhiều lần (bug đã xảy ra thật: chọn 1 contact
        // gửi ra 3 file .vcf vì 3 ChatView Page cùng lắng nghe không lọc).
        Connections {
            target: zService
            onContactVcfReady: {
                if (threadId !== chatViewPage.threadId) return;
                chatViewPage.stageAndSendLocalFile(path);
            }
            onContactPickError: {
                if (threadId !== chatViewPage.threadId) return;
                if (reason !== "canceled") {
                    errorToast.body = "Could not get contact";
                    errorToast.show();
                }
            }
        },

        FilePicker {
            id: imagePicker
            type: FileType.Picture
            mode: FilePickerMode.Picker
            title: "Select Picture"
            onFileSelected: {
                var path = selectedFiles[0];
                // Cache the picked photo right away, not just on Send — the picker's
                // own path may be transient/sandboxed
                var cachedPath = zService.cacheLocalImage(path);
                // Stage the image — clears any pending reply first, since the two
                // staging bars are mutually exclusive
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
            }
        },

        FilePicker {
            id: videoPicker
            type: FileType.Video
            mode: FilePickerMode.Picker
            title: "Select Video"
            onFileSelected: {
                var path = selectedFiles[0];
                var ext  = path.substring(path.lastIndexOf('.') + 1).toLowerCase();
                if (ext !== "mp4") {
                    errorToast.body = "Only .mp4 videos are supported";
                    errorToast.show();
                    return;
                }
                var fname = path.substring(path.lastIndexOf('/') + 1);
                var mf = {
                    msgId:    "local_video_" + new Date().getTime(),
                    content:  JSON.stringify({ fileName: fname, href: "", fileSize: 0 }),
                    msgType:  3,
                    isMine:   true,
                    isGroup:  chatViewPage.isGroup,
                    senderId: "self",
                    dName:    chatViewPage.selfName,
                    ts:       String(new Date().getTime()),
                    selfName: chatViewPage.selfName
                };
                msgModel.append(mf);
                chatViewPage.rebuildGroups(true);
                msgList.uploadVideoActive = true;
                msgList.uploadVideoPercent = 0;
                zService.sendVideo(chatViewPage.threadId, path, chatViewPage.isGroup);
            }
        },

        // File đính kèm dạng bất kỳ. FileType.Other là type ĐÚNG cho nút
        // "File" này (không đổi sang type khác) — nó cho picker hiển thị
        // MỌI định dạng, không lọc theo Picture/Music/Video. Trước đây có
        // 1 whitelist chỉ cho qua doc/docx/ppt/pptx/xls/xlsx/txt/pdf sau khi
        // picker đã cho chọn, chặn silently mọi định dạng khác bằng toast
        // lỗi — mâu thuẫn với việc picker vốn hiển thị mọi loại. Đã BỎ
        // whitelist: cho gửi bất kỳ file nào người dùng chọn qua picker này.
        // Cùng shape content {"fileName","href","fileSize"} và cùng msgType=3
        // như video (xem ghi chú ở videoBubble trong bubble delegate) — nhờ vậy
        // dùng chung được 1 bubble QML lẫn pipeline reconcile/tải-về/mở file,
        // chỉ khác icon hiển thị (theo phần mở rộng, xem iconFor() ở
        // videoBubble — bao gồm cả các định dạng chưa được liệt kê icon riêng,
        // sẽ rơi về icon Video mặc định cho tới khi được thêm). sendFile() giờ
        // dùng chung chunked-upload (<=512K/chunk) với sendVideo() — an toàn
        // cho file nặng (vd. ~30MB) và có % tiến trình thật qua fileUploadProgress.
        FilePicker {
            id: filePicker
            type: FileType.Other
            mode: FilePickerMode.Picker
            title: "Select File"
            onFileSelected: {
                chatViewPage.stageAndSendLocalFile(selectedFiles[0]);
            }
        },

        // Audio = chọn 1 file âm thanh có sẵn (mp3/flac/m4a/...) từ máy —
        // khác Voice Note (ghi âm mới). Cùng pipeline gửi/nhận/bubble với
        // File ở trên (msgType=3, chunked upload) — chỉ khác nguồn file.
        FilePicker {
            id: audioPicker
            type: FileType.Music
            mode: FilePickerMode.Picker
            title: "Select Audio"
            onFileSelected: {
                chatViewPage.stageAndSendLocalFile(selectedFiles[0]);
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
        },

        // Confirms the event subject before writing it to the calendar. Plain property
        // rather than a text field since ConfirmDialog has no input control — subject
        // is computed once at trigger time and shown for confirmation.
        ConfirmDialog {
            id: createEventDialog
            property string eventSubject: ""
            title: "Create today's event"
            body: "Create \"" + eventSubject + "\" in your calendar for today?"
            confirmLabel: "Create"
            onConfirmed: {
                app.createTodayEvent(createEventDialog.eventSubject,
                "Create on Zalo10 - " + chatViewPage.threadName, 30);
            }
        }
    ]
}
