import bb.cascades 1.4
import bb.system 1.0
import QtQuick 1.0
import "lists"

NavigationPane {
    id: groupsNav
    peekEnabled: false

    property string selfName: ""
    signal onUnreadMessage()
    property variant currentPage: null
    property variant activeQmPage: null
    property variant activeGroupBoardPage: null
    property bool searchVisible: false
    property string searchText: ""
    property variant allGroups: []
    property bool refreshCooldown: false

    attachedObjects: [
        Timer {
            id: groupsRefreshCooldownTimer
            interval: 11000
            repeat: false
            onTriggered: groupsNav.refreshCooldown = false
        },
        Timer {
            id: groupViewWarmupTimer
            interval: 800
            repeat: false
            running: true
            onTriggered: {
                var w = groupsDef.createObject();
                if (w) w.destroy();
            }
        }
    ]

    property variant lastThreads: []

    function buildGroups(threads, restoreAvatars) {
        groupsLoading.visible = false;
        var avatarByTid = {};
        if (restoreAvatars) {
            for (var a = 0; a < groupModel.size(); a++) {
                var ad = groupModel.value(a);
                if (ad.threadId && ad.localAvatar) avatarByTid[ad.threadId] = ad.localAvatar;
            }
        }
        var pinnedIds = zService.pinnedThreadIds();
        var pinMap = {};
        for (var pn = 0; pn < pinnedIds.length; pn++) pinMap[pinnedIds[pn]] = true;
        groupModel.clear();
        var arr = [];
        // Restore last messages from local history: the server's group list has none.
        var lastMsgs = zService.getThreadLastMessages();
        var unreadIds = zService.unreadThreadIds();
        var unreadMap = {};
        for (var u = 0; u < unreadIds.length; u++) unreadMap[unreadIds[u]] = true;
        for (var i = 0; i < threads.length; i++) {
            if (!threads[i].isGroup) continue;
            // Copy: writes into a list handed over by a C++ signal can silently no-op on this runtime.
            var g = {};
            for (var gk in threads[i]) g[gk] = threads[i][gk];
            g.localAvatar    = (restoreAvatars && avatarByTid[g.threadId || ""]) ? avatarByTid[g.threadId] : "";
            g.isPinned       = (pinMap[g.threadId || ""] === true);
            g.lastMsgIsMine  = false;
            g.lastMsgId      = "";
            g.lastMsgState   = 0;
            g.lastSenderName = "";
            g.sortTs         = 0;
            var lm = lastMsgs[g.threadId || ""];
            if (lm) {
                var isMine = (lm.isMine === true || lm.isMine === "true" || lm.isMine === 1);
                var mt = lm.msgType;
                var snippet;
                if (mt === 99 || mt === "99") {
                    snippet = isMine ? "You recalled a message" : "This message was recalled";
                } else if (mt === 2 || mt === "2") {
                    snippet = "[Photo]";
                } else {
                    snippet = groupsNav.msgSnippet(mt, lm.content);
                }
                g.lastMessage    = snippet;
                g.lastMsgIsMine  = isMine;
                g.lastMsgId      = lm.msgId || "";
                g.lastMsgState   = isMine ? (lm.state || 1) : 0;
                g.lastSenderName = lm.dName || "";
                g.lastTime       = lm.ts || "";
            }
            g.hasUnread = (unreadMap[g.threadId || ""] === true)
                          && (g.lastMessage || "").length > 0 && !g.lastMsgIsMine;
            if (g.lastTime && g.lastTime !== "") {
                var ts = parseInt(g.lastTime);
                if (!isNaN(ts)) {
                    g.sortTs = ts;
                    g.lastTime = groupsNav.formatTime(ts);
                }
            }
            arr.push(g);
        }
        arr.sort(function(a, b) {
            var pa = a.isPinned ? 1 : 0, pb = b.isPinned ? 1 : 0;
            if (pa !== pb) return pb - pa;
            return (b.sortTs || 0) - (a.sortTs || 0);
        });
        for (var j = 0; j < arr.length; j++) {
            var gg = arr[j];
            groupModel.append(gg);
            var gurl = gg.avatar || "";
            var gtid = gg.threadId || "";
            if (gurl.length > 0 && gtid.length > 0 && !(restoreAvatars && gg.localAvatar))
                zService.downloadAvatar(gtid, gurl);
        }
        groupsNav.allGroups = arr;
    }

    function firstUnpinnedIndex() {
        var n = 0;
        for (var k = 0; k < groupModel.size(); k++) {
            if (groupModel.value(k).isPinned) n++; else break;
        }
        return n;
    }

    function filterList() {
        var q = groupsNav.searchText.toLowerCase().trim();
        searchModel.clear();
        for (var i = 0; i < groupModel.size(); i++) {
            var g = groupModel.value(i);
            if (q.length === 0) {
                searchModel.append(g);
            } else {
                var name = (g.name || "").toLowerCase();
                if (name.indexOf(q) !== -1) searchModel.append(g);
            }
        }
    }

    onCurrentPageChanged: {
        if (!currentPage) return;
        popWatcher.target = currentPage;
    }

    function formatTime(timestamp) {
        if (!timestamp || timestamp === "") return "";
        var date = new Date(timestamp * 1);
        var now  = new Date();
        if (date.toDateString() === now.toDateString()) {
            var h = date.getHours(), m = date.getMinutes();
            var ampm = h >= 12 ? "PM" : "AM";
            h = h % 12 || 12;
            return h + ":" + (m < 10 ? "0" : "") + m + " " + ampm;
        }
        if ((now - date) < 7 * 24 * 60 * 60 * 1000) {
            return ["Sun","Mon","Tue","Wed","Thu","Fri","Sat"][date.getDay()];
        }
        var mon = ["Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"][date.getMonth()];
        return mon + " " + date.getDate();
    }

    // Handles msgType===3 (video/file) like ChatsTab; otherwise the preview showed raw JSON.
    function extractFileName(content) {
        var c = content || "";
        if (c.length === 0 || c.charCodeAt(0) !== 123) return "";
        var key = '"fileName":"';
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
    function msgSnippet(mt, content) {
        if (mt === 4 || mt === "4") {
            var c = content || "";
            var missed = c.indexOf('"callResult":"missed"') >= 0;
            var video  = c.indexOf('"callKind":"video"') >= 0;
            if (missed) return video ? "Missed video call" : "Missed call";
            return video ? "Video call" : "Voice call";
        }
        if (mt === 5 || mt === "5") return "[Sticker]";
        if (mt !== 3 && mt !== "3") return (content || "").substring(0, 60);
        var fname = groupsNav.extractFileName(content);
        var ext = fname.substring(fname.lastIndexOf('.') + 1).toLowerCase();
        var isVideo = (ext === "mp4" || ext === "mov" || ext === "3gp" || ext === "mkv");
        if (isVideo) return "[Video]";
        return fname.length > 0 ? fname : "[File]";
    }
    // Preview line as a function (no block-body binding on Label.text, see videoBubble in
    // ChatView.qml). The prefix is "You: " here but "Me: " in ChatsTab.qml; intentional.
    function lastMessagePreview(lastMessage, lastMsgIsMine, lastSenderName) {
        var lm = lastMessage || "";
        if (lm.length === 0) return "No messages yet";
        var prefix = "";
        if (lastMsgIsMine === true || lastMsgIsMine === "true") {
            prefix = "You: ";
        } else if (lastSenderName && lastSenderName.length > 0) {
            prefix = lastSenderName.split(" ")[0] + ": ";
        }
        return prefix + lm;
    }

    onPopTransitionEnded: {
        zService.clearActiveThread();
    }

    onPushTransitionEnded: {
        if (page && typeof page.flushPendingImages === "function") {
            page.pageVisible = true;
            page.flushPendingImages();
        }
    }

    Page {
        id: groupsPage

        titleBar: GroupsTitleBar {}

        shortcuts: [
            Shortcut {
                key: "s"
                onTriggered: {
                    groupsNav.searchVisible = !groupsNav.searchVisible;
                    if (!groupsNav.searchVisible) {
                        groupsNav.searchText = "";
                        groupsNav.filterList();
                    }
                }
            }
        ]

        actions: [
            ActionItem {
                title: groupsNav.refreshCooldown ? "Please wait..." : "Refresh"
                enabled: !groupsNav.refreshCooldown
                imageSource: "asset:///images/GroupsTab/ic_sync.png"
                ActionBar.placement: ActionBarPlacement.OnBar
                onTriggered: {
                    if (groupsNav.refreshCooldown) return;
                    groupsNav.refreshCooldown = true;
                    groupsRefreshCooldownTimer.restart();
                    groupModel.clear();
                    zService.fetchConversations();
                    groupsLoading.visible = true;
                }
            },
            ActionItem {
                title: "Search"
                imageSource: "asset:///images/GroupsTab/action_icon_search.png"
                ActionBar.placement: ActionBarPlacement.InOverflow
                onTriggered: {
                    groupsNav.searchVisible = !groupsNav.searchVisible;
                    if (!groupsNav.searchVisible) {
                        groupsNav.searchText = "";
                        groupsNav.filterList();
                    }
                }
            },
            ActionItem {
                title: "Create Group"
                imageSource: "asset:///images/GroupsTab/ic_create_group_disabled.png"
                ActionBar.placement: ActionBarPlacement.InOverflow
                onTriggered: { createGroupDialog.show() }
            }
        ]

        content: Container {
            layout: DockLayout {}
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment: VerticalAlignment.Fill

            ListView {
                id: groupList
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment: VerticalAlignment.Fill
                dataModel: groupsNav.searchVisible ? searchModel : groupModel

                function itemType(data, indexPath) { return "item"; }

                // Proxy: delegates can't resolve groupsNav (ReferenceError); they go through
                // ListItem.view.lastMessagePreview().
                function lastMessagePreview(lastMessage, lastMsgIsMine, lastSenderName) {
                    return groupsNav.lastMessagePreview(lastMessage, lastMsgIsMine, lastSenderName);
                }

                listItemComponents: [
                    ListItemComponent {
                        type: "item"
                        GroupsListItem {}
                    }
                ]

                onTriggered: {
                    var t0 = Date.now();
                    var item = dataModel.data(indexPath);
                    var page = groupsDef.createObject();
                    if (!page) return;
                    page.threadId   = item.threadId || "";
                    page.threadName = item.name || "Group";
                    page.isGroup    = true;
                    var av = item.localAvatar || item.avatar || "";
                    if (av.length === 0) av = "asset:///images/GroupsTab/default_group.png";
                    page.avatarUrl  = av;
                    page.selfName   = groupsNav.selfName;
                    var t1 = Date.now();
                    groupsNav.push(page);
                    groupsNav.currentPage = page;
                    groupQmTriggerConn.target = page;
                    groupBoardTriggerConn.target = page;
                    page.startChat();
                }
            }

            ActivityIndicator {
                id: groupsLoading
                horizontalAlignment: HorizontalAlignment.Center
                verticalAlignment: VerticalAlignment.Center
                preferredWidth: ui.du(12)
                preferredHeight: ui.du(12)
                running: visible
                visible: false
            }
        }

        attachedObjects: [
            ArrayDataModel { id: groupModel },
            SystemDialog {
                id: createGroupDialog
                title: "Create Group"
                body: "This feature is still under development."
                confirmButton.label: "OK"
                cancelButton.label: ""
                cancelButton.enabled: false
            },
            ArrayDataModel { id: searchModel },
            ComponentDefinition {
                id: groupsDef
                source: "asset:///ChatView.qml"
            },
            ComponentDefinition {
                id: qmPageDef
                source: "asset:///QuickMessagesSheet.qml"
            },
            ComponentDefinition {
                id: groupBoardPageDef
                source: "asset:///GroupBoardSheet.qml"
            },
            Connections {
                id: groupBoardTriggerConn
                target: null
                onGroupBoardRequestedChanged: {
                    if (!groupsNav.currentPage || !groupsNav.currentPage.groupBoardRequested) return;
                    groupsNav.currentPage.groupBoardRequested = false;
                    var boardPage = groupBoardPageDef.createObject();
                    if (!boardPage) return;
                    boardPage.groupId = groupsNav.currentPage.threadId;
                    boardPage.groupName = groupsNav.currentPage.threadName;
                    boardPage.groupsNavRef = groupsNav;
                    groupsNav.activeGroupBoardPage = boardPage;
                    groupsNav.push(boardPage);
                }
            },
            Connections {
                // target starts null and is assigned in openThread(): binding to groupsNav.currentPage warns
                // while it is null.
                id: groupQmTriggerConn
                target: null
                onQmRequestedChanged: {
                    if (!groupsNav.currentPage || !groupsNav.currentPage.qmRequested) return;
                    groupsNav.currentPage.qmRequested = false;
                    var qmPage = qmPageDef.createObject();
                    if (!qmPage) return;
                    groupsNav.activeQmPage = qmPage;
                    groupQmInsertConn.target = qmPage;
                    groupsNav.push(qmPage);
                }
            },
            Connections {
                id: groupQmInsertConn
                target: null
                onUseInChatRequestedChanged: {
                    if (!groupsNav.activeQmPage || !groupsNav.activeQmPage.useInChatRequested) return;
                    var content = groupsNav.activeQmPage.insertRequestedContent;
                    groupsNav.pop();
                    if (groupsNav.currentPage) groupsNav.currentPage.applyQuickMessage(content);
                }
            },
            Connections {
                id: popWatcher
                target: null
                onPopRequestedChanged: {
                    if (target && target.popRequested) {
                        target.popRequested = false;
                        groupsNav.pop();
                    }
                }
            },
            GroupsServiceConnections {}
        ]
    }

    function openThread(threadId, isGroup) {
        while (groupsNav.count() > 1) {
            groupsNav.pop();
        }
        var page = groupsDef.createObject();
        if (!page) return;
        var threadName = "Group";
        var avatarUrl  = "asset:///images/GroupsTab/default_group.png";
        for (var i = 0; i < groupModel.size(); i++) {
            var d = groupModel.value(i);
            if ((d.threadId || "") === threadId) {
                threadName = d.name || "Group";
                var av = d.localAvatar || d.avatar || "";
                if (av.length > 0) avatarUrl = av;
                break;
            }
        }
        page.threadId   = threadId;
        page.threadName = threadName;
        page.isGroup    = true;
        page.avatarUrl  = avatarUrl;
        page.selfName   = groupsNav.selfName;
        groupsNav.push(page);
        groupsNav.currentPage = page;
        groupQmTriggerConn.target = page;
        groupBoardTriggerConn.target = page;
        page.startChat();
    }
}
