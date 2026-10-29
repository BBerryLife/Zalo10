import bb.cascades 1.4
import bb.cascades.pickers 1.0
import bb.system 1.0
import QtQuick 1.0
import "chat"

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

    titleBar: ChatTitleBar {}

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
                // Pinned messages jump to the message in the chat. Notes and polls live only in the
                // Group Board, so open it.
                if (boardType === "pin") {
                    chatViewPage.jumpToMessage(itemId);
                } else {
                    chatViewPage.groupBoardRequested = true;
                }
            }
            onMoreRequested: { chatViewPage.groupBoardRequested = true; }
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
                zService.deleteMessage(chatViewPage.threadId, chatViewPage.isGroup, msgId, cliMsgId, senderId, true);
            }
            function doDeleteMsg(msgId)    {}

            listItemComponents: [
                ListItemComponent {
                    type: ""
                    ChatBubbleItem {}
                }
            ]
        }

        Container {
            id: emojiPanelSlot
            horizontalAlignment: HorizontalAlignment.Fill
            topMargin: ui.du(0.8)
            visible: false
        }

        ChatQuickMessageBar {}

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

        ChatAttachPreviewBar {}

        ChatReplyPreviewBar {}

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
            onTriggered: { chatViewPage.groupBoardRequested = true; }
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
            if (id.length === 0 || id.indexOf("local_") === 0) continue;
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

        // Feeds PinboardBar. Polls are NOT rendered in the chat: they only live in the Group Board.
        // Filtered by groupId so a stale fetch for a thread we've left can't clobber this one.
        Connections {
            target: zService
            onGroupBoardReady: {
                if (groupId !== chatViewPage.threadId) return;
                chatViewPage.boardItems = chatViewPage.filterBoardItems(items);
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
                    }
                }
                chatViewPage.loadBoardItems();
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

        Connections {
            target: app

            onShowRecalledMessagesChanged: {
                chatViewPage.showRecalledMessages = show;
            }
            onDarkThemeChanged: {
                chatViewPage.isDark = dark;
            }
        },

        ChatMessageFlowConnections {},
        ChatThreadEventConnections {},

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
