import bb.cascades 1.4
import QtQuick 1.0

// Quick view of a chat, shown when a BlackBerry Hub notification is tapped (Settings > Notifications > Quick View
// from Hub). It is the normal ChatView in a sheet whose title bar has Cancel and Open: Cancel closes the sheet,
// Open closes it and goes to the conversation in the app (openRequested).
// The ChatView sits in a NavigationPane like in the Chats tab, so the pane disposes of the page the same way.
Sheet {
    id: hubSheet

    property string threadId: ""
    property bool   isGroup: false

    signal openRequested(string openThreadId, bool openIsGroup)

    function openFor(tid, group, name, avatar, selfName) {
        while (hubNav.count() > 1) hubNav.pop();
        var page = chatDef.createObject();
        if (!page) return;
        hubSheet.threadId = tid;
        hubSheet.isGroup  = group;
        page.threadId   = tid;
        page.threadName = name;
        page.isGroup    = group;
        page.avatarUrl  = avatar;
        page.selfName   = selfName;
        page.sheetMode  = true;
        hubConn.target  = page;
        hubNav.push(page);
        hubSheet.open();
        page.startChat();
    }

    function finish(openIt) {
        var tid = hubSheet.threadId;
        var grp = hubSheet.isGroup;
        hubSheet.close();
        if (openIt) hubSheet.openRequested(tid, grp);
    }

    onClosed: {
        hubConn.target = null;
        while (hubNav.count() > 1) hubNav.pop();
    }

    attachedObjects: [
        ComponentDefinition {
            id: chatDef
            source: "asset:///ChatView.qml"
        },
        Connections {
            // target starts null and is assigned in openFor(): the page does not exist before
            id: hubConn
            target: null
            onSheetCancel: { hubSheet.finish(false); }
            onSheetOpen:   { hubSheet.finish(true); }
        }
    ]

    NavigationPane {
        id: hubNav
        peekEnabled: false
        backButtonsVisible: false

        onPopTransitionEnded: { zService.clearActiveThread(); }

        Page { }
    }
}
