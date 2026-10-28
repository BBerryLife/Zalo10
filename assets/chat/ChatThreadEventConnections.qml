import bb.cascades 1.4
import bb.system 1.0
import QtQuick 1.0

Connections {
    target: zService

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
}
