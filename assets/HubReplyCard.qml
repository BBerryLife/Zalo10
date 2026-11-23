import bb.cascades 1.4
import QtQuick 1.0
import bb.system 1.0
import "chat"

// The Hub "Reply" card (card.composer): a small ChatView. Same bubbles (ChatBubbleItem), same background and input
// row as the chat, but only reading and replying: no attachments, stickers, search, board or long-press menu.
// The Hub opens it over its own screen; ApplicationUI loads this file instead of main.qml when the app is started
// in InvokeCard mode. This process has no WebSocket and no Hub account: it reads the conversation from the local
// database (the main app keeps it up to date), sends over HTTP and looks at the database every few seconds.
// Cancel closes the card, Open hands the conversation to the main app.
Page {
    id: card

    property string threadId: ""
    property bool   isGroup: false
    property string cardTitle: ""
    property bool   isDark: app.getDarkTheme()
    property string pendingText: ""
    property string lastSent: ""
    property bool   sending: false
    property bool   retried: false
    property string lastSig: ""
    property string photoAsked: ""        // "|msgId|" of photos whose download was already started
    property double lastLoadTs: 0
    // Quote-reply: set by the "Reply" entry of a bubble's long-press menu.
    property string pendingReplyMsgId: ""
    property string pendingReplyCliMsgId: ""
    property string pendingReplyOwnerId: ""
    property string pendingReplySenderName: ""
    property string pendingReplyContent: ""
    property int    pendingReplyMsgType: 0
    property string pendingReplyTs: ""
    property string sentQuoteJson: ""     // the quote of the message being sent, kept for a retry

    function reloadSession() {
        lastLoadTs = new Date().getTime();
        zService.loadSession();
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

    function quoteSnippet(content, msgType) {
        if (msgType === 2) return "[Photo]";
        if (msgType === 6) return "[Sticker]";
        var c = String(content || "");
        if (c.charAt(0) === "{") return "[Attachment]";
        return c;
    }

    function clearReply() {
        pendingReplyMsgId = "";
        pendingReplyCliMsgId = "";
        pendingReplyOwnerId = "";
        pendingReplySenderName = "";
        pendingReplyContent = "";
        pendingReplyMsgType = 0;
        pendingReplyTs = "";
        sendAction.enabled = (inputField.text.replace(/^\s+|\s+$/g, "").length > 0);
    }

    function startReply(msgId, cliMsgId, senderId, isMine, rawDName, content, msgType, ts) {
        var name;
        if (isMine) name = "Me";
        else if (!isGroup && cardTitle.length > 0) name = cardTitle;
        else if (isGroup) {
            var mem = zService.memberDisplayName(senderId || "");
            name = (mem && mem.length > 0) ? mem : (rawDName || "Unknown");
        } else name = rawDName || "Unknown";
        pendingReplyMsgId = msgId || "";
        pendingReplyCliMsgId = cliMsgId || "";
        pendingReplyOwnerId = senderId || "";
        pendingReplySenderName = name;
        pendingReplyContent = content || "";
        pendingReplyMsgType = parseInt(msgType, 10) || 0;
        pendingReplyTs = String(ts || "");
        sendAction.enabled = true;
        inputField.requestFocus();
    }

    // The database is updated by the answer to the request: look again soon, not only at the next poll.
    function reloadSoon() {
        reloadTimer.restart();
    }

    // Every message of the card (a failed send, a hint) is a system toast; there is no text line in the page.
    function tell(msg) {
        cardToast.body = msg;
        cardToast.show();
    }

    // The newest messages of the conversation from the database, oldest first, as rows for ChatBubbleItem.
    function loadHistory() {
        if (threadId.length === 0) return;
        var rows = zService.dbLoadMessagesPage(threadId, "", 20);
        var n = rows ? rows.length : 0;
        var sig = n + ":";
        for (var si = 0; si < n; si++) sig += String(rows[si].msgId) + "/" + String(rows[si].msgType) + ",";
        if (sig === lastSig) return;                 // nothing new
        lastSig = sig;

        var states = zService.getThreadMessageStates(threadId) || {};
        msgModel.clear();
        var nm = "";
        for (var i = 0; i < n; i++) {
            var c = rows[i];
            c.isMine = (c.isMine === "true" || c.isMine === 1 || c.isMine === true);
            c.selfName = "Me";
            c.kind = "message";
            c.grouped = false;
            c.bubblePos = "full";
            c.reactions = [];
            if (c.isMine && c.msgId && states[c.msgId]) c.msgState = states[c.msgId];
            if (!c.isMine && nm.length === 0 && c.dName && String(c.dName).length > 0) nm = String(c.dName);
            msgModel.append(c);

            var isPhoto = (c.msgType === 2 || c.msgType === "2");
            var hasLocal = (c.localImage && c.localImage.length > 0);
            if (isPhoto && !hasLocal && c.msgId && photoAsked.indexOf("|" + c.msgId + "|") < 0) {
                var u = card.extractPhotoUrl(c.content || "");
                if (u.length > 0) {
                    photoAsked = photoAsked + "|" + c.msgId + "|";
                    zService.downloadImageMessage(c.msgId, u, threadId);
                }
            }
        }
        if (cardTitle.length === 0) {
            if (nm.length > 0) cardTitle = nm;
            else cardTitle = isGroup ? "Group chat" : "Chat";
        }
        scrollTimer.restart();
    }

    function trySend() {
        if (pendingText.length === 0 || threadId.length === 0) return;
        if (!zService.loggedIn) return;              // retried from onLoggedInChanged
        var t = pendingText;
        pendingText = "";
        if (pendingReplyMsgId.length > 0) {
            var qServerType = (pendingReplyMsgType === 2) ? 32 : 1;
            var qContent = (pendingReplyMsgType === 2) ? "[Photo]" : pendingReplyContent;
            zService.sendMessageQuote(threadId, t, isGroup, pendingReplyMsgId, pendingReplyCliMsgId,
                                      pendingReplyOwnerId, qContent, qServerType, pendingReplyTs, pendingReplySenderName);
        } else {
            zService.sendMessage(threadId, t, isGroup);
        }
    }

    function startSend() {
        var t = inputField.text.replace(/^\s+|\s+$/g, "");
        if (t.length === 0 || sending) return;
        sending = true;
        retried = false;
        pendingText = t;
        lastSent = t;
        // Not signed in yet: the text stays pending and goes out from onLoggedInChanged once the session
        // restored by onCreationCompleted is ready; timeoutTimer reports a failure otherwise.
        if (zService.loggedIn) trySend();
        timeoutTimer.restart();
    }

    titleBar: TitleBar {
        title: card.cardTitle
        dismissAction: ActionItem {
            title: "Cancel"
            onTriggered: { app.closeCard(); }
        }
        acceptAction: ActionItem {
            title: "Open"
            onTriggered: { app.openThreadFromCard(card.threadId, card.isGroup); }
        }
    }

    actions: [
        ActionItem {
            id: sendAction
            title: "Send"
            imageSource: "asset:///images/ChatView/ConversationPaneSend.png"
            ActionBar.placement: ActionBarPlacement.OnBar
            enabled: false
            onTriggered: { card.startSend(); }
        }
    ]

    content: Container {
        layout: StackLayout { orientation: LayoutOrientation.TopToBottom }
        horizontalAlignment: HorizontalAlignment.Fill
        verticalAlignment:   VerticalAlignment.Fill
        background: card.isDark ? Color.black : Color.create("#d6d6d6")

        ListView {
            id: msgList
            // What ChatBubbleItem and its bubbles read from their list (the same proxies as ChatView's list).
            property real   widthHint: msgListLUH.layoutFrame.width
            property bool   isDark: card.isDark
            property bool   showRecalledMessages: app.getShowRecalledMessages()
            property bool   isAdminOrOwner: false
            property bool   isGroupChat: card.isGroup
            property string threadNameProxy: card.cardTitle
            property string selfUidProxy: zService.selfUid
            property string selfNameProxy: "Me"
            property string jumpHighlightMsgId: ""
            property string searchQuery: ""
            property int    searchCurrentMsgIndex: -1
            property int    namesVersion: 0
            property string videoProgressMsgId: ""
            property int    videoProgressPercent: 0
            property int    uploadVideoPercent: 0
            property bool   uploadVideoActive: false
            property bool   uploadFileActive: false
            property int    uploadFilePercent: 0
            property bool   cardMode: true   // ChatBubbleItem then uses the card's own long-press menu
            function highlightMatchesProxy(text, query, color) { return text; }
            function jumpToMessageProxy(msgId) {}
            function memberDisplayNameProxy(uid) { var v = msgList.namesVersion; return zService.memberDisplayName(uid); }
            function viewPollVotersProxy() {}
            function openPollVoteProxy() {}
            function playVideoMsg() { card.tell("Open the conversation to play videos."); }
            // The card's long-press menu (BubbleActionsCard): Copy, Reply, Recall, Delete for me.
            function doCopy(text, isImage) { if (isImage) return; app.copyToClipboard(text); card.tell("Copied"); }
            function doReply(msgId, cliMsgId, senderId, isMine, rawDName, content, msgType, ts) {
                card.startReply(msgId, cliMsgId, senderId, isMine, rawDName, content, msgType, ts);
            }
            function doRecallMsg(msgId, cliMsgId, isMine) {
                if (!isMine) { card.tell("You can only recall your own messages."); return; }
                zService.recallMessage(card.threadId, card.isGroup, msgId, cliMsgId);
                card.reloadSoon();
            }
            function doDeleteForMe(msgId, cliMsgId, senderId) {
                zService.deleteMessage(card.threadId, card.isGroup, msgId, cliMsgId, senderId, true);
                card.reloadSoon();
            }
            function doSendReaction() {}   // RowReactions only shows pills; nothing is sent from the card

            horizontalAlignment: HorizontalAlignment.Fill
            layoutProperties: StackLayoutProperties { spaceQuota: 1 }
            dataModel: msgModel
            bottomPadding: ui.du(1.5)
            flickMode: FlickMode.Momentum

            attachedObjects: [
                LayoutUpdateHandler { id: msgListLUH },
                ArrayDataModel { id: msgModel }
            ]

            listItemComponents: [
                ListItemComponent {
                    type: ""
                    ChatBubbleItem {}
                }
            ]
        }

        // The message being answered (set from a bubble's "Reply").
        Container {
            visible: card.pendingReplyMsgId.length > 0
            horizontalAlignment: HorizontalAlignment.Fill
            background: card.isDark ? Color.create("#1e2a38") : Color.create("#dce8f5")
            topPadding: ui.du(1.0); bottomPadding: ui.du(1.0)
            leftPadding: ui.du(1.0); rightPadding: ui.du(1.5)
            layout: StackLayout { orientation: LayoutOrientation.LeftToRight }

            Container {
                verticalAlignment: VerticalAlignment.Center
                preferredWidth: ui.du(6); preferredHeight: ui.du(6)
                layout: DockLayout {}
                gestureHandlers: [ TapHandler { onTapped: { card.clearReply(); } } ]
                Label {
                    text: "\u2715"
                    horizontalAlignment: HorizontalAlignment.Center
                    verticalAlignment: VerticalAlignment.Center
                    textStyle { fontSize: FontSize.Medium; fontWeight: FontWeight.Bold
                                color: card.isDark ? Color.create("#aaaaaa") : Color.create("#555555") }
                }
            }
            Container {
                verticalAlignment: VerticalAlignment.Fill
                preferredWidth: ui.du(0.5); minWidth: ui.du(0.5); maxWidth: ui.du(0.5)
                leftMargin: ui.du(0.5); rightMargin: ui.du(1.0)
                background: Color.create("#2575fc")
            }
            Container {
                layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                verticalAlignment: VerticalAlignment.Center
                Label {
                    text: card.pendingReplySenderName
                    multiline: false
                    textStyle { fontSize: FontSize.Small; fontWeight: FontWeight.Bold; color: Color.create("#2575fc") }
                    topMargin: 0; bottomMargin: 0
                }
                Label {
                    text: card.quoteSnippet(card.pendingReplyContent, card.pendingReplyMsgType)
                    multiline: false
                    textStyle { fontSize: FontSize.Small; color: card.isDark ? Color.create("#cfd8e3") : Color.create("#444444") }
                    topMargin: 0; bottomMargin: 0
                }
            }
        }

        Container {
            horizontalAlignment: HorizontalAlignment.Fill
            background: card.isDark ? Color.create("#272727") : Color.create("#FFFFFF")
            topPadding:    ui.du(1.2)
            bottomPadding: ui.du(1.2)
            leftPadding:   ui.du(1.0)
            rightPadding:  ui.du(1.0)
            layout: StackLayout { orientation: LayoutOrientation.LeftToRight }

            TextField {
                id: inputField
                layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                verticalAlignment: VerticalAlignment.Center
                hintText: "Enter a message"
                inputMode: TextFieldInputMode.Chat
                minHeight: ui.du(7)
                backgroundVisible: false
                clearButtonVisible: false
                enabled: !card.sending
                input {
                    flags: TextInputFlag.SpellCheck | TextInputFlag.WordSubstitution
                    submitKey: SubmitKey.Send
                    onSubmitted: { card.startSend(); }
                }
                onTextChanging: { sendAction.enabled = (text.replace(/^\s+|\s+$/g, "").length > 0 && !card.sending); }
            }
        }
    }

    attachedObjects: [
        SystemToast { id: cardToast },
        Connections {
            target: app
            onReplyCardRequested: {
                card.threadId = threadId;
                card.isGroup = isGroup;
                // A group: the Hub item's own title is the group name. A 1-1 chat: the Hub item is titled "Zalo10", so use
                // the person's name as the chat list does (known contact), else the name on their messages (loadHistory()).
                if (isGroup) {
                    var hubTitle = zService.hubThreadTitle(threadId);
                    card.cardTitle = (hubTitle && hubTitle.length > 0) ? hubTitle : "Group chat";
                } else {
                    var peer = zService.memberDisplayName(threadId);
                    card.cardTitle = (peer && peer.length > 0) ? peer : "";
                }
                card.loadHistory();
                // The main app may have renewed the session key since it was saved: take the current one.
                if (new Date().getTime() - card.lastLoadTs > 4000) card.reloadSession();
                inputField.requestFocus();
            }
        },
        Connections {
            target: zService
            onLoggedInChanged: { if (zService.loggedIn) card.trySend(); }
            onMessagesReady: { if (threadId === card.threadId) card.loadHistory(); }
            onImageMsgReady: {
                for (var i = 0; i < msgModel.size(); i++) {
                    var d = msgModel.value(i);
                    if (d.msgId === msgId) {
                        d.localImage = localPath;
                        if (width > 0) d.imgWidth = width;
                        if (height > 0) d.imgHeight = height;
                        msgModel.removeAt(i);
                        msgModel.insert(i, d);
                        break;
                    }
                }
            }
            onMessageSent: {
                if (threadId !== card.threadId) return;
                timeoutTimer.stop();
                if (success) {
                    card.sending = false;
                    inputField.text = "";
                    sendAction.enabled = false;
                    card.retried = false;
                    card.clearReply();
                    card.lastSig = "";
                    card.loadHistory();
                } else if (!card.retried) {
                    // The session key is probably stale: reload the saved session and send once more.
                    card.retried = true;
                    card.pendingText = card.lastSent;
                    timeoutTimer.restart();
                    card.reloadSession();
                } else {
                    card.sending = false;
                    card.retried = false;
                    card.tell("Message failed to send. The session may have expired - open Zalo10 once, then try again.");
                }
            }
        },
        Timer {
            // New messages are written to the database by the main app: look again every few seconds.
            id: pollTimer
            interval: 3000
            repeat: true
            running: card.threadId.length > 0
            onTriggered: { card.loadHistory(); }
        },
        Timer {
            id: scrollTimer
            interval: 120
            repeat: false
            onTriggered: { msgList.scrollToPosition(ScrollPosition.End, ScrollAnimation.None); }
        },
        Timer {
            id: reloadTimer
            interval: 1200
            repeat: false
            onTriggered: { card.lastSig = ""; card.loadHistory(); }
        },
        Timer {
            id: timeoutTimer
            interval: 20000
            repeat: false
            onTriggered: {
                if (card.sending) {
                    card.sending = false;
                    card.tell("Could not send. Check the connection and try again.");
                }
            }
        }
    ]

    onCreationCompleted: {
        // Restore the saved session: HTTP only, the WebSocket and every poller are off in card mode.
        card.reloadSession();
    }
}
