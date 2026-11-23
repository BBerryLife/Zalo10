import bb.cascades 1.4

// Long-press menu of a bubble on the Hub reply card: only what the card can do by itself (HTTP, no WebSocket).
// Everything else (reaction, forward, pin, download, share, resend) is in the conversation inside the app.
ActionSet {
    title: "Message"
    ActionItem {
        title: "Copy"
        imageSource: "asset:///images/ChatView/ic_copy.png"
        enabled: !(ListItemData.msgType === 2 || ListItemData.msgType === "2")
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
        title: "Recall"
        imageSource: "asset:///images/ChatView/ic_retract_message.png"
        enabled: rowRoot.canRecall(ListItemData.ts)
        onTriggered: { rowRoot.ListItem.view.doRecallMsg(ListItemData.msgId, ListItemData.cliMsgId, rowRoot.mine); }
    }
    ActionItem {
        title: "Delete for me only"
        imageSource: "asset:///images/ChatView/action_delete.png"
        onTriggered: { rowRoot.ListItem.view.doDeleteForMe(ListItemData.msgId, ListItemData.cliMsgId, ListItemData.senderId); }
    }
}
