import bb.cascades 1.4
import bb.system 1.0
import QtQuick 1.0

Connections {
    target: zService

    onConversationsReady: {
        groupsNav.lastThreads = threads;
        groupsNav.buildGroups(threads, false);
    }

    onHistorySynced: {
        if (groupsNav.lastThreads && groupsNav.lastThreads.length > 0)
            groupsNav.buildGroups(groupsNav.lastThreads, true);
    }
    onPinnedChanged: {
        if (groupsNav.lastThreads && groupsNav.lastThreads.length > 0)
            groupsNav.buildGroups(groupsNav.lastThreads, true);
    }

    onAvatarReady: {
        for (var i = 0; i < groupModel.size(); i++) {
            var d = groupModel.value(i);
            if (d.threadId === threadId) {
                d.localAvatar = localPath;
                groupModel.replace(i, d);
                break;
            }
        }
        if (groupsNav.searchVisible) {
            for (var j = 0; j < searchModel.size(); j++) {
                var sd = searchModel.value(j);
                if (sd.threadId === threadId) {
                    sd.localAvatar = localPath;
                    searchModel.replace(j, sd);
                    break;
                }
            }
        }
    }

    onLoginSuccess: {
        if (groupModel.size() === 0) {
            groupsNav.refreshCooldown = true;
            groupsRefreshCooldownTimer.restart();
            zService.fetchConversations();
            groupsLoading.visible = true;
        }
    }

    onNewMessage: {
        if (message.isGroup === true || message.isGroup === "true") {
            var isMine = (message.isMine === true || message.isMine === "true" || message.isMine === 1);
            var tid = threadId;
            var snippet = (message.msgType === 2 || message.msgType === "2")
                ? "[Photo]" : groupsNav.msgSnippet(message.msgType, message.content);
            if (!isMine) groupsNav.onUnreadMessage();
            for (var i = 0; i < groupModel.size(); i++) {
                var d = groupModel.value(i);
                if (d.threadId === tid) {
                    d.lastMessage    = snippet;
                    d.lastMsgIsMine  = isMine;
                    d.lastMsgId      = message.msgId || "";
                    d.lastMsgState   = isMine ? 1 : 0;
                    d.lastSenderName = message.dName || "";
                    d.hasUnread      = !isMine && !zService.isActiveThread(tid);
                    zService.setThreadUnread(tid, d.hasUnread);
                    if (message.ts) d.lastTime = groupsNav.formatTime(message.ts);
                    groupModel.removeAt(i);
                    groupModel.insert(d.isPinned ? 0 : groupsNav.firstUnpinnedIndex(), d);
                    break;
                }
            }
        }
    }

    onThreadRead: {
        for (var i = 0; i < groupModel.size(); i++) {
            var d = groupModel.value(i);
            if (d.threadId === threadId) {
                if (d.hasUnread) { d.hasUnread = false; groupModel.replace(i, d); }
                break;
            }
        }
        for (var j = 0; j < searchModel.size(); j++) {
            var sd = searchModel.value(j);
            if (sd.threadId === threadId) {
                if (sd.hasUnread) { sd.hasUnread = false; searchModel.replace(j, sd); }
                break;
            }
        }
    }

    onThreadMarkedUnread: {
        for (var i = 0; i < groupModel.size(); i++) {
            var d = groupModel.value(i);
            if (d.threadId === threadId) {
                if (!d.hasUnread) { d.hasUnread = true; groupModel.replace(i, d); }
                break;
            }
        }
        for (var j = 0; j < searchModel.size(); j++) {
            var sd = searchModel.value(j);
            if (sd.threadId === threadId) {
                if (!sd.hasUnread) { sd.hasUnread = true; searchModel.replace(j, sd); }
                break;
            }
        }
    }

    onMessageStatesChanged: {
        var states = zService.getThreadMessageStates(threadId) || {};
        for (var i = 0; i < groupModel.size(); i++) {
            var d = groupModel.value(i);
            if (d.threadId === threadId) {
                if (d.lastMsgIsMine === true && d.lastMsgId) {
                    var st = states[d.lastMsgId] || 1;
                    if (st !== d.lastMsgState) { d.lastMsgState = st; groupModel.replace(i, d); }
                }
                break;
            }
        }
        for (var j = 0; j < searchModel.size(); j++) {
            var sd = searchModel.value(j);
            if (sd.threadId === threadId) {
                if (sd.lastMsgIsMine === true && sd.lastMsgId) {
                    var sst = states[sd.lastMsgId] || 1;
                    if (sst !== sd.lastMsgState) { sd.lastMsgState = sst; searchModel.replace(j, sd); }
                }
                break;
            }
        }
    }

    onMessageSent: {
        if (success) return;
        for (var i = 0; i < groupModel.size(); i++) {
            var d = groupModel.value(i);
            if (d.threadId === threadId) {
                d.lastMessage   = "Message not sent";
                d.lastMsgIsMine = true;
                d.lastMsgId     = "";
                d.lastMsgState  = 4;
                groupModel.replace(i, d);
                break;
            }
        }
    }

    onClearHistoryDone: {
        if (!success) return;
        for (var i = 0; i < groupModel.size(); i++) {
            var d = groupModel.value(i);
            if (d.threadId === threadId) {
                d.lastMessage    = "";
                d.lastMsgIsMine  = false;
                d.lastMsgId      = "";
                d.lastMsgState   = 0;
                d.lastSenderName = "";
                d.hasUnread      = false;
                zService.setThreadUnread(threadId, false);
                groupModel.replace(i, d);
                return;
            }
        }
    }

    onLeaveGroupDone: {
        if (!success) return;
        for (var i = 0; i < groupModel.size(); i++) {
            if (groupModel.value(i).threadId === groupId) {
                groupModel.removeAt(i);
                break;
            }
        }
        // Pop back directly in case the popRequested -> popWatcher chain doesn't fire.
        if (groupsNav.currentPage && groupsNav.currentPage.threadId === groupId) {
            groupsNav.pop();
        }
    }
}
