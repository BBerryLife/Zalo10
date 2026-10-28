import bb.cascades 1.4

ActionSet {
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
}
