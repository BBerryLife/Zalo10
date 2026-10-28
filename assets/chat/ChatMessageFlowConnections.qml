import bb.cascades 1.4
import bb.system 1.0
import QtQuick 1.0

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
}
