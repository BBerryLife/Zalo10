import bb.cascades 1.4
import bb.system 1.0
import QtQuick 1.0
import "lists"
import "lists/avatarhub.js" as AvatarHub

NavigationPane {
    id: groupsNav
    signal myProfileRequested()
    peekEnabled: false

    property string selfName: ""
    property string selfAvatar: ""
    signal onUnreadMessage()
    property variant currentPage: null
    property variant activeQmPage: null
    property variant activeGroupBoardPage: null
    property bool searchVisible: false
    property string searchText: ""
    property variant allGroups: []
    property bool refreshCooldown: false
    property bool isDark: app.getDarkTheme()
    // The group that was just created, opened by openNewGroupTimer.
    property string pendingGroupId: ""
    property string pendingGroupName: ""
    property string pendingGroupAvatar: ""
    // Number of groups, shown as the Groups tab subtitle ("N Groups") by main.qml.
    property int groupCount: 0
    // Groups that still have unread messages (yellow pellet). main.qml shows the tab's red spark
    // while this is above 0, so the spark only goes away once those groups have been read.
    property int unreadCount: 0
    function recountUnread() {
        var n = 0;
        for (var k = 0; k < groupModel.size(); k++) {
            if (groupModel.value(k).hasUnread === true) n++;
        }
        groupsNav.unreadCount = n;
    }

    attachedObjects: [
        Timer {
            id: groupsRefreshCooldownTimer
            interval: 11000
            repeat: false
            onTriggered: groupsNav.refreshCooldown = false
        },
        // Lite: the old 0.8 s warm-up (createObject()+destroy() of a whole ChatView at start-up) is
        // gone; the first chat opens a little slower instead of slowing every start.
        Timer {
            id: groupAppendTimer
            interval: 40
            repeat: true
            onTriggered: {
                groupsNav.appendPending(100);
                if (groupsNav.pendingPos >= groupsNav.pendingRows.length) groupAppendTimer.stop();
            }
        },
        Timer {
            // Lite: hands avatar requests to zService at a steady pace (see lists/avatarhub.js).
            id: groupAvatarPaceTimer
            interval: 300   // = AvatarHub.TICK_MS
            repeat: true
            onTriggered: {
                if (AvatarHub.drain("groups", zService, Date.now()) === 0) groupAvatarPaceTimer.stop();
            }
        }
    ]

    property variant lastThreads: []

    // Same slice-by-slice append as the Chats tab.
    property variant pendingRows: []
    property int pendingPos: 0
    property bool pendingRestore: false

    function appendPending(n) {
        var rows = groupsNav.pendingRows;
        var end = Math.min(rows.length, groupsNav.pendingPos + n);
        for (var j = groupsNav.pendingPos; j < end; j++) {
            var gg = rows[j];
            groupModel.append(gg);
            var gurl = gg.avatar || "";
            var gtid = gg.threadId || "";
            if (gurl.length > 0 && gtid.length > 0 && !(groupsNav.pendingRestore && gg.localAvatar))
                AvatarHub.enqueue("groups", gtid, gurl);
        }
        groupsNav.pendingPos = end;
        if (AvatarHub.pending("groups") && !groupAvatarPaceTimer.running) groupAvatarPaceTimer.start();
    }

    function buildGroups(threads, restoreAvatars) {
        groupAppendTimer.stop();
        groupsNav.pendingRows = [];
        groupsNav.pendingPos = 0;
        groupsLoading.visible = false;
        AvatarHub.reset("groups");
        AvatarHub.invalidate("groups");
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
                          && (g.lastMessage || "").length > 0;
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
        groupsNav.pendingRows = arr;
        groupsNav.pendingPos = 0;
        groupsNav.pendingRestore = restoreAvatars;
        groupsNav.appendPending(60);
        if (groupsNav.pendingPos < arr.length) groupAppendTimer.restart();
        groupsNav.allGroups = arr;
        groupsNav.groupCount = arr.length;
    }

    // ---- Lite: avatar updates in O(1) instead of scanning the whole list ----
    function applyAvatar(tid, localPath) {
        AvatarHub.arrived("groups", tid);
        var hit = AvatarHub.find("groups", groupModel, tid);
        if (hit && hit.row.localAvatar !== localPath) {
            hit.row.localAvatar = localPath;
            groupModel.replace(hit.i, hit.row);
        }
        if (groupsNav.searchVisible) {
            var sh = AvatarHub.find("groupsSearch", searchModel, tid);
            if (sh && sh.row.localAvatar !== localPath) {
                sh.row.localAvatar = localPath;
                searchModel.replace(sh.i, sh.row);
            }
        }
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
        AvatarHub.invalidate("groupsSearch");
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

        titleBar: ProfileTitleBar {
            selfName:   groupsNav.selfName
            selfAvatar: groupsNav.selfAvatar
            profileHandled: true
            onProfileClicked: groupsNav.myProfileRequested()
            addIcon: "asset:///images/GroupsTab/ic_create_group_disabled.png"
            addDialogTitle: "Create Group"
            addHandled: true
            onAddClicked: groupsNav.openCreateGroup()
        }

        shortcuts: [
            Shortcut {
                key: "s"
                onTriggered: { groupsSearchField.requestFocus(); }
            }
        ]

        actions: [
            ActionItem {
                title: groupsNav.refreshCooldown ? "Please wait..." : "Refresh"
                enabled: !groupsNav.refreshCooldown
                imageSource: "asset:///images/GroupsTab/ic_sync.png"
                ActionBar.placement: ActionBarPlacement.InOverflow
                onTriggered: {
                    if (groupsNav.refreshCooldown) return;
                    groupsNav.refreshCooldown = true;
                    groupsRefreshCooldownTimer.restart();
                    groupAppendTimer.stop();
                    groupModel.clear();
                    AvatarHub.invalidate("groups");
                    zService.fetchConversations();
                    groupsLoading.visible = true;
                }
            }
        ]

        content: Container {
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment: VerticalAlignment.Fill

            FocusSink {}

            // Search box under the header, drawn with a visible border. Colours follow the theme
            // (Settings > Dark Theme) live, through groupsNav.isDark.
            Container {
                background: groupsNav.isDark ? Color.create("#000000") : Color.create("#f2f2f2")
                horizontalAlignment: HorizontalAlignment.Fill
                topPadding: ui.du(1.0); bottomPadding: ui.du(1.0)
                leftPadding: ui.du(1.5); rightPadding: ui.du(1.5)
                Container {
                    background: groupsNav.isDark ? Color.create("#3a3a3a") : Color.create("#9a9a9a")
                    horizontalAlignment: HorizontalAlignment.Fill
                    topPadding: ui.du(0.2); bottomPadding: ui.du(0.2)
                    leftPadding: ui.du(0.2); rightPadding: ui.du(0.2)
                    Container {
                        background: groupsNav.isDark ? Color.create("#1a1a1a") : Color.White
                        horizontalAlignment: HorizontalAlignment.Fill
                        TextField {
                            id: groupsSearchField
                            hintText: "Search groups..."
                            backgroundVisible: false
                            horizontalAlignment: HorizontalAlignment.Fill
                            textStyle.color: groupsNav.isDark ? Color.White : Color.Black
                            onTextChanging: {
                                groupsNav.searchText = text;
                                groupsNav.searchVisible = (text.trim().length > 0);
                                groupsNav.filterList();
                            }
                            onCreationCompleted: {
                                inputMode.type = TextInputFlag.AutoCapitalizationOff | TextInputFlag.AutoCorrectionOff | TextInputFlag.SpellCheckOff | TextInputFlag.PredictionOff;
                            }
                        }
                    }
                }
            }

            Container {
            layout: DockLayout {}
            layoutProperties: StackLayoutProperties { spaceQuota: 1 }
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment: VerticalAlignment.Fill

            ListView {
                id: groupList
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment: VerticalAlignment.Fill
                dataModel: groupsNav.searchVisible ? searchModel : groupModel

                property bool isDark: app.getDarkTheme()   // read by rows through ListItem.view
                function itemType(data, indexPath) { return "item"; }

                // Long-press "Mark as Read / Unread": Hub item, pellet and official Zalo all follow (ZaloService).
                function toggleUnread(indexPath) {
                    var d = dataModel.data(indexPath);
                    if (!d) return;
                    var tid = d.threadId || "";
                    if (tid.length === 0) return;
                    if (d.hasUnread === true) zService.markThreadReadUser(tid, true);
                    else zService.markThreadUnreadUser(tid, true);
                }

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
        }

        attachedObjects: [
            // Create Group (same sheet as Invite More in a one-to-one chat, started with nobody ticked).
            GroupInviteSheet {
                id: groupCreateSheet
                isDark: groupsNav.isDark
                onGroupCreated: {
                    groupsNav.pendingGroupId     = newGroupId;
                    groupsNav.pendingGroupName   = newName;
                    groupsNav.pendingGroupAvatar = newAvatar;
                    zService.fetchConversations();
                    openNewGroupTimer.restart();
                }
            },
            Timer {
                // Opens the new group a moment after the sheet has closed, not from inside its own signal.
                id: openNewGroupTimer
                interval: 200
                repeat: false
                onTriggered: groupsNav.openThread(groupsNav.pendingGroupId, true, groupsNav.pendingGroupName, groupsNav.pendingGroupAvatar)
            },
            ArrayDataModel {
                id: groupModel
                onItemAdded:    { unreadRecountTimer.restart(); }
                onItemRemoved:  { unreadRecountTimer.restart(); }
                onItemUpdated:  { unreadRecountTimer.restart(); }
                onItemsChanged: { unreadRecountTimer.restart(); }
            },
            Timer {
                id: unreadRecountTimer
                interval: 250
                repeat: false
                onTriggered: { groupsNav.recountUnread(); }
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
            ComponentDefinition {
                id: stickerShopPageDef
                source: "asset:///sticker/StickerShopPage.qml"
            },
            ComponentDefinition {
                id: stickerSettingsPageDef
                source: "asset:///sticker/StickerSettingsPage.qml"
            },
            Connections {
                target: app
                onDarkThemeChanged: { groupsNav.isDark = dark; groupList.isDark = dark; }
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
                onStickerShopRequestedChanged: {
                    if (!groupsNav.currentPage || !groupsNav.currentPage.stickerShopRequested) return;
                    groupsNav.currentPage.stickerShopRequested = false;
                    var shopPage = stickerShopPageDef.createObject();
                    if (shopPage) groupsNav.push(shopPage);
                }
                onStickerSettingsRequestedChanged: {
                    if (!groupsNav.currentPage || !groupsNav.currentPage.stickerSettingsRequested) return;
                    groupsNav.currentPage.stickerSettingsRequested = false;
                    var stPage = stickerSettingsPageDef.createObject();
                    if (stPage) groupsNav.push(stPage);
                }
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

    function openCreateGroup() {
        groupCreateSheet.openCreate("", "", "", "");
    }

    // nameOverride / avatarOverride: a group that was just created is not in groupModel yet.
    function openThread(threadId, isGroup, nameOverride, avatarOverride) {
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
        if (threadName === "Group" && nameOverride && nameOverride.length > 0) threadName = nameOverride;
        if (avatarOverride && avatarOverride.length > 0 && avatarUrl.indexOf("asset:") === 0) avatarUrl = avatarOverride;
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
