import bb.cascades 1.4
import bb.cascades.pickers 1.0
import bb.system 1.0
import QtQuick 1.0

Page {
    id: chatViewPage

    property string threadId:    ""
    property string threadName:  ""
    property bool   isGroup:     false
    // TODO: no group-role data yet, so this is hardcoded false and "Delete for everyone" stays hidden.
    property bool   isCurrentUserAdminOrOwner: false
    property string avatarUrl:   ""
    property string selfName:    ""
    property string pendingMsg:  ""
    property bool   initialized: false
    property bool   emojiPanelOpen: false
    property variant emojiPanelRef: null
    property string pendingAttachPath: ""
    property string pendingAttachName: ""
    property string pendingReplyMsgId:      ""
    property string pendingReplyCliMsgId:   ""
    property string pendingReplyOwnerId:    ""
    property string pendingReplySenderName: ""
    property string pendingReplyContent:    ""
    property int    pendingReplyMsgType:    0
    property string pendingReplyTs:         ""
    property variant qmMatch: null
    property variant dbIsMineCache: ({})
    property variant msgStates: ({})
    property bool   isMuted: false
    property bool   isBlocked: false
    property bool   popRequested: false
    property bool   qmRequested: false
    // Flip a bool and let the owning Nav react (same as qmRequested): ChatView can't push into its
    // own NavigationPane.
    property bool   groupBoardRequested: false
    property variant boardItems: []
    property variant removedBoardIds: ({})
    // pollId -> true while a getPollDetail() requested for a board event is in flight.
    // pollDetailReady also fires for PollVotersSheet lookups; without this guard, opening the
    // voters list would move the poll card to the bottom.
    property variant pollBumpPending: ({})
    property variant pendingImageUpdates: ([])
    property bool   pageVisible: false
    property bool   isDark: app.getDarkTheme()
    property bool   showRecalledMessages: app.getShowRecalledMessages()
    property bool   searchVisible: false
    property string searchText: ""
    property variant searchMatches: []
    property int      searchMatchPos: -1
    property string jumpHighlightMsgId: ""

    // Windowed history: only a bounded slice lives in msgModel (one ListView with a long history
    // lagged/crashed BB10). The newest historyPageSize rows load on open; scrolling to the top loads
    // another page from SQLite (requestOlderMessages) up to historyMaxRows.
    property int  historyPageSize: 30
    property int  historyMaxRows:  240
    property bool loadingOlder:  false
    property bool hasMoreOlder:  true
    property bool olderArmed:    false  // Set shortly after open so the initial scroll-to-end can't trigger a load.

    property real clockOffsetMs: 0
    property bool clockOffsetSet: false

    titleBar: TitleBar {
        scrollBehavior: TitleBarScrollBehavior.Sticky
        kind: TitleBarKind.FreeForm
        kindProperties: FreeFormTitleBarKindProperties {
            content: Container {
                background: Color.create("#2575fc")
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment:   VerticalAlignment.Fill
                layout: DockLayout {}

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
                    Container {
                        preferredWidth: ui.du(0.2)
                    }
                }

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
        // msgModel.replace() doesn't make the ImageView delegate re-bind imageSource on this Cascades
        // version (a blank image stays stuck). clear() + deferred re-append (rebuildFlushTimer) forces
        // an empty layout pass that drops the stale pooled Controls.
        msgModel.clear();
        rebuildFlushTimer.pendingItems = items;
        rebuildFlushTimer.pendingScroll = false;
        rebuildFlushTimer.start();
    }

    // Sticker counterpart of applyImageUpdate: patch the rows, then rebuild (a sticker can appear
    // several times in a thread). stickerBubble reads ListItemData.stickerLocalPath as a plain
    // property: calling zService from deep inside a nested delegate was unreliable on device.
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

    function updateClockOffset(cliMsgId, serverTs) {
        var cli = parseInt(cliMsgId || "0", 10);
        var srv = parseInt(serverTs || "0", 10);
        if (srv > 0 && srv < 1e12) srv *= 1000;
        if (cli <= 0 || srv <= 0) return;
        var diff = srv - cli;
        if (Math.abs(diff) < 60000) return;
        chatViewPage.clockOffsetMs = diff;
        chatViewPage.clockOffsetSet = true;
    }

    function applyLocalDelete(msgId) {
        chatViewPage.flushPendingRebuild();
        var size = msgModel.size();
        for (var j = 0; j < size; j++) {
            var d = msgModel.value(j);
            if ((d.msgId || "") === msgId) {
                msgModel.removeAt(j);
                // Re-run grouping: removing a row changes bubblePos of its neighbors.
                rebuildGroups();
                return;
            }
        }
    }

    function applyTsCorrection(msgId, newTs) {
        chatViewPage.flushPendingRebuild();
        var size = msgModel.size();
        for (var j = 0; j < size; j++) {
            var d = msgModel.value(j);
            if ((d.msgId || "") === msgId) {
                if (d.ts === newTs) return;
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
                // Keep the original content for "Show Recalled Messages". Idempotent: the server can redeliver
                // the recall, so capture the text only the first time or a second call overwrites it with "".
                if (!d.recalledOriginalContent || d.recalledOriginalContent.length === 0) {
                    d.recalledOriginalContent = d.content || "";
                }
                d.content    = "";
                d.msgType    = 99;
                msgModel.replace(j, d);
                return;
            }
        }
    }

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

    function filterBoardItems(items) {
        var rem = chatViewPage.removedBoardIds || {};
        var out = [];
        var src = items || [];
        for (var i = 0; i < src.length; i++) {
            if (rem[String(src[i].id)]) continue;
            if (src[i].boardType === "poll" && src[i].closed === true) continue;
            out.push(src[i]);
        }
        return out;
    }

    function loadBoardItems() {
        if (!chatViewPage.isGroup || chatViewPage.threadId.length === 0) {
            chatViewPage.boardItems = [];
            return;
        }
        zService.fetchGroupBoard(chatViewPage.threadId, 1, 50);
    }

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

    function openPollVote(pollId) {
        if (!pollId || pollId.length === 0) return;
        chatViewPage.flushPendingRebuild();
        var idx = chatViewPage.findPollRowIndex(pollId);
        if (idx < 0) return;
        pollVoteSheet.isDark = chatViewPage.isDark;
        pollVoteSheet.openFor(msgModel.value(idx), chatViewPage.threadId);
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
            pollCreatorId: item.creatorId || "",
            pollCreatedTs: item.createTime || 0,
            pollNumVote: item.numVote || 0
        };
    }

    function insertPollAtChronological(row) {
        var insertIdx = msgModel.size();
        for (var i = 0; i < msgModel.size(); i++) {
            var r = msgModel.value(i);
            var rts = r ? (r.latestTs || r.ts || 0) : 0;
            if (rts && rts > row.ts) { insertIdx = i; break; }
        }
        msgModel.insert(insertIdx, row);
    }

    // Called from groupBoardReady for every poll on the board. Never repositions a known row (a
    // refresh isn't necessarily a vote); only inserts newly-seen polls at their chronological slot.
    // bumpPollToBottom() moves a card, and only runs on an explicit vote.
    // Returns true if a row was inserted.
    function upsertPollRow(item) {
        var idx = chatViewPage.findPollRowIndex(item.id);
        var row = chatViewPage.buildPollRow(item);
        if (idx >= 0) { msgModel.replace(idx, row); return false; }
        chatViewPage.insertPollAtChronological(row);
        return true;
    }

    function removePollRow(pollId) {
        var idx = chatViewPage.findPollRowIndex(pollId);
        if (idx < 0) return false;
        msgModel.removeAt(idx);
        return true;
    }

    function syncPollRows(items) {
        chatViewPage.flushPendingRebuild();
        var rem = chatViewPage.removedBoardIds || {};
        var structural = false;
        for (var i = 0; i < items.length; i++) {
            var it = items[i];
            if (it.boardType !== "poll") continue;
            if (it.closed === true || rem[String(it.id)] || zService.isLocallyHidden("poll_" + it.id)) {
                if (chatViewPage.removePollRow(it.id)) structural = true;
            } else {
                if (chatViewPage.upsertPollRow(it)) structural = true;
            }
        }
        if (structural) chatViewPage.rebuildGroups();
    }

    function bumpPollToBottom(pollId, updatedOptions, detail) {
        if (zService.isLocallyHidden("poll_" + pollId)) return;
        chatViewPage.flushPendingRebuild();
        var idx = chatViewPage.findPollRowIndex(pollId);
        var row;
        if (idx >= 0) {
            row = msgModel.value(idx);
            msgModel.removeAt(idx);
            row = JSON.parse(JSON.stringify(row));
        } else if (detail) {
            row = {
                kind: "poll", msgId: "poll_" + pollId, pollId: pollId, ts: Date.now(),
                pollQuestion: detail.question || "", pollAllowMulti: !!detail.allowMultiChoices,
                pollCreatorId: detail.creator || "",
                pollCreatedTs: detail.createTime || 0,
                pollNumVote: detail.numVote || 0
            };
        } else {
            return;
        }
        row.pollOptions = updatedOptions || [];
        if (detail && detail.numVote !== undefined) row.pollNumVote = detail.numVote;
        row.ts = Date.now();
        msgModel.append(row);
        chatViewPage.rebuildGroups(true);
    }

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

        // Cancel any deferred rebuild flush from the previous thread: applying it would leak the old
        // thread's messages into this model.
        rebuildFlushTimer.stop();
        rebuildFlushTimer.pendingItems = null;
        rebuildFlushTimer.pendingScroll = false;
        rebuildFlushTimer.pendingAnchor = -1;

        msgModel.clear();
        zService.setActiveThread(chatViewPage.threadId, chatViewPage.isGroup);

        msgList.reactionsByMsg = zService.dbLoadThreadReactions(chatViewPage.threadId) || {};

        chatViewPage.msgStates = zService.getThreadMessageStates(chatViewPage.threadId) || {};
        chatViewPage.loadingOlder = false;
        chatViewPage.olderArmed   = false;
        olderArmTimer.restart();
        var cached = zService.dbLoadMessagesPage(chatViewPage.threadId, "", chatViewPage.historyPageSize);
        chatViewPage.hasMoreOlder = !!(cached && cached.length >= chatViewPage.historyPageSize);
        if (cached && cached.length > 0) {
            var newCache = {};
            for (var i = 0; i < cached.length; i++) {
                var c = cached[i];
                c.selfName = chatViewPage.selfName || "Me";
                c.isMine = (c.isMine === "true" || c.isMine === 1 || c.isMine === true);
                if (c.msgId) newCache[c.msgId] = c.isMine;
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
                    // The link thumbnail is a remote https:// URL that ImageView can't load: use the
                    // download-to-local-file pipeline, as for photos.
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
        chatViewPage.loadingOlder = false;
        chatViewPage.hasMoreOlder = true;
        chatViewPage.olderArmed   = false;
        olderLoadTimer.stop();
        chatViewPage.dbIsMineCache = {};
        chatViewPage.msgStates = {};
        chatViewPage.removedBoardIds = {};
        chatViewPage.pollBumpPending = {};
        chatViewPage.boardItems = [];
    }

    content: Container {
        layout: StackLayout { orientation: LayoutOrientation.TopToBottom }
        horizontalAlignment: HorizontalAlignment.Fill
        verticalAlignment:   VerticalAlignment.Fill
        background: chatViewPage.isDark ? Color.create("#1a1a1a") : Color.create("#d6d6d6")

        PinboardBar {
            id: pinboardBar
            items: chatViewPage.boardItems
            isDark: chatViewPage.isDark
            horizontalAlignment: HorizontalAlignment.Fill
            onItemTapped: {
                if (boardType === "pin") {
                    chatViewPage.jumpToMessage(itemId);
                } else if (boardType === "poll") {
                    chatViewPage.jumpToMessage("poll_" + itemId);
                } else {
                    groupBoardUnderDevDialog.show();
                }
            }
            onMoreRequested: { groupBoardUnderDevDialog.show(); }
        }

        Container {
            id: olderStrip
            visible: chatViewPage.loadingOlder
            horizontalAlignment: HorizontalAlignment.Fill
            background: chatViewPage.isDark ? Color.create("#262626") : Color.create("#c4c4c4")
            topPadding: ui.du(0.8)
            bottomPadding: ui.du(0.8)

            Container {
                horizontalAlignment: HorizontalAlignment.Center
                layout: StackLayout { orientation: LayoutOrientation.LeftToRight }

                ActivityIndicator {
                    verticalAlignment: VerticalAlignment.Center
                    preferredWidth: ui.du(3.5)
                    preferredHeight: ui.du(3.5)
                    running: olderStrip.visible
                }
                Label {
                    verticalAlignment: VerticalAlignment.Center
                    leftMargin: ui.du(1)
                    text: "Loading older messages..."
                    textStyle.fontSize: FontSize.XSmall
                    textStyle.color: chatViewPage.isDark ? Color.create("#b0b0b0") : Color.create("#555555")
                }
            }
        }

        ListView {
            id: msgList
            property real   widthHint: msgListLUH.layoutFrame.width
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
            // Proxies: ListItemComponent delegates are a separate scope, so calling chatViewPage.foo()
            // directly throws "Can't find variable: chatViewPage".
            function highlightMatchesProxy(text, query, color) { return chatViewPage.highlightMatches(text, query, color); }
            function jumpToMessageProxy(msgId) { chatViewPage.jumpToMessage(msgId); }
            // uid -> name lookup built from getmg-v2 currentMems (the wire dName is unreliable).
            // Returns "" if unknown.
            function memberDisplayNameProxy(uid) { return zService.memberDisplayName(uid); }
            function votePollOptionProxy(pollId, optionId, allowMulti, options) { chatViewPage.doVotePollOption(pollId, optionId, allowMulti, options); }
            function viewPollVotersProxy(pollId) { chatViewPage.openPollVoters(pollId); }
            function openPollVoteProxy(pollId) { chatViewPage.openPollVote(pollId); }
            function openGroupBoardProxy() { groupBoardUnderDevDialog.show(); }
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
            property int uploadVideoPercent: 0
            property bool uploadVideoActive: false
            property bool uploadFileActive: false
            property int  uploadFilePercent: 0
            horizontalAlignment: HorizontalAlignment.Fill
            layoutProperties: StackLayoutProperties { spaceQuota: 1 }
            dataModel: msgModel
            bottomPadding: ui.du(1.5)
            flickMode: FlickMode.Momentum

            attachedObjects: [
                LayoutUpdateHandler { id: msgListLUH },
                ArrayDataModel { id: msgModel },
                Timer {
                    id: rebuildFlushTimer
                    property variant pendingItems: null
                    property bool    pendingScroll: false
                    // Index of the row that was first before a "load older" prepend; scrolled back to so the view
                    // doesn't jump.
                    property int     pendingAnchor: -1
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
                        if (rebuildFlushTimer.pendingAnchor >= 0) {
                            msgList.scrollToItem([rebuildFlushTimer.pendingAnchor], ScrollAnimation.None);
                            rebuildFlushTimer.pendingAnchor = -1;
                            chatViewPage.loadingOlder = false;
                        }
                    }
                },
                Timer {
                    id: olderLoadTimer
                    interval: 150
                    repeat: false
                    onTriggered: chatViewPage.doLoadOlderMessages()
                },
                Timer {
                    id: olderArmTimer
                    interval: 1500
                    repeat: false
                    onTriggered: chatViewPage.olderArmed = true
                },
                ListScrollStateHandler {
                    id: msgScrollState
                    onAtBeginningChanged: {
                        if (atBeginning) chatViewPage.requestOlderMessages();
                    }
                    onFirstVisibleItemChanged: {
                        if (firstVisibleItem && firstVisibleItem.length > 0 && firstVisibleItem[0] <= 2)
                            chatViewPage.requestOlderMessages();
                    }
                }
            ]

            function doCopy(content, isPhoto, localImage) {
                if (isPhoto) {
                    errorToast.body = "Copy isn't available for photos";
                    errorToast.show();
                    return;
                }
                app.copyToClipboard(content);
                copyToast.show();
            }
            function doReply(msgId, cliMsgId, senderId, isMine, rawDName, content, msgType, ts) {
                if (chatViewPage.pendingAttachPath.length > 0) {
                    chatViewPage.pendingAttachPath = "";
                    chatViewPage.pendingAttachName = "";
                }
                var resolvedName;
                if (isMine) {
                    resolvedName = chatViewPage.selfName || "Me";
                } else if (!chatViewPage.isGroup && chatViewPage.threadName.length > 0) {
                    // 1-1 thread: wire dName is unreliable (it can carry our own name); use threadName.
                    resolvedName = chatViewPage.threadName;
                } else if (chatViewPage.isGroup) {
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
            property variant reactionAssets: ({
                "like":  "asset:///images/ChatView/emoji/people/emoji_1f44d_64.png",
                "heart": "asset:///images/ChatView/emoji/people/emoji_2764_64.png",
                "haha":  "asset:///images/ChatView/emoji/people/emoji_1f604_64.png",
                "wow":   "asset:///images/ChatView/emoji/people/emoji_1f631_64.png",
                "cry":   "asset:///images/ChatView/emoji/people/emoji_1f62d_64.png",
                "angry": "asset:///images/ChatView/emoji/people/emoji_1f621_64.png"
            })
            property variant reactionRTypes: ({ "like": 0, "heart": 1, "haha": 2, "wow": 3, "cry": 4, "angry": 5 })
            function reactionAssetFor(iconId) { return msgList.reactionAssets[iconId] || ""; }
            function reactionRType(iconId)    { return (iconId in msgList.reactionRTypes) ? msgList.reactionRTypes[iconId] : 0; }

            property variant reactionsByMsg: ({})

            function findMsgRowIndexById(msgId) {
                for (var i = 0; i < msgModel.size(); i++) {
                    var r = msgModel.value(i);
                    if (r && r.msgId === msgId) return i;
                }
                return -1;
            }

            // Pure: reactionsByMsg[msgId] -> [{icon, asset, count, mine}] ordered by first reactor, so new
            // reactions add a pill on the right instead of reordering.
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

            function refreshReactionsRow(msgId) {
                var idx = msgList.findMsgRowIndexById(msgId);
                if (idx < 0) return;
                var row = msgModel.value(idx);
                row = JSON.parse(JSON.stringify(row));
                row.reactions = msgList.summarizePills(msgId);
                msgModel.replace(idx, row);
            }

            function applyReactionRecord(msgId, uid, icon) {
                // `property variant` returns a fresh copy on every read on this engine, so mutating a nested
                // path silently no-ops. Read into a local, mutate it, write back in one assignment.
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

            function doSendReaction(msgId, cliMsgId, msgType, iconId) {
                if (!msgId || msgId.length === 0) return;
                var mine = (msgList.reactionsByMsg[msgId] && msgList.reactionsByMsg[msgId][zService.selfUid])
                           ? msgList.reactionsByMsg[msgId][zService.selfUid].icon : "";
                var removing = (mine === iconId);
                var newIcon  = removing ? "" : iconId;
                msgList.applyReactionRecord(msgId, zService.selfUid, newIcon);
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
            function doResend(msgId) { chatViewPage.resendMessage(msgId); }
            function doRecallMsg(msgId, cliMsgId, isMine) {
                if (!isMine) {
                    errorToast.body = "You can only recall your own messages";
                    errorToast.show();
                    return;
                }
                zService.recallMessage(chatViewPage.threadId, chatViewPage.isGroup, msgId, cliMsgId);
            }
            function doForward(msgId, content, msgType, ts) { forwardPickerSheet.openFor(content || "", msgType || 0, msgId || "", ts || ""); }
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
            function playVideoMsg(msgId, href, fileName) {
                msgList.pendingVideoOpenMsgId = msgId;
                zService.downloadVideoMessage(msgId, href, fileName);
            }
            function doDownloadVideo(msgId, href, fileName) {
                if (!href || href.length === 0) {
                    errorToast.body = "Video not available";
                    errorToast.show();
                    return;
                }
                msgList.pendingVideoOpenMsgId = "";
                zService.downloadVideoMessage(msgId, href, fileName);
            }
            // The sticker bubble doesn't trigger its download from QML: it hit "Can't find variable:
            // zService" from Component.onCompleted/onVisibleChanged in the nested delegate. The C++ side
            // calls downloadSticker() eagerly when normalizing chat.sticker; stickerBubble only reads
            // ListItemData.stickerLocalPath once applyStickerUpdate() patches it.
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
                // Poll cards are synthetic rows ("poll_<id>") built from the group board; the server has no
                // message to delete. Hide locally and remember it, or the next board fetch (on every open)
                // brings the card back.
                if ((msgId || "").indexOf("poll_") === 0) {
                    zService.hideLocalMessage(chatViewPage.threadId, msgId);
                    chatViewPage.applyLocalDelete(msgId);
                    return;
                }
                zService.deleteMessage(chatViewPage.threadId, chatViewPage.isGroup, msgId, cliMsgId, senderId, true);
            }
            function doDeleteMsg(msgId)    {}

            listItemComponents: [
                ListItemComponent {
                    type: ""
                    CustomListItem {
                        id: rowRoot
                        highlightAppearance: HighlightAppearance.None
                        dividerVisible: false

                        // Helper functions for bindings: block-body bindings ({ var x; ...; return x; }) are a
                        // parse-time error on this QtQuick1/Cascades engine ("Expected token `numeric literal'").
                        // Every multi-statement binding in this delegate must go through a named function.
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
                        function photoStatusText(mine, msgId, failed) {
                            var mid = msgId || "";
                            if (mine && mid.indexOf("local_img_") === 0) return failed ? "Not sent" : "Sending...";
                            return mine ? "Picture sent" : "Photo";
                        }

                        // Do NOT set preferredHeight on rowRoot (even bound to a child's layoutFrame.height, or -1): it
                        // is self-referential, locks the first measured height and clips later wrapped lines.
                        // CustomListItem also has no padding or background property; the divider/highlight gap is
                        // cancelled with a fixed negative bottomMargin on rowContentRoot.

                        // ActionItem/DeleteActionItem has no "visible" property (it extends UIObject, not Control):
                        // build two full ActionSets (member vs admin/owner) and pick one. A failed bubble gets its own
                        // set with "Resend" under "Reaction".
                        contextActions: [
                            rowRoot.sendFailed ? bubbleActionsFailed
                                : (rowRoot.isAdminOrOwner ? bubbleActionsAdmin : bubbleActionsMember)
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
                                id: bubbleActionsFailed
                                title: "Message not sent"
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
                                    title: "Resend"
                                    imageSource: "asset:///images/ChatView/ic_resend.png"
                                    onTriggered: { rowRoot.ListItem.view.doResend(ListItemData.msgId); }
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

                        property string searchQuery: ListItem.view.searchQuery
                        property bool   isCurrentSearchMatch: rowRoot.searchQuery.length > 0
                                                               && ListItem.view.searchCurrentMsgIndex === ListItem.indexPath[0]

                        property bool mine: (ListItemData.isMine === true
                                             || ListItemData.isMine === "true"
                                             || ListItemData.isMine === 1)
                        property string kind: ListItemData.kind || "message"
                        property bool grouped: ListItemData.grouped === true
                        onGroupedChanged: {
                        }
                        property bool recalled: (ListItemData.msgType === 99 || ListItemData.msgType === "99")
                        property bool showRecalledSetting: ListItem.view.showRecalledMessages
                        property string recalledOriginal: ListItemData.recalledOriginalContent || ""
                        property bool recalledIsPhoto: (rowRoot.recalledOriginal.length > 0
                                                         && rowRoot.recalledOriginal.charAt(0) === "{"
                                                         && (rowRoot.recalledOriginal.indexOf("normalUrl") >= 0
                                                             || rowRoot.recalledOriginal.indexOf("thumbUrl") >= 0
                                                             || rowRoot.recalledOriginal.indexOf("thumb") >= 0
                                                             || rowRoot.recalledOriginal.indexOf("href") >= 0))
                                                        || !!(ListItemData.localImage && ListItemData.localImage !== "")
                        property bool recalledHasOriginalText: rowRoot.recalledOriginal.length > 0 && !rowRoot.recalledIsPhoto
                        property bool recalledHidden: rowRoot.recalled
                                                       && !(rowRoot.showRecalledSetting
                                                            && (rowRoot.recalledHasOriginalText || rowRoot.recalledIsPhoto))

                        property string bubblePos: ListItemData.bubblePos || "full"
                        property bool sendFailed: ListItemData.sendFailed === true
                        property int outState: rowRoot.mine
                            ? (rowRoot.sendFailed ? 4
                                : ((String(ListItemData.msgId || "").indexOf("local_") === 0)
                                    ? 0 : (ListItemData.msgState || 1)))
                            : -1
                        onBubblePosChanged: {
                        }
                        property real pollInnerW: Math.max(0, (rowLUH.layoutFrame.width > 94 ? rowLUH.layoutFrame.width
                                                  : (ListItem.view.widthHint > 94 ? ListItem.view.widthHint : 0)) - 40 - ui.du(2.0))
                        function pollFillPct(opts, numVote, i) {
                            if (!opts || !opts[i]) return 0;
                            var base = numVote || 0;
                            if (base <= 0) {
                                for (var k = 0; k < opts.length; k++) base += (opts[k].votes || 0);
                            }
                            if (base <= 0) return 0;
                            var pct = Math.round(100 * (opts[i].votes || 0) / base);
                            return Math.max(0, Math.min(100, pct));
                        }
                        property real bubbleMaxW: rowLUH.layoutFrame.width > 94
                                                   ? (rowLUH.layoutFrame.width - 94)
                                                   : (ListItem.view.widthHint > 94
                                                      ? (ListItem.view.widthHint - 94)
                                                      : ui.du(40))

                        property bool hasQuote: !!(ListItemData.quoteMsgId && ListItemData.quoteMsgId.length > 0)

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

                        // Zalo's WS dName can carry OUR OWN name. 1-1: use chatViewPage.threadName; group:
                        // memberDisplayNameProxy (m_memberNames from getmg-v2 currentMems). Falls back to the wire dName.
                        property string otherMemberName: ListItem.view.isGroupChat
                            ? ListItem.view.memberDisplayNameProxy(ListItemData.senderId || "")
                            : ""
                        property string otherDisplayName: (!ListItem.view.isGroupChat && ListItem.view.threadNameProxy.length > 0)
                            ? ListItem.view.threadNameProxy
                            : ((ListItem.view.isGroupChat && rowRoot.otherMemberName.length > 0)
                               ? rowRoot.otherMemberName
                               : (ListItemData.dName || "Unknown"))

                        property bool isJumpHighlighted: ListItem.view.jumpHighlightMsgId.length > 0
                                                          && ListItem.view.jumpHighlightMsgId === (ListItemData.msgId || "")
                        property bool isHighlighted: rowRoot.isCurrentSearchMatch || rowRoot.isJumpHighlighted

                        Container {
                            id: rowContentRoot
                            horizontalAlignment: HorizontalAlignment.Fill
                            layout: StackLayout { orientation: LayoutOrientation.TopToBottom }
                            // Constant offsets cancelling CustomListItem's invisible divider gap. They must be plain
                            // constants, not bindings to a measured height (that clipped long messages). Tuned per
                            // bubblePos ("bottom" and "middle" rows measure differently).
                            property int gapCompensation:
                                rowRoot.bubblePos === "middle" ? -6 : -12
                            bottomMargin: rowRoot.kind === "message" ? gapCompensation : 0
                            attachedObjects: [
                                LayoutUpdateHandler {
                                    id: rowContentRootLUH
                                    onLayoutFrameChanged: {
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
                                    onLayoutFrameChanged: {
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
                            preferredWidth: rowRoot.bubbleMaxW
                            maxWidth:       rowRoot.bubbleMaxW
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
                                layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                                horizontalAlignment: HorizontalAlignment.Fill
                                topMargin: 0; bottomMargin: 0

                                // State icon: incoming = grey pellet; outgoing = sending -> sent -> delivered (D) -> read (R).
                                // One ImageView per state toggled via visible: rebinding imageSource on a replaced row is
                                // unreliable on this Cascades version (see applyImageUpdate).
                                Container {
                                    visible: !rowRoot.recalled
                                    verticalAlignment: VerticalAlignment.Top
                                    topPadding:   7
                                    rightPadding: 8
                                Container {
                                    layout: DockLayout {}
                                    preferredWidth:  38
                                    minWidth:        38
                                    maxWidth:        38
                                    preferredHeight: 38
                                    minHeight:       38
                                    ImageView {
                                        visible: rowRoot.outState === -1
                                        imageSource: "asset:///images/ChatView/bubble/message_states/grey_pellet.png"
                                        horizontalAlignment: HorizontalAlignment.Fill
                                        verticalAlignment: VerticalAlignment.Fill
                                        scalingMethod: ScalingMethod.AspectFit
                                    }
                                    ImageView {
                                        visible: rowRoot.outState === 0
                                        imageSource: "asset:///images/ChatView/bubble/message_states/sending.png"
                                        horizontalAlignment: HorizontalAlignment.Fill
                                        verticalAlignment: VerticalAlignment.Fill
                                        scalingMethod: ScalingMethod.AspectFit
                                    }
                                    ImageView {
                                        visible: rowRoot.outState === 1
                                        imageSource: "asset:///images/ChatView/bubble/message_states/sent.png"
                                        horizontalAlignment: HorizontalAlignment.Fill
                                        verticalAlignment: VerticalAlignment.Fill
                                        scalingMethod: ScalingMethod.AspectFit
                                    }
                                    ImageView {
                                        visible: rowRoot.outState === 2
                                        imageSource: "asset:///images/ChatView/bubble/message_states/delivered.png"
                                        horizontalAlignment: HorizontalAlignment.Fill
                                        verticalAlignment: VerticalAlignment.Fill
                                        scalingMethod: ScalingMethod.AspectFit
                                    }
                                    ImageView {
                                        visible: rowRoot.outState === 3
                                        imageSource: "asset:///images/ChatView/bubble/message_states/read.png"
                                        horizontalAlignment: HorizontalAlignment.Fill
                                        verticalAlignment: VerticalAlignment.Fill
                                        scalingMethod: ScalingMethod.AspectFit
                                    }
                                    ImageView {
                                        visible: rowRoot.outState === 4
                                        imageSource: "asset:///images/ChatView/bubble/message_states/failed.png"
                                        horizontalAlignment: HorizontalAlignment.Fill
                                        verticalAlignment: VerticalAlignment.Fill
                                        scalingMethod: ScalingMethod.AspectFit
                                    }
                                }
                                }

Container {
                                id: msgContentRoot
                                horizontalAlignment: HorizontalAlignment.Fill
                                layoutProperties: StackLayoutProperties { spaceQuota: 1 }
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

                                    Label {
                                        id: photoCaptionLbl
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

                                    Container {
                                        visible: photoCaptionLbl.visible
                                        horizontalAlignment: HorizontalAlignment.Fill
                                        preferredHeight: 1
                                        background: rowRoot.isDark ? Color.create("#555555") : Color.create("#cccccc")
                                        bottomMargin: 6
                                    }

                                    Container {
                                        id: photoFrame
                                        horizontalAlignment: HorizontalAlignment.Left
                                        property real photoMaxW: Math.max(1, rowRoot.bubbleMaxW - 28)
                                        property bool hasDims: !!(ListItemData.imgWidth  && ListItemData.imgWidth  > 0
                                                                  && ListItemData.imgHeight && ListItemData.imgHeight > 0)
                                        property bool hasImage: !!(ListItemData.localImage && ListItemData.localImage !== "")
                                        property real actualW: photoFrameLUH.layoutFrame.width
                                        property real ratio: hasDims ? (ListItemData.imgHeight / ListItemData.imgWidth) : 1
                                        preferredWidth:  hasDims ? Math.min(photoMaxW, ListItemData.imgWidth)
                                                                 : Math.min(photoMaxW, ui.du(30))
                                        preferredHeight: hasDims
                                                         ? ((actualW > 0 ? Math.min(actualW, photoMaxW)
                                                                         : Math.min(photoMaxW, ListItemData.imgWidth)) * ratio)
                                                         : ui.du(30)
                                        background: hasImage ? Color.Transparent
                                                             : (rowRoot.isDark ? Color.create("#3a3a3a") : Color.create("#e0e0e0"))
                                        layout: DockLayout {}
                                        attachedObjects: [ LayoutUpdateHandler { id: photoFrameLUH } ]

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

                                    Label {
                                        text: rowRoot.photoStatusText(rowRoot.mine, ListItemData.msgId, rowRoot.sendFailed)
                                        textStyle {
                                            fontSize: FontSize.XSmall
                                            fontStyle: FontStyle.Italic
                                            color: Color.create("#888888")
                                        }
                                        topMargin: 4; bottomMargin: 0
                                    }
                                }

                                Container {
                                    id: videoBubble
                                    visible: !rowRoot.recalled
                                             && (ListItemData.msgType === 3 || ListItemData.msgType === "3")
                                    horizontalAlignment: HorizontalAlignment.Fill
                                    topMargin: 2; bottomMargin: 2
                                    layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                                    maxWidth: rowRoot.bubbleMaxW

                                    // Use plain functions with ListItemData.content passed explicitly, not IIFE bindings
                                    // ((function(){...})()): the dependency on ListItemData.content inside the closure wasn't always
                                    // tracked, so a bubble created just as the message arrived could keep stale content (garbled
                                    // filename/href).
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
                                    function extFor(fileName) {
                                        var n = fileName || "";
                                        var di = n.lastIndexOf(".");
                                        return di >= 0 ? n.substring(di + 1).toLowerCase() : "";
                                    }
                                    function iconFor(ext) {
                                        if (ext === "txt")                 return "asset:///images/ChatView/bubble/file_types/File Type - TXT.png";
                                        if (ext === "doc" || ext === "docx") return "asset:///images/ChatView/bubble/file_types/File Type - Document.png";
                                        if (ext === "pdf")                 return "asset:///images/ChatView/bubble/file_types/File Type - PDF.png";
                                        if (ext === "ppt" || ext === "pptx") return "asset:///images/ChatView/bubble/file_types/File Type - PPT.png";
                                        if (ext === "xls" || ext === "xlsx") return "asset:///images/ChatView/bubble/file_types/File Type - XLS (Spreadsheet).png";
                                        if (ext === "epub")                return "asset:///images/ChatView/bubble/file_types/File Type - ePUB.png";
                                        if (ext === "apk")                 return "asset:///images/ChatView/bubble/file_types/File Type - APK.png";
                                        if (ext === "cer")                 return "asset:///images/ChatView/bubble/file_types/File Type - Certificate.png";
                                        if (ext === "zip" || ext === "rar" || ext === "7z") return "asset:///images/ChatView/bubble/file_types/File Type - ZIP (Compressed).png";
                                        if (ext === "vcf")                 return "asset:///images/ChatView/bubble/file_types/File Type - VCF (Contanct).png";
                                        if (ext === "mp3" || ext === "flac") return "asset:///images/ChatView/bubble/file_types/File Type - Music.png";
                                        if (ext === "m4a")                 return "asset:///images/ChatView/bubble/file_types/File Type - Voice Note (Audio Recording).png";
                                        if (ext === "bar")                 return "asset:///images/ChatView/bubble/file_types/File Type - BAR.png";
                                        return "asset:///images/ChatView/bubble/file_types/File Type - Video.png";
                                    }
                                    // Status text as a function with explicit parameters (block-body bindings are invalid, and
                                    // explicit parameters keep the dependency visible to the binding engine).
                                    function statusText(mine, msgId, uploadVideoActive, uploadVideoPercent,
                                                         uploadFileActive, uploadFilePercent,
                                                         downloading, progress, isVideo) {
                                        var mid = msgId || "";
                                        if (mine && mid.indexOf("local_video_") === 0) {
                                            return uploadVideoActive
                                                ? "Sending " + uploadVideoPercent + "%..."
                                                : "Sending...";
                                        }
                                        if (mine && mid.indexOf("local_file_") === 0) {
                                            return uploadFileActive
                                                ? "Sending " + uploadFilePercent + "%..."
                                                : "Sending...";
                                        }
                                        if (downloading) return "Downloading " + progress + "%...";
                                        return isVideo ? "Tap to play" : "Tap to open";
                                    }
                                    property string vExt: videoBubble.extFor(videoBubble.vFileName)
                                    property bool vIsVideo: videoBubble.vExt === "mp4" || videoBubble.vExt === "mov"
                                                          || videoBubble.vExt === "3gp" || videoBubble.vExt === "mkv"
                                    property string vIconSource: videoBubble.iconFor(videoBubble.vExt)
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
                                                rowRoot.ListItem.view.playVideoMsg(ListItemData.msgId, videoBubble.vHref, videoBubble.vFileName);
                                            }
                                        }
                                    ]
                                }

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

                                    function iconForCall(missed, mine, video) {
                                        var kind = video ? "video" : "voice";
                                        var dir  = missed ? "missed" : (mine ? "outgoing" : "incoming");
                                        return "asset:///images/ChatView/bubble/video_voice/ca_" + kind + "_chat_" + dir + ".png";
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

                                    // Informational only: no in-app calling, so tapping doesn't re-dial.
                                }

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
                                    // Label has no maxLines in Cascades QML 1.0 (it crashed the parser): cap the length manually so
                                    // long descriptions don't blow up the bubble height.
                                    function truncateDesc(s) {
                                        if (s.length <= 140) return s;
                                        return s.substring(0, 140) + "...";
                                    }
                                    property string lTitle: linkBubble.extractJsonStringField(ListItemData.content, "linkTitle")
                                    property string lDesc:  linkBubble.truncateDesc(linkBubble.extractJsonStringField(ListItemData.content, "linkDesc"))
                                    property string lHref:  linkBubble.extractJsonStringField(ListItemData.content, "linkHref")
                                    property string lThumb: linkBubble.extractJsonStringField(ListItemData.content, "linkThumb")

                                    // Cascades QML 1.0 ImageView can't load remote https:// URLs ("Unsupported scheme (https)").
                                    // ListItemData.localImage is filled asynchronously after downloadImageMessage() ->
                                    // imageMsgReady -> applyImageUpdate(); falling back to lThumb is a last resort that won't load.
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

                                Container {
                                    id: stickerBubble
                                    visible: !rowRoot.recalled
                                             && (ListItemData.msgType === 5 || ListItemData.msgType === "5")
                                    horizontalAlignment: HorizontalAlignment.Left
                                    topMargin: 2; bottomMargin: 2
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
                                    // stickerLocalPath is patched by applyStickerUpdate() once the async download finishes; this
                                    // bubble only reads it. The download starts in C++ (WebSocket/Messages) because QML init
                                    // signals (Component.onCompleted, onVisibleChanged) never fired reliably for this nested delegate.
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
                            }
                        }

                            // Accent strip on the bubble's bottom edge (gray = mine, blue = theirs), only on the last bubble
                            // of a group or a standalone one. Height 0 instead of visible: false, which doesn't reliably
                            // release the Container's layout space.
                            Container {
                                horizontalAlignment: HorizontalAlignment.Fill
                                property bool showStrip: rowRoot.bubblePos === "bottom" || rowRoot.bubblePos === "full"
                                preferredHeight: showStrip ? ui.du(0.8) : 0
                                minHeight: showStrip ? ui.du(0.8) : 0
                                maxHeight: showStrip ? ui.du(0.8) : 0
                                visible: showStrip
                                background: rowRoot.mine ? Color.create("#999999") : Color.create("#0073BC")
                            }
                        }

                        Container {
                            preferredWidth: rowRoot.mine ? 60 : 18
                            minWidth:       rowRoot.mine ? 60 : 18
                            maxWidth:       rowRoot.mine ? 60 : 18
                        }
                        }

                        // Reaction pills (kind === "message" only): one chip per distinct icon, "+N" for the count
                        // (msgList.summarizePills()). 6 fixed slots, no Repeater in this QtQuick version.
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

                                Container {
                                    visible: !!(ListItemData.reactions && ListItemData.reactions.length > 0)
                                    rightMargin: 4
                                    background: (ListItemData.reactions && ListItemData.reactions[0] && ListItemData.reactions[0].mine)
                                        ? Color.create("#b8c4cc") : (rowRoot.isDark ? Color.create("#3a3a3a") : Color.create("#e2e2e2"))
                                    topPadding: 6; bottomPadding: 6; leftPadding: 12; rightPadding: 12
                                    layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                                    gestureHandlers: [ TapHandler { onTapped: {
                                        if (ListItemData.reactions && ListItemData.reactions[0])
                                            rowRoot.ListItem.view.doSendReaction(ListItemData.msgId, ListItemData.cliMsgId, ListItemData.msgType, ListItemData.reactions[0].icon);
                                    } } ]
                                    ImageView { imageSource: (ListItemData.reactions && ListItemData.reactions[0]) ? ListItemData.reactions[0].asset : ""; preferredWidth: 32; preferredHeight: 32; verticalAlignment: VerticalAlignment.Center; scalingMethod: ScalingMethod.AspectFit }
                                    Label { text: (ListItemData.reactions && ListItemData.reactions[0]) ? ("+" + ListItemData.reactions[0].count) : ""; textStyle { fontSize: FontSize.Small; color: rowRoot.isDark ? Color.create("#FFFFFF") : Color.create("#444444") } leftMargin: 4; verticalAlignment: VerticalAlignment.Center }
                                }
                                Container {
                                    visible: !!(ListItemData.reactions && ListItemData.reactions.length > 1)
                                    rightMargin: 4
                                    background: (ListItemData.reactions && ListItemData.reactions[1] && ListItemData.reactions[1].mine)
                                        ? Color.create("#b8c4cc") : (rowRoot.isDark ? Color.create("#3a3a3a") : Color.create("#e2e2e2"))
                                    topPadding: 6; bottomPadding: 6; leftPadding: 12; rightPadding: 12
                                    layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                                    gestureHandlers: [ TapHandler { onTapped: {
                                        if (ListItemData.reactions && ListItemData.reactions[1])
                                            rowRoot.ListItem.view.doSendReaction(ListItemData.msgId, ListItemData.cliMsgId, ListItemData.msgType, ListItemData.reactions[1].icon);
                                    } } ]
                                    ImageView { imageSource: (ListItemData.reactions && ListItemData.reactions[1]) ? ListItemData.reactions[1].asset : ""; preferredWidth: 32; preferredHeight: 32; verticalAlignment: VerticalAlignment.Center; scalingMethod: ScalingMethod.AspectFit }
                                    Label { text: (ListItemData.reactions && ListItemData.reactions[1]) ? ("+" + ListItemData.reactions[1].count) : ""; textStyle { fontSize: FontSize.Small; color: rowRoot.isDark ? Color.create("#FFFFFF") : Color.create("#444444") } leftMargin: 4; verticalAlignment: VerticalAlignment.Center }
                                }
                                Container {
                                    visible: !!(ListItemData.reactions && ListItemData.reactions.length > 2)
                                    rightMargin: 4
                                    background: (ListItemData.reactions && ListItemData.reactions[2] && ListItemData.reactions[2].mine)
                                        ? Color.create("#b8c4cc") : (rowRoot.isDark ? Color.create("#3a3a3a") : Color.create("#e2e2e2"))
                                    topPadding: 6; bottomPadding: 6; leftPadding: 12; rightPadding: 12
                                    layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                                    gestureHandlers: [ TapHandler { onTapped: {
                                        if (ListItemData.reactions && ListItemData.reactions[2])
                                            rowRoot.ListItem.view.doSendReaction(ListItemData.msgId, ListItemData.cliMsgId, ListItemData.msgType, ListItemData.reactions[2].icon);
                                    } } ]
                                    ImageView { imageSource: (ListItemData.reactions && ListItemData.reactions[2]) ? ListItemData.reactions[2].asset : ""; preferredWidth: 32; preferredHeight: 32; verticalAlignment: VerticalAlignment.Center; scalingMethod: ScalingMethod.AspectFit }
                                    Label { text: (ListItemData.reactions && ListItemData.reactions[2]) ? ("+" + ListItemData.reactions[2].count) : ""; textStyle { fontSize: FontSize.Small; color: rowRoot.isDark ? Color.create("#FFFFFF") : Color.create("#444444") } leftMargin: 4; verticalAlignment: VerticalAlignment.Center }
                                }
                                Container {
                                    visible: !!(ListItemData.reactions && ListItemData.reactions.length > 3)
                                    rightMargin: 4
                                    background: (ListItemData.reactions && ListItemData.reactions[3] && ListItemData.reactions[3].mine)
                                        ? Color.create("#b8c4cc") : (rowRoot.isDark ? Color.create("#3a3a3a") : Color.create("#e2e2e2"))
                                    topPadding: 6; bottomPadding: 6; leftPadding: 12; rightPadding: 12
                                    layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                                    gestureHandlers: [ TapHandler { onTapped: {
                                        if (ListItemData.reactions && ListItemData.reactions[3])
                                            rowRoot.ListItem.view.doSendReaction(ListItemData.msgId, ListItemData.cliMsgId, ListItemData.msgType, ListItemData.reactions[3].icon);
                                    } } ]
                                    ImageView { imageSource: (ListItemData.reactions && ListItemData.reactions[3]) ? ListItemData.reactions[3].asset : ""; preferredWidth: 32; preferredHeight: 32; verticalAlignment: VerticalAlignment.Center; scalingMethod: ScalingMethod.AspectFit }
                                    Label { text: (ListItemData.reactions && ListItemData.reactions[3]) ? ("+" + ListItemData.reactions[3].count) : ""; textStyle { fontSize: FontSize.Small; color: rowRoot.isDark ? Color.create("#FFFFFF") : Color.create("#444444") } leftMargin: 4; verticalAlignment: VerticalAlignment.Center }
                                }
                                Container {
                                    visible: !!(ListItemData.reactions && ListItemData.reactions.length > 4)
                                    rightMargin: 4
                                    background: (ListItemData.reactions && ListItemData.reactions[4] && ListItemData.reactions[4].mine)
                                        ? Color.create("#b8c4cc") : (rowRoot.isDark ? Color.create("#3a3a3a") : Color.create("#e2e2e2"))
                                    topPadding: 6; bottomPadding: 6; leftPadding: 12; rightPadding: 12
                                    layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                                    gestureHandlers: [ TapHandler { onTapped: {
                                        if (ListItemData.reactions && ListItemData.reactions[4])
                                            rowRoot.ListItem.view.doSendReaction(ListItemData.msgId, ListItemData.cliMsgId, ListItemData.msgType, ListItemData.reactions[4].icon);
                                    } } ]
                                    ImageView { imageSource: (ListItemData.reactions && ListItemData.reactions[4]) ? ListItemData.reactions[4].asset : ""; preferredWidth: 32; preferredHeight: 32; verticalAlignment: VerticalAlignment.Center; scalingMethod: ScalingMethod.AspectFit }
                                    Label { text: (ListItemData.reactions && ListItemData.reactions[4]) ? ("+" + ListItemData.reactions[4].count) : ""; textStyle { fontSize: FontSize.Small; color: rowRoot.isDark ? Color.create("#FFFFFF") : Color.create("#444444") } leftMargin: 4; verticalAlignment: VerticalAlignment.Center }
                                }
                                Container {
                                    visible: !!(ListItemData.reactions && ListItemData.reactions.length > 5)
                                    background: (ListItemData.reactions && ListItemData.reactions[5] && ListItemData.reactions[5].mine)
                                        ? Color.create("#b8c4cc") : (rowRoot.isDark ? Color.create("#3a3a3a") : Color.create("#e2e2e2"))
                                    topPadding: 6; bottomPadding: 6; leftPadding: 12; rightPadding: 12
                                    layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                                    gestureHandlers: [ TapHandler { onTapped: {
                                        if (ListItemData.reactions && ListItemData.reactions[5])
                                            rowRoot.ListItem.view.doSendReaction(ListItemData.msgId, ListItemData.cliMsgId, ListItemData.msgType, ListItemData.reactions[5].icon);
                                    } } ]
                                    ImageView { imageSource: (ListItemData.reactions && ListItemData.reactions[5]) ? ListItemData.reactions[5].asset : ""; preferredWidth: 32; preferredHeight: 32; verticalAlignment: VerticalAlignment.Center; scalingMethod: ScalingMethod.AspectFit }
                                    Label { text: (ListItemData.reactions && ListItemData.reactions[5]) ? ("+" + ListItemData.reactions[5].count) : ""; textStyle { fontSize: FontSize.Small; color: rowRoot.isDark ? Color.create("#FFFFFF") : Color.create("#444444") } leftMargin: 4; verticalAlignment: VerticalAlignment.Center }
                                }
                            }

                            Container { preferredWidth: rowRoot.mine ? 60 : 18; minWidth: rowRoot.mine ? 60 : 18; maxWidth: rowRoot.mine ? 60 : 18 }
                        }

                        Container {
                            id: pollCardRoot
                            visible: rowRoot.kind === "poll"
                            horizontalAlignment: HorizontalAlignment.Fill
                            topPadding: 12; bottomPadding: 14; leftPadding: 20; rightPadding: 20

                            Container {
                                horizontalAlignment: HorizontalAlignment.Fill
                                layout: StackLayout { orientation: LayoutOrientation.TopToBottom }
                                background: rowRoot.isDark ? Color.create("#26323d") : Color.White
                                topPadding: ui.du(0.8); bottomPadding: ui.du(1.4)
                                leftPadding: ui.du(1.0); rightPadding: ui.du(1.0)

                                Container {
                                    horizontalAlignment: HorizontalAlignment.Fill
                                    layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                                    bottomMargin: ui.du(0.4)
                                    Container {
                                        layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                                        layout: DockLayout {}
                                        minHeight: ui.du(5.6)
                                        maxHeight: (ListItemData.pollQuestion || "").length <= 16 ? ui.du(5.6) : 100000
                                        Label {
                                            horizontalAlignment: HorizontalAlignment.Left
                                            verticalAlignment: VerticalAlignment.Center
                                            text: ListItemData.pollQuestion || ""
                                            multiline: true
                                            textStyle { base: SystemDefaults.TextStyles.BigText
                                                        color: rowRoot.isDark ? Color.White : Color.Black }
                                        }
                                    }
                                    Label {
                                        visible: (ListItemData.pollNumVote || 0) > 0
                                        verticalAlignment: VerticalAlignment.Center
                                        text: String(ListItemData.pollNumVote || 0) + " voted"
                                        textStyle { color: Color.create("#2575fc"); fontSize: FontSize.Small }
                                        gestureHandlers: [ TapHandler { onTapped: {
                                            rowRoot.ListItem.view.viewPollVotersProxy(ListItemData.pollId);
                                        } } ]
                                    }
                                }

                                Container {
                                    visible: !!(ListItemData.pollOptions && ListItemData.pollOptions.length > 0)
                                    horizontalAlignment: HorizontalAlignment.Fill
                                    layout: DockLayout {}
                                    background: rowRoot.isDark ? Color.create("#33404a") : Color.create("#ececec")
                                    bottomMargin: ui.du(0.7)
                                    minHeight: ui.du(5.6)
                                    maxHeight: ((ListItemData.pollOptions && ListItemData.pollOptions[0]) ? (ListItemData.pollOptions[0].content || "") : "").length <= 26 ? ui.du(5.6) : 100000
                                    Container {
                                        visible: rowRoot.pollFillPct(ListItemData.pollOptions, ListItemData.pollNumVote, 0) > 0
                                        horizontalAlignment: HorizontalAlignment.Left
                                        verticalAlignment: VerticalAlignment.Fill
                                        preferredWidth: rowRoot.pollInnerW * rowRoot.pollFillPct(ListItemData.pollOptions, ListItemData.pollNumVote, 0) / 100
                                        background: rowRoot.isDark ? Color.create("#2c5f94") : Color.create("#a8cdf8")
                                    }
                                    Container {
                                        horizontalAlignment: HorizontalAlignment.Fill
                                        verticalAlignment: VerticalAlignment.Center
                                        layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                                        leftPadding: ui.du(1.2); rightPadding: ui.du(1.2)
                                        Label {
                                            layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                                            verticalAlignment: VerticalAlignment.Center
                                            text: ((ListItemData.pollOptions && ListItemData.pollOptions[0]) ? (ListItemData.pollOptions[0].content || "") : "")
                                            multiline: true
                                            textStyle { fontSize: FontSize.Large; color: rowRoot.isDark ? Color.White : Color.Black }
                                        }
                                        Label {
                                            verticalAlignment: VerticalAlignment.Center
                                            text: (ListItemData.pollOptions && ListItemData.pollOptions[0]) ? String(ListItemData.pollOptions[0].votes || 0) : ""
                                            textStyle { fontSize: FontSize.Large; color: Color.Gray }
                                        }
                                    }
                                }
                                Container {
                                    visible: !!(ListItemData.pollOptions && ListItemData.pollOptions.length > 1)
                                    horizontalAlignment: HorizontalAlignment.Fill
                                    layout: DockLayout {}
                                    background: rowRoot.isDark ? Color.create("#33404a") : Color.create("#ececec")
                                    bottomMargin: ui.du(0.7)
                                    minHeight: ui.du(5.6)
                                    maxHeight: ((ListItemData.pollOptions && ListItemData.pollOptions[1]) ? (ListItemData.pollOptions[1].content || "") : "").length <= 26 ? ui.du(5.6) : 100000
                                    Container {
                                        visible: rowRoot.pollFillPct(ListItemData.pollOptions, ListItemData.pollNumVote, 1) > 0
                                        horizontalAlignment: HorizontalAlignment.Left
                                        verticalAlignment: VerticalAlignment.Fill
                                        preferredWidth: rowRoot.pollInnerW * rowRoot.pollFillPct(ListItemData.pollOptions, ListItemData.pollNumVote, 1) / 100
                                        background: rowRoot.isDark ? Color.create("#2c5f94") : Color.create("#a8cdf8")
                                    }
                                    Container {
                                        horizontalAlignment: HorizontalAlignment.Fill
                                        verticalAlignment: VerticalAlignment.Center
                                        layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                                        leftPadding: ui.du(1.2); rightPadding: ui.du(1.2)
                                        Label {
                                            layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                                            verticalAlignment: VerticalAlignment.Center
                                            text: ((ListItemData.pollOptions && ListItemData.pollOptions[1]) ? (ListItemData.pollOptions[1].content || "") : "")
                                            multiline: true
                                            textStyle { fontSize: FontSize.Large; color: rowRoot.isDark ? Color.White : Color.Black }
                                        }
                                        Label {
                                            verticalAlignment: VerticalAlignment.Center
                                            text: (ListItemData.pollOptions && ListItemData.pollOptions[1]) ? String(ListItemData.pollOptions[1].votes || 0) : ""
                                            textStyle { fontSize: FontSize.Large; color: Color.Gray }
                                        }
                                    }
                                }
                                Container {
                                    visible: !!(ListItemData.pollOptions && ListItemData.pollOptions.length > 2)
                                    horizontalAlignment: HorizontalAlignment.Fill
                                    layout: DockLayout {}
                                    background: rowRoot.isDark ? Color.create("#33404a") : Color.create("#ececec")
                                    bottomMargin: ui.du(0.7)
                                    minHeight: ui.du(5.6)
                                    maxHeight: ((ListItemData.pollOptions && ListItemData.pollOptions[2]) ? (ListItemData.pollOptions[2].content || "") : "").length <= 26 ? ui.du(5.6) : 100000
                                    Container {
                                        visible: rowRoot.pollFillPct(ListItemData.pollOptions, ListItemData.pollNumVote, 2) > 0
                                        horizontalAlignment: HorizontalAlignment.Left
                                        verticalAlignment: VerticalAlignment.Fill
                                        preferredWidth: rowRoot.pollInnerW * rowRoot.pollFillPct(ListItemData.pollOptions, ListItemData.pollNumVote, 2) / 100
                                        background: rowRoot.isDark ? Color.create("#2c5f94") : Color.create("#a8cdf8")
                                    }
                                    Container {
                                        horizontalAlignment: HorizontalAlignment.Fill
                                        verticalAlignment: VerticalAlignment.Center
                                        layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                                        leftPadding: ui.du(1.2); rightPadding: ui.du(1.2)
                                        Label {
                                            layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                                            verticalAlignment: VerticalAlignment.Center
                                            text: ((ListItemData.pollOptions && ListItemData.pollOptions[2]) ? (ListItemData.pollOptions[2].content || "") : "")
                                            multiline: true
                                            textStyle { fontSize: FontSize.Large; color: rowRoot.isDark ? Color.White : Color.Black }
                                        }
                                        Label {
                                            verticalAlignment: VerticalAlignment.Center
                                            text: (ListItemData.pollOptions && ListItemData.pollOptions[2]) ? String(ListItemData.pollOptions[2].votes || 0) : ""
                                            textStyle { fontSize: FontSize.Large; color: Color.Gray }
                                        }
                                    }
                                }
                                Container {
                                    visible: !!(ListItemData.pollOptions && ListItemData.pollOptions.length > 3)
                                    horizontalAlignment: HorizontalAlignment.Fill
                                    layout: DockLayout {}
                                    background: rowRoot.isDark ? Color.create("#33404a") : Color.create("#ececec")
                                    bottomMargin: ui.du(0.7)
                                    minHeight: ui.du(5.6)
                                    maxHeight: ((ListItemData.pollOptions && ListItemData.pollOptions[3]) ? (ListItemData.pollOptions[3].content || "") : "").length <= 26 ? ui.du(5.6) : 100000
                                    Container {
                                        visible: rowRoot.pollFillPct(ListItemData.pollOptions, ListItemData.pollNumVote, 3) > 0
                                        horizontalAlignment: HorizontalAlignment.Left
                                        verticalAlignment: VerticalAlignment.Fill
                                        preferredWidth: rowRoot.pollInnerW * rowRoot.pollFillPct(ListItemData.pollOptions, ListItemData.pollNumVote, 3) / 100
                                        background: rowRoot.isDark ? Color.create("#2c5f94") : Color.create("#a8cdf8")
                                    }
                                    Container {
                                        horizontalAlignment: HorizontalAlignment.Fill
                                        verticalAlignment: VerticalAlignment.Center
                                        layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                                        leftPadding: ui.du(1.2); rightPadding: ui.du(1.2)
                                        Label {
                                            layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                                            verticalAlignment: VerticalAlignment.Center
                                            text: ((ListItemData.pollOptions && ListItemData.pollOptions[3]) ? (ListItemData.pollOptions[3].content || "") : "")
                                            multiline: true
                                            textStyle { fontSize: FontSize.Large; color: rowRoot.isDark ? Color.White : Color.Black }
                                        }
                                        Label {
                                            verticalAlignment: VerticalAlignment.Center
                                            text: (ListItemData.pollOptions && ListItemData.pollOptions[3]) ? String(ListItemData.pollOptions[3].votes || 0) : ""
                                            textStyle { fontSize: FontSize.Large; color: Color.Gray }
                                        }
                                    }
                                }
                                Container {
                                    visible: !!(ListItemData.pollOptions && ListItemData.pollOptions.length > 4)
                                    horizontalAlignment: HorizontalAlignment.Fill
                                    layout: DockLayout {}
                                    background: rowRoot.isDark ? Color.create("#33404a") : Color.create("#ececec")
                                    bottomMargin: ui.du(0.7)
                                    minHeight: ui.du(5.6)
                                    maxHeight: ((ListItemData.pollOptions && ListItemData.pollOptions[4]) ? (ListItemData.pollOptions[4].content || "") : "").length <= 26 ? ui.du(5.6) : 100000
                                    Container {
                                        visible: rowRoot.pollFillPct(ListItemData.pollOptions, ListItemData.pollNumVote, 4) > 0
                                        horizontalAlignment: HorizontalAlignment.Left
                                        verticalAlignment: VerticalAlignment.Fill
                                        preferredWidth: rowRoot.pollInnerW * rowRoot.pollFillPct(ListItemData.pollOptions, ListItemData.pollNumVote, 4) / 100
                                        background: rowRoot.isDark ? Color.create("#2c5f94") : Color.create("#a8cdf8")
                                    }
                                    Container {
                                        horizontalAlignment: HorizontalAlignment.Fill
                                        verticalAlignment: VerticalAlignment.Center
                                        layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                                        leftPadding: ui.du(1.2); rightPadding: ui.du(1.2)
                                        Label {
                                            layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                                            verticalAlignment: VerticalAlignment.Center
                                            text: ((ListItemData.pollOptions && ListItemData.pollOptions[4]) ? (ListItemData.pollOptions[4].content || "") : "")
                                            multiline: true
                                            textStyle { fontSize: FontSize.Large; color: rowRoot.isDark ? Color.White : Color.Black }
                                        }
                                        Label {
                                            verticalAlignment: VerticalAlignment.Center
                                            text: (ListItemData.pollOptions && ListItemData.pollOptions[4]) ? String(ListItemData.pollOptions[4].votes || 0) : ""
                                            textStyle { fontSize: FontSize.Large; color: Color.Gray }
                                        }
                                    }
                                }
                                Container {
                                    visible: !!(ListItemData.pollOptions && ListItemData.pollOptions.length > 5)
                                    horizontalAlignment: HorizontalAlignment.Fill
                                    layout: DockLayout {}
                                    background: rowRoot.isDark ? Color.create("#33404a") : Color.create("#ececec")
                                    bottomMargin: ui.du(0.7)
                                    minHeight: ui.du(5.6)
                                    maxHeight: ((ListItemData.pollOptions && ListItemData.pollOptions[5]) ? (ListItemData.pollOptions[5].content || "") : "").length <= 26 ? ui.du(5.6) : 100000
                                    Container {
                                        visible: rowRoot.pollFillPct(ListItemData.pollOptions, ListItemData.pollNumVote, 5) > 0
                                        horizontalAlignment: HorizontalAlignment.Left
                                        verticalAlignment: VerticalAlignment.Fill
                                        preferredWidth: rowRoot.pollInnerW * rowRoot.pollFillPct(ListItemData.pollOptions, ListItemData.pollNumVote, 5) / 100
                                        background: rowRoot.isDark ? Color.create("#2c5f94") : Color.create("#a8cdf8")
                                    }
                                    Container {
                                        horizontalAlignment: HorizontalAlignment.Fill
                                        verticalAlignment: VerticalAlignment.Center
                                        layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                                        leftPadding: ui.du(1.2); rightPadding: ui.du(1.2)
                                        Label {
                                            layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                                            verticalAlignment: VerticalAlignment.Center
                                            text: ((ListItemData.pollOptions && ListItemData.pollOptions[5]) ? (ListItemData.pollOptions[5].content || "") : "")
                                            multiline: true
                                            textStyle { fontSize: FontSize.Large; color: rowRoot.isDark ? Color.White : Color.Black }
                                        }
                                        Label {
                                            verticalAlignment: VerticalAlignment.Center
                                            text: (ListItemData.pollOptions && ListItemData.pollOptions[5]) ? String(ListItemData.pollOptions[5].votes || 0) : ""
                                            textStyle { fontSize: FontSize.Large; color: Color.Gray }
                                        }
                                    }
                                }

                                Container {
                                    visible: !!(ListItemData.pollOptions && ListItemData.pollOptions.length > 0)
                                    horizontalAlignment: HorizontalAlignment.Fill
                                    topMargin: ui.du(0.6)
                                    preferredHeight: ui.du(5.8)
                                    layout: DockLayout {}
                                    background: rowRoot.isDark ? Color.create("#2a3f57") : Color.create("#dbe9fb")
                                    gestureHandlers: [ TapHandler { onTapped: {
                                        rowRoot.ListItem.view.openPollVoteProxy(ListItemData.pollId);
                                    } } ]
                                    Label {
                                        horizontalAlignment: HorizontalAlignment.Center
                                        verticalAlignment: VerticalAlignment.Center
                                        text: "Vote"
                                        textStyle { color: Color.create("#2575fc"); fontSize: FontSize.Large; fontWeight: FontWeight.Bold }
                                    }
                                }
                            }
                        }

                        }

                    }
                }
            ]
        }

        Container {
            id: emojiPanelSlot
            horizontalAlignment: HorizontalAlignment.Fill
            topMargin: ui.du(0.8)
            visible: false
        }

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
            id: replyPreviewBar
            visible: chatViewPage.pendingReplyMsgId.length > 0
            horizontalAlignment: HorizontalAlignment.Fill
            background: chatViewPage.isDark ? Color.create("#1e2a38") : Color.create("#dce8f5")
            topPadding:    ui.du(1.0)
            bottomPadding: ui.du(1.0)
            leftPadding:   ui.du(1.0)
            rightPadding:  ui.du(1.5)
            layout: StackLayout { orientation: LayoutOrientation.LeftToRight }

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
        // Group Board is pushed via groupBoardRequested (flip-a-bool, like qmRequested). Disabled, not
        // hidden, outside groups (same as Block/Leave group).
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
        // Best match: an exact name wins, otherwise the shortest prefix match.
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
            placeholder.quoteCliMsgId   = chatViewPage.pendingReplyCliMsgId;
            placeholder.quoteTs         = chatViewPage.pendingReplyTs;
        }
        msgModel.append(placeholder);
        chatViewPage.rebuildGroups(true);

        chatViewPage.pendingMsg = txt;

        if (isReply) {
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

    function extractJsonField(content, key) {
        if (typeof content !== "string" || content.length === 0) return "";
        var re = new RegExp("\"" + key + "\"\\s*:\\s*\"([^\"]*)\"");
        var m = content.match(re);
        return (m && m[1]) ? m[1] : "";
    }

    function extractPhotoCaption(content) {
        if (typeof content !== "string" || content.length === 0) return "";
        if (content.charAt(0) !== "{") return "";
        var m = content.match(/"caption"\s*:\s*"((?:[^"\\]|\\.)*)"/);
        if (!m || !m[1]) return "";
        return m[1].replace(/\\n/g, "\n").replace(/\\r/g, "\r")
                   .replace(/\\t/g, "\t").replace(/\\"/g, "\"").replace(/\\\\/g, "\\");
    }

    // Call before any direct msgModel.append()/clear() so a pending deferred flush from
    // rebuildGroups() lands first; otherwise new data can end up out of order or overwritten.
    function flushPendingRebuild() {
        if (rebuildFlushTimer.running || rebuildFlushTimer.pendingItems) {
            rebuildFlushTimer.stop();
            if (rebuildFlushTimer.pendingItems) {
                msgModel.append(rebuildFlushTimer.pendingItems);
                rebuildFlushTimer.pendingItems = null;
            }
            rebuildFlushTimer.pendingScroll = false;
            rebuildFlushTimer.pendingAnchor = -1;
        }
    }

    // Shared by filePicker/audioPicker/voiceNoteSheet/contact-vcf: builds a local echo bubble
    // (msgType=3, "local_file_" prefix) then calls sendFile(). path must be a real system path
    // WITHOUT "file://".
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

    function msgTsMs(ts) {
        var n = (ts || 0) * 1;
        if (n > 0 && n < 1e12) n *= 1000;
        return n;
    }

    function windowOldestRow() {
        var size = msgModel.size();
        for (var i = 0; i < size; i++) {
            var v = msgModel.value(i);
            var id = v.msgId || "";
            if (id.length === 0 || id.indexOf("local_") === 0 || id.indexOf("poll_") === 0) continue;
            return v;
        }
        return null;
    }

    function requestOlderMessages() {
        if (!chatViewPage.initialized || !chatViewPage.olderArmed) return;
        if (chatViewPage.loadingOlder || !chatViewPage.hasMoreOlder) return;
        if (msgModel.size() === 0) return;
        if (msgModel.size() >= chatViewPage.historyMaxRows) {
            chatViewPage.hasMoreOlder = false;
            return;
        }
        chatViewPage.loadingOlder = true;
        olderLoadTimer.restart();
    }

    function doLoadOlderMessages() {
        if (!chatViewPage.initialized) { chatViewPage.loadingOlder = false; return; }
        // Must run before reading/replacing msgModel: see flushPendingRebuild().
        chatViewPage.flushPendingRebuild();

        var oldest = chatViewPage.windowOldestRow();
        var room = chatViewPage.historyMaxRows - msgModel.size();
        var want = Math.min(chatViewPage.historyPageSize, room);
        if (!oldest || want <= 0) {
            chatViewPage.loadingOlder = false;
            chatViewPage.hasMoreOlder = false;
            return;
        }

        var page = zService.dbLoadMessagesPage(chatViewPage.threadId, String(oldest.ts), want + 1);
        var existing = chatViewPage.buildExistingIds();
        var cache = chatViewPage.dbIsMineCache;
        var fresh = [];
        for (var i = 0; i < page.length; i++) {
            var c = page[i];
            if (!c.msgId || existing[c.msgId]) continue;
            c.selfName = chatViewPage.selfName || "Me";
            c.isMine = (c.isMine === "true" || c.isMine === 1 || c.isMine === true);
            cache[c.msgId] = c.isMine;
            c.reactions = msgList.summarizePills(c.msgId);
            fresh.push(c);

            var isPhoto = (c.msgType === 2 || c.msgType === "2");
            var hasLocal = (c.localImage && c.localImage.length > 0);
            if (isPhoto && !hasLocal) {
                var photoUrl = chatViewPage.extractPhotoUrl(c.content || "");
                if (photoUrl.length > 0)
                    zService.downloadImageMessage(c.msgId, photoUrl, chatViewPage.threadId);
            }
            var isLink = (c.msgType === 6 || c.msgType === "6");
            if (isLink && !hasLocal) {
                var linkThumbUrl = chatViewPage.extractJsonField(c.content || "", "linkThumb");
                if (linkThumbUrl.length > 0)
                    zService.downloadImageMessage(c.msgId, linkThumbUrl, chatViewPage.threadId);
            }
        }
        chatViewPage.dbIsMineCache = cache;

        chatViewPage.hasMoreOlder = (page.length >= want + 1) && (fresh.length > 0)
                                    && (msgModel.size() + fresh.length < chatViewPage.historyMaxRows);
        if (fresh.length === 0) {
            chatViewPage.loadingOlder = false;
            return;
        }

        if (chatViewPage.searchVisible) {
            chatViewPage.searchMatches = [];
            chatViewPage.searchMatchPos = -1;
        }

        chatViewPage.rebuildGroups(false, fresh);
        if (!rebuildFlushTimer.running && !rebuildFlushTimer.pendingItems)
            chatViewPage.loadingOlder = false;
    }

    function rebuildGroups(scrollAfter, prependItems) {
        // Data-loss race: if rebuildGroups() deferred an update onto rebuildFlushTimer and another
        // message arrives before it fires, the second pendingItems would overwrite the first (a reply
        // sent right after an incoming photo made the photo vanish). So apply a pending flush
        // synchronously first.
        chatViewPage.flushPendingRebuild();

        var prependCount = (prependItems && prependItems.length) ? prependItems.length : 0;
        var size = msgModel.size() + prependCount;
        if (size === 0) return;

        var items = [];
        var prevGrouped = [];
        var prevBubblePos = [];
        for (var pp = 0; pp < prependCount; pp++) {
            items.push(prependItems[pp]);
            prevGrouped.push(undefined);
            prevBubblePos.push(undefined);
        }
        var curSize = size - prependCount;
        for (var i = 0; i < curSize; i++) {
            var v = msgModel.value(i);
            items.push(v);
            prevGrouped.push(v.grouped);
            prevBubblePos.push(v.bubblePos);
        }

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

            var curIsPhoto  = (cur.msgType  === 2 || cur.msgType  === "2");
            var prevIsPhoto = prev ? (prev.msgType === 2 || prev.msgType === "2") : false;
            var nextIsPhoto = next ? (next.msgType === 2 || next.msgType === "2") : false;

            var curId  = cur.msgId  || "";
            var prevId = prev ? (prev.msgId || "") : "";
            var nextId = next ? (next.msgId || "") : "";
            var curLocal  = curId.indexOf("local_")  === 0;
            var prevLocal = prevId.indexOf("local_") === 0;
            var nextLocal = nextId.indexOf("local_") === 0;

            var curTs  = toMs(cur.ts,               curLocal);
            var prevTs = toMs(prev ? prev.ts : null, prevLocal);
            var nextTs = toMs(next ? next.ts : null, nextLocal);
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
            cur.msgState  = curMine ? (chatViewPage.msgStates[curId] || 1) : 0;

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

        // replace() only swaps data and doesn't re-measure, so a row whose grouped/bubblePos changed
        // keeps a stale height. Per-row removeAt+insert races (overlapping animations leave rows at half
        // height). So when a re-layout is needed, do one clear()+append(); unchanged rows still use the
        // cheap in-place replace().
        var anyLayoutChanged = false;
        var undefPrev = 0;
        for (var i = 0; i < size; i++) {
            if (prevGrouped[i] === undefined && prevBubblePos[i] === undefined) {
                undefPrev++;
                if (items[i].grouped === true || items[i].bubblePos !== "full") {
                    anyLayoutChanged = true;
                    break;
                }
                continue;
            }
            if (prevGrouped[i] !== items[i].grouped || prevBubblePos[i] !== items[i].bubblePos) {
                anyLayoutChanged = true;
                break;
            }
        }
        if (undefPrev > 1) anyLayoutChanged = true;
        // Prepending changes the model size, so the in-place replace() path can't be used: always
        // clear() + deferred re-append.
        if (prependCount > 0) anyLayoutChanged = true;
        if (anyLayoutChanged) {
            // clear()+append() alone doesn't re-measure: the ListView reused pooled Controls with stale
            // heights, and a synchronous dataModel=null;dataModel=msgModel bounce is a no-op (same
            // reference). Deferring the re-append to the next event loop turn (zero-interval Timer) gives
            // Cascades a real empty-model layout pass that drops the pooled heights.
            msgModel.clear();
            rebuildFlushTimer.pendingItems = items;
            rebuildFlushTimer.pendingScroll = !!scrollAfter;
            rebuildFlushTimer.pendingAnchor = (prependCount > 0) ? prependCount : -1;
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

    function isTextPlaceholderId(id) {
        return !!id && id.indexOf("local_") === 0
            && id.indexOf("local_img_") !== 0
            && id.indexOf("local_video_") !== 0
            && id.indexOf("local_file_") !== 0;
    }

    // ONE bubble per outgoing message: when the server confirms a plain-text/reply message, turn
    // the "sending" placeholder into the confirmed message in place (new msgId/ts). Returns true if
    // a placeholder was adopted. Failed bubbles are never adopted (they wait for Resend).
    function adoptTextPlaceholder(msg) {
        if (!msg || msg.content === undefined) return false;
        var k, it;
        if (msg.msgId) {
            for (k = 0; k < msgModel.size(); k++) {
                var ex = msgModel.value(k);
                if (ex.msgId === msg.msgId) return false;
            }
        }
        var hasQuote = !!(msg.quoteMsgId && String(msg.quoteMsgId).length > 0);
        var byQuote = -1;
        var target = -1;
        for (k = msgModel.size() - 1; k >= 0; k--) {
            it = msgModel.value(k);
            if (!chatViewPage.isTextPlaceholderId(it.msgId) || it.sendFailed === true) continue;
            if (it.content === msg.content) { target = k; break; }
            if (byQuote < 0 && hasQuote && it.quoteMsgId === msg.quoteMsgId) byQuote = k;
        }
        if (target < 0) target = byQuote;
        if (target < 0) return false;
        it = msgModel.value(target);
        if (!msg.quoteMsgId && it.quoteMsgId) {
            msg.quoteMsgId      = it.quoteMsgId;
            msg.quoteContent    = it.quoteContent;
            msg.quoteSenderName = it.quoteSenderName;
            msg.quoteMsgType    = it.quoteMsgType;
            msg.quoteOwnerId    = it.quoteOwnerId;
        }
        msgModel.replace(target, msg);
        return true;
    }

    // messageSent(false): keep the unsent bubble flagged as failed (red X + Resend in its
    // hold-menu). Video/file uploads can't be replayed yet, so they are removed.
    function markSendFailed() {
        for (var k = msgModel.size() - 1; k >= 0; k--) {
            var it = msgModel.value(k);
            var id = it.msgId || "";
            if (id.indexOf("local_") !== 0 || it.sendFailed === true) continue;
            if (id.indexOf("local_video_") === 0 || id.indexOf("local_file_") === 0) {
                msgModel.removeAt(k);
                return;
            }
            it.sendFailed = true;
            msgModel.replace(k, it);
            return;
        }
    }

    function resendMessage(msgId) {
        var idx = -1;
        for (var i = 0; i < msgModel.size(); i++) {
            if (msgModel.value(i).msgId === msgId) { idx = i; break; }
        }
        if (idx < 0) return;
        var it = msgModel.value(idx);
        if (it.sendFailed !== true) return;
        if (!chatViewPage.threadId || chatViewPage.threadId === "") return;
        chatViewPage.flushPendingRebuild();

        it.sendFailed = false;
        it.ts = String(new Date().getTime());
        msgModel.removeAt(idx);
        msgModel.append(it);
        chatViewPage.rebuildGroups(true);

        sendAction.enabled = false;
        chatViewPage.pendingMsg = it.content || "";
        var isImg = (String(it.msgId).indexOf("local_img_") === 0);
        if (isImg) {
            var path = String(it.localImage || "").replace(/^file:\/\//, "");
            zService.sendPhoto(chatViewPage.threadId, path, chatViewPage.isGroup,
                               chatViewPage.extractPhotoCaption(it.content || ""));
        } else if (it.quoteMsgId && String(it.quoteMsgId).length > 0) {
            var qType = (it.quoteMsgType === 2 || it.quoteMsgType === "2") ? 32 : 1;
            zService.sendMessageQuote(chatViewPage.threadId, it.content, chatViewPage.isGroup,
                it.quoteMsgId, it.quoteCliMsgId || "", it.quoteOwnerId || "",
                it.quoteContent || "", qType, it.quoteTs || "", it.quoteSenderName || "");
        } else {
            zService.sendMessage(chatViewPage.threadId, it.content, chatViewPage.isGroup);
        }
    }

    function removeLocalPlaceholder(content) {
        for (var k = msgModel.size() - 1; k >= 0; k--) {
            var item = msgModel.value(k);
            // Never touch a failed bubble: it is waiting for Resend.
            if (item.msgId && item.msgId.indexOf("local_") === 0 && item.content === content
                && item.sendFailed !== true) {
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

        Connections {
            id: emojiSignalCon
            target: null
            onEmojiPicked: {
                inputField.text = inputField.text + charStr
            }
        },

        SystemToast {
            id: copyToast
            body: "Copied to clipboard"
            position: SystemUiPosition.MiddleCenter
        },

        Timer {
            id: jumpHighlightTimer
            interval: 2000
            repeat: false
            onTriggered: { chatViewPage.jumpHighlightMsgId = ""; }
        },

        SystemToast {
            id: errorToast
            position: SystemUiPosition.MiddleCenter
        },

        SystemToast {
            id: downloadToast
            body: "Saved to Downloads"
            position: SystemUiPosition.MiddleCenter
        },

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

        // Feeds PinboardBar and upserts every poll into msgModel as an inline card. Filtered by groupId
        // so a stale fetch for a thread we've left can't clobber this one.
        Connections {
            target: zService
            onGroupBoardReady: {
                if (groupId !== chatViewPage.threadId) return;
                chatViewPage.boardItems = chatViewPage.filterBoardItems(items);
                chatViewPage.syncPollRows(items);
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
                    chatViewPage.bumpPollToBottom(pollId, updatedOptions, null);
                }
            }
        },

        Connections {
            target: zService
            onBoardEventOccurred: {
                if (groupId !== chatViewPage.threadId) return;
                // isSelf events are NOT skipped: unpinning from the official app on another device of the same
                // account arrives as a "self" event, and skipping it left the bar stale. A duplicate refresh
                // after our own action is harmless.
                var removedAct = (act === "unpin_topic" || act === "remove_topic" || act === "remove_board");
                if (topicId.length > 0) {
                    var rem = chatViewPage.removedBoardIds || {};
                    if (removedAct) rem[topicId] = true; else delete rem[topicId];
                    chatViewPage.removedBoardIds = rem;
                    if (removedAct) {
                        chatViewPage.boardItems = chatViewPage.filterBoardItems(chatViewPage.boardItems);
                        if (chatViewPage.removePollRow(topicId)) chatViewPage.rebuildGroups();
                    }
                }
                chatViewPage.loadBoardItems();
                if (!isSelf && topicType === 3 && topicId.length > 0 && act !== "remove_board" && act !== "remove_topic") {
                    var pb = chatViewPage.pollBumpPending || {};
                    pb[topicId] = true;
                    chatViewPage.pollBumpPending = pb;
                    zService.getPollDetail(topicId);
                }
            }
        },
        Connections {
            target: zService
            onPollDetailReady: {
                if (error && error.length > 0) return;
                // Only react to lookups WE started for a board event: PollVotersSheet fires this signal too,
                // and viewing voters must not move the card.
                var pending = chatViewPage.pollBumpPending || {};
                if (!pending[pollId]) return;
                delete pending[pollId];
                chatViewPage.pollBumpPending = pending;
                if ((chatViewPage.removedBoardIds || {})[pollId]) return;
                if (detail.closed === true) {
                    if (chatViewPage.removePollRow(pollId)) chatViewPage.rebuildGroups();
                    return;
                }
                // Never blank a card because a lookup came back without options.
                var detailOpts = detail.options || [];
                if (detailOpts.length === 0) return;
                chatViewPage.bumpPollToBottom(pollId, detailOpts, detail);
            }
        },
        // Realtime reaction from the server (another member, or the WS echo of ours: a harmless
        // re-apply). icon === "" means removed. Filtered by threadId so a stale push can't touch this
        // screen.
        Connections {
            target: zService
            onReactionUpdated: {
                if (threadId !== chatViewPage.threadId) return;
                msgList.applyReactionRecord(msgId, uid, icon);
            }
        },

        Connections {
            target: zService
            onVideoUploadProgress: {
                if (threadId !== chatViewPage.threadId) return;
                msgList.uploadVideoActive = (percent < 100);
                msgList.uploadVideoPercent = percent;
            }
        },
        // Own signal so a file upload's % doesn't drive the video bubble's UI.
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
        PollVoteSheet { id: pollVoteSheet },
        ForwardPickerSheet { id: forwardPickerSheet; isDark: chatViewPage.isDark },
        ReactionPickerSheet {
            id: reactionPickerSheet
            onReacted: { msgList.doSendReaction(msgId, cliMsgId, msgType, icon); }
        },

        SharePickerSheet { id: sharePicker },

        Connections {
            target: app
            onShareTargetsReady: {
                shareDimFadeOut.play();
                sharePicker.openWithTargets(targets);
            }
        },

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
            onDarkThemeChanged: {
                chatViewPage.isDark = dark;
            }
        },

        Connections {
            target: zService

            onMessagesReady: {
                if (threadId !== chatViewPage.threadId) return;
                // Must run before any direct msgModel read/append: see flushPendingRebuild().
                chatViewPage.flushPendingRebuild();

                var existing = chatViewPage.buildExistingIds();
                var added = false;

                // Windowed history: a cloud fetch can return far more than the window holds. Anything older
                // than the oldest loaded row belongs to an older page (already in SQLite) and must not be
                // appended after newer rows.
                var winOldest = chatViewPage.windowOldestRow();
                var oldestMs = winOldest ? chatViewPage.msgTsMs(winOldest.ts) : 0;
                var candidates = [];
                for (var j0 = 0; j0 < messages.length; j0++) {
                    var m0 = messages[j0];
                    if (existing[m0.msgId]) continue;
                    if (oldestMs > 0 && chatViewPage.msgTsMs(m0.ts) < oldestMs) continue;
                    candidates.push(m0);
                }
                if (oldestMs === 0 && candidates.length > chatViewPage.historyPageSize) {
                    candidates.sort(function(a, b) { return chatViewPage.msgTsMs(a.ts) - chatViewPage.msgTsMs(b.ts); });
                    candidates = candidates.slice(candidates.length - chatViewPage.historyPageSize);
                    chatViewPage.hasMoreOlder = true;
                }

                for (var j = 0; j < candidates.length; j++) {
                    var msg = candidates[j];
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
                    chatViewPage.markSendFailed();
                    chatViewPage.rebuildGroups();
                } else {
                    chatViewPage.removeLocalPlaceholder(chatViewPage.pendingMsg);
                }
                chatViewPage.pendingMsg = "";
                sendAction.enabled = true;
            }

            onNewMessage: {
                if (threadId !== chatViewPage.threadId) return;
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
                    chatViewPage.updateClockOffset(msg.cliMsgId, msg.ts);

                    if (msg.msgType === 2 || msg.msgType === "2") {
                        if (msg.msgId) {
                            for (var di0 = 0; di0 < msgModel.size(); di0++) {
                                var dv0 = msgModel.value(di0);
                                if (dv0.msgId === msg.msgId && dv0.msgId.indexOf("local_") !== 0) {
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
                        // The server confirmation never carries fileSize, only the placeholder did: re-inject it or the
                        // size disappears from the bubble.
                        if (savedFileSize > 0 && (msg.content || "").indexOf('"fileSize"') < 0) {
                            if (msg.content && msg.content.charAt(0) === "{")
                                msg.content = msg.content.slice(0, -1) + ',"fileSize":' + savedFileSize + '}';
                            else
                                msg.content = '{"fileSize":' + savedFileSize + '}';
                        }

                        if (placeholderIdx >= 0) {
                            msgModel.replace(placeholderIdx, msg);
                            handledInPlace = true;
                        } else if (savedLocalImage.length === 0 && msg.localImage && msg.localImage.length > 0) {
                            for (var li = msgModel.size() - 1; li >= 0; li--) {
                                if (msgModel.value(li).localImage === msg.localImage) {
                                    msgModel.replace(li, msg);
                                    handledInPlace = true;
                                    break;
                                }
                            }
                        }

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
                        // Video counterpart of the photo dedup block: without it the confirmed video didn't match the
                        // placeholder (empty href/fileSize:0), was appended as a second row, and the "local_video_..."
                        // row stayed on "Sending..." forever.

                        if (msg.msgId) {
                            for (var vdi0 = 0; vdi0 < msgModel.size(); vdi0++) {
                                var vdv0 = msgModel.value(vdi0);
                                if (vdv0.msgId === msg.msgId && vdv0.msgId.indexOf("local_") !== 0) {
                                    return;
                                }
                            }
                        }

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
                            msgModel.replace(vPlaceholderIdx, msg);
                            handledInPlace = true;
                        }
                    } else if (msg.msgType === 6 || msg.msgType === "6") {
                        // Link-share counterpart: the plain-text "local_..." placeholder (content = the raw URL) never
                        // matches the WS echo, whose content the server rewrites to {linkTitle, linkHref} JSON (mt==6),
                        // so a duplicate row was appended. Replace the newest plain-text "local_" placeholder in place
                        // (by recency, not string equality).

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
                        if (chatViewPage.adoptTextPlaceholder(msg))
                            handledInPlace = true;
                    }
                }

                if (!handledInPlace) {
                    var dupIdx = -1;
                    for (var di = 0; di < msgModel.size(); di++) {
                        if (msgModel.value(di).msgId === msg.msgId) { dupIdx = di; break; }
                    }
                    if (dupIdx >= 0) {
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
                    // Link thumbnail: ImageView can't load remote https:// URLs, so reuse downloadImageMessage()
                    // (-> imageMsgReady -> applyImageUpdate() -> msg.localImage) instead of binding linkThumb to
                    // imageSource.
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
            }

            onMessageRecalled: {
                if (threadId !== chatViewPage.threadId) return;
                chatViewPage.applyRecall(msgId);
            }

            // Companion to the C++ fix in ZaloService_WebSocket.cpp: after the DB row's local-clock ts is
            // corrected via the WS echo, patch the loaded in-memory row too or it keeps the stale ts until
            // the thread is reopened.
            onMessageTsCorrected: {
                if (threadId !== chatViewPage.threadId) return;
                chatViewPage.applyTsCorrection(msgId, newTs);
            }

            onMessageStatesChanged: {
                if (threadId !== chatViewPage.threadId) return;
                chatViewPage.msgStates = zService.getThreadMessageStates(threadId) || {};
                chatViewPage.rebuildGroups();
            }

            onMessageRecalledDone: {
                if (threadId !== chatViewPage.threadId) return;
                if (success) {
                    chatViewPage.applyRecall(msgId);
                } else {
                    errorToast.body = error.length > 0 ? error : "Failed to recall message";
                    errorToast.show();
                }
            }

            onMessageDeleted: {
                if (threadId !== chatViewPage.threadId) return;
                if (success) {
                    chatViewPage.applyLocalDelete(msgId);
                } else {
                    errorToast.body = error.length > 0 ? error : "Failed to delete message";
                    errorToast.show();
                }
            }

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
            onContactSelected:  { zService.pickContact(chatViewPage.threadId); }
        },

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

        // pickContact() result: the built .vcf is sent via sendFile(), like VoiceNoteSheet.onVoiceNoteReady.
        // No toast on cancel (a user action), only for "error". MUST filter by threadId in both
        // handlers: zService is a singleton and every live ChatView page would send the file (one
        // contact sent 3 .vcf files).
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
                var cachedPath = zService.cacheLocalImage(path);
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

        // FileType.Other is correct for this File button: the picker shows all formats (a former
        // extension whitelist rejected other formats after selection and was removed). Content shape
        // {fileName, href, fileSize} and msgType=3 are shared with video (see videoBubble), so one
        // bubble and one reconcile/download pipeline serve both; only the icon differs (by extension,
        // iconFor()). Uses the chunked upload (<=512K) with real progress via fileUploadProgress.
        FilePicker {
            id: filePicker
            type: FileType.Other
            mode: FilePickerMode.Picker
            title: "Select File"
            onFileSelected: {
                chatViewPage.stageAndSendLocalFile(selectedFiles[0]);
            }
        },

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
