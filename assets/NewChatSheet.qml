import bb.cascades 1.4
import bb.system 1.0
import QtQuick 1.0

// "New Chat" sheet of the Chats tab: a search box, and under it a "Suggested" header with ten people.
// Nobody is listed until you type: the matching names appear as you search (the friend list is only
// searched, never shown whole). Picking someone emits chatRequested() and the Chats tab opens an empty
// chat; the conversation is listed in Chats once its first message exists.
Sheet {
    id: ncSheet

    property bool    isDark: false
    property bool    loading: false
    property bool    searching: false
    property string  queryText: ""
    property string  statusText: ""
    property variant people: []       // every friend: { threadId, name, key (folded name), avatar, localAvatar }
    property variant avatarMap: ({})  // threadId -> picture file already on disk
    property string  foldFrom: "\u00e0\u00e1\u1ea1\u1ea3\u00e3\u00e2\u1ea7\u1ea5\u1ead\u1ea9\u1eab\u0103\u1eb1\u1eaf\u1eb7\u1eb3\u1eb5\u00e8\u00e9\u1eb9\u1ebb\u1ebd\u00ea\u1ec1\u1ebf\u1ec7\u1ec3\u1ec5\u00ec\u00ed\u1ecb\u1ec9\u0129\u00f2\u00f3\u1ecd\u1ecf\u00f5\u00f4\u1ed3\u1ed1\u1ed9\u1ed5\u1ed7\u01a1\u1edd\u1edb\u1ee3\u1edf\u1ee1\u00f9\u00fa\u1ee5\u1ee7\u0169\u01b0\u1eeb\u1ee9\u1ef1\u1eed\u1eef\u1ef3\u00fd\u1ef5\u1ef7\u1ef9\u0111"
    property string  foldTo:   "aaaaaaaaaaaaaaaaaeeeeeeeeeeeiiiiiooooooooooooooooouuuuuuuuuuuyyyyyd"

    signal chatRequested(string threadId, string name, string avatar, string localAvatar)

    // Lower case, no Vietnamese accents: typing "nguyen" finds "Nguyễn".
    function fold(s) {
        var t = (s || "").toLowerCase();
        var out = "";
        for (var i = 0; i < t.length; i++) {
            var c = t.charAt(i);
            var code = t.charCodeAt(i);
            if (code >= 0x300 && code <= 0x36f) continue;   // combining accent
            var k = ncSheet.foldFrom.indexOf(c);
            out += (k >= 0) ? ncSheet.foldTo.charAt(k) : c;
        }
        return out;
    }

    function openSheet() {
        searchField.text = "";
        ncSheet.queryText = "";
        ncSheet.searching = false;
        ncSheet.statusText = "";
        listModel.clear();
        var cached = zService.cachedFriends();
        if (cached.length > 0) {
            ncSheet.loading = false;
            ncSheet.loadPeople(cached);
            ncSheet.showSuggested();
        } else {
            // First time: the friend list is read once, only to be searched here.
            ncSheet.people = [];
            ncSheet.loading = true;
            ncSheet.statusText = "Loading contacts...";
            loadTimeout.restart();
            zService.fetchFriends();
        }
        ncSheet.open();
        focusTimer.restart();
    }

    function closeSheet() {
        ncSheet.loading = false;
        loadTimeout.stop();
        ncSheet.close();
    }

    function loadPeople(list) {
        var arr = [];
        for (var i = 0; i < list.length; i++) {
            var f = list[i];
            var tid = f.threadId || f.uid || "";
            var name = f.name || f.displayName || "";
            if (tid.length === 0 || name.length === 0) continue;
            arr.push({
                threadId: tid, name: name, key: ncSheet.fold(name),
                avatar: f.avatar || "", localAvatar: f.localAvatar || ""
            });
        }
        ncSheet.people = arr;
    }

    // Ten people, any ten: a random pick so it is not always the same ones.
    function showSuggested() {
        var ppl = ncSheet.people;
        var idx = [];
        for (var i = 0; i < ppl.length; i++) idx.push(i);
        var pick = [];
        var want = Math.min(10, idx.length);
        for (var n = 0; n < want; n++) {
            var j = n + Math.floor(Math.random() * (idx.length - n));
            var tmp = idx[n]; idx[n] = idx[j]; idx[j] = tmp;
            pick.push(ppl[idx[n]]);
        }
        ncSheet.searching = false;
        ncSheet.setRows(pick);
        ncSheet.statusText = (ppl.length === 0 && !ncSheet.loading) ? "You have no contacts yet." : "";
    }

    function runSearch() {
        var q = ncSheet.fold(ncSheet.queryText).replace(/^\s+|\s+$/g, "");
        if (q.length === 0) { ncSheet.showSuggested(); return; }
        if (ncSheet.loading) return;   // the answer to the pending load shows the results
        var ppl = ncSheet.people;
        var strong = [];   // name or one of its words starts with the text
        var weak = [];     // the text is somewhere inside
        for (var i = 0; i < ppl.length; i++) {
            var key = ppl[i].key;
            var at = key.indexOf(q);
            if (at < 0) continue;
            if (at === 0 || key.charAt(at - 1) === " ") strong.push(ppl[i]);
            else if (weak.length < 40) weak.push(ppl[i]);
            if (strong.length >= 40) break;
        }
        ncSheet.searching = true;
        ncSheet.setRows(strong.concat(weak).slice(0, 40));
        ncSheet.statusText = (listModel.size() === 0) ? "No one found" : "";
    }

    function setRows(rows) {
        listModel.clear();
        for (var i = 0; i < rows.length; i++) {
            var r = rows[i];
            var local = ncSheet.avatarMap[r.threadId] || r.localAvatar || "";
            listModel.append({ threadId: r.threadId, name: r.name, avatar: r.avatar, localAvatar: local });
            if (local.length === 0 && r.avatar.length > 0) zService.downloadAvatar(r.threadId, r.avatar);
        }
    }

    function applyAvatar(tid, path) {
        var m = {};
        for (var k in ncSheet.avatarMap) m[k] = ncSheet.avatarMap[k];
        if (m[tid] === path) return;
        m[tid] = path;
        ncSheet.avatarMap = m;
        for (var i = 0; i < listModel.size(); i++) {
            var d = listModel.value(i);
            if (d.threadId === tid) { d.localAvatar = path; listModel.replace(i, d); break; }
        }
    }

    function loadFailed() {
        ncSheet.loading = false;
        loadTimeout.stop();
        ncSheet.statusText = "Could not load your contacts. Close this sheet and try again.";
    }

    attachedObjects: [
        Timer {
            id: focusTimer
            interval: 350
            repeat: false
            onTriggered: searchField.requestFocus()
        },
        Timer {
            id: searchTimer
            interval: 200
            repeat: false
            onTriggered: ncSheet.runSearch()
        },
        Timer {
            // fetchFriends() can be refused silently (cooldown / 429), so a load cannot wait forever.
            id: loadTimeout
            interval: 15000
            repeat: false
            onTriggered: { if (ncSheet.loading) ncSheet.loadFailed(); }
        },
        Connections {
            target: zService
            onFriendsReady: {
                if (!ncSheet.loading) return;
                ncSheet.loading = false;
                loadTimeout.stop();
                ncSheet.statusText = "";
                ncSheet.loadPeople(friends);
                ncSheet.runSearch();
            }
            onFriendsFetchFailed: {
                if (ncSheet.loading) ncSheet.loadFailed();
            }
            onAvatarReady: {
                ncSheet.applyAvatar(threadId, localPath);
            }
        },
        ArrayDataModel { id: listModel }
    ]

    content: Page {
        titleBar: TitleBar {
            title: "New Chat"
            dismissAction: ActionItem {
                title: "Cancel"
                onTriggered: { ncSheet.closeSheet(); }
            }
        }

        Container {
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment: VerticalAlignment.Fill
            background: ncSheet.isDark ? Color.create("#1a1a1a") : Color.White

            Container {
                horizontalAlignment: HorizontalAlignment.Fill
                topPadding: ui.du(1); bottomPadding: ui.du(1)
                leftPadding: ui.du(1.2); rightPadding: ui.du(1.2)
                TextField {
                    id: searchField
                    hintText: "Search name..."
                    horizontalAlignment: HorizontalAlignment.Fill
                    input {
                        flags: TextInputFlag.AutoCapitalizationOff | TextInputFlag.AutoCorrectionOff
                               | TextInputFlag.SpellCheckOff | TextInputFlag.PredictionOff
                    }
                    onTextChanging: {
                        ncSheet.queryText = text;
                        searchTimer.restart();
                    }
                }
            }

            // "Suggested" header: part of the content, not the sheet's title bar.
            Container {
                visible: !ncSheet.searching && listModel.size() > 0
                horizontalAlignment: HorizontalAlignment.Fill
                background: ncSheet.isDark ? Color.create("#2a2a2a") : Color.create("#eeeeee")
                topPadding: ui.du(0.8); bottomPadding: ui.du(0.8)
                leftPadding: ui.du(1.5); rightPadding: ui.du(1.5)
                Label {
                    text: "Suggested"
                    textStyle {
                        base: SystemDefaults.TextStyles.SmallText
                        fontWeight: FontWeight.Bold
                        color: ncSheet.isDark ? Color.create("#b0b0b0") : Color.create("#555555")
                    }
                }
            }

            Label {
                visible: ncSheet.statusText.length > 0
                text: ncSheet.statusText
                multiline: true
                horizontalAlignment: HorizontalAlignment.Center
                topMargin: ui.du(4)
                textStyle {
                    textAlign: TextAlign.Center
                    color: ncSheet.isDark ? Color.create("#b0b0b0") : Color.create("#666666")
                }
            }

            ListView {
                id: personList
                layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                horizontalAlignment: HorizontalAlignment.Fill
                dataModel: listModel
                property bool isDarkProxy: ncSheet.isDark

                listItemComponents: [
                    ListItemComponent {
                        CustomListItem {
                            id: rowRoot
                            dividerVisible: true
                            Container {
                                background: rowRoot.ListItem.view.isDarkProxy ? Color.create("#1a1a1a") : Color.White
                                layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                                horizontalAlignment: HorizontalAlignment.Fill
                                verticalAlignment: VerticalAlignment.Center
                                topPadding: ui.du(1); bottomPadding: ui.du(1)
                                leftPadding: ui.du(1.2); rightPadding: ui.du(1.2)

                                ImageView {
                                    preferredWidth: ui.du(8)
                                    preferredHeight: ui.du(8)
                                    minWidth: ui.du(8)
                                    verticalAlignment: VerticalAlignment.Center
                                    scalingMethod: ScalingMethod.AspectFill
                                    imageSource: ListItemData.localAvatar ? ListItemData.localAvatar : "asset:///images/ChatsTab/default_avt.png"
                                }
                                Label {
                                    layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                                    verticalAlignment: VerticalAlignment.Center
                                    leftMargin: ui.du(1.2)
                                    text: ListItemData.name || "Unknown"
                                    textStyle {
                                        color: rowRoot.ListItem.view.isDarkProxy ? Color.White : Color.Black
                                        fontSize: FontSize.Medium
                                    }
                                }
                            }
                        }
                    }
                ]

                onTriggered: {
                    var item = dataModel.data(indexPath);
                    if (!item) return;
                    ncSheet.chatRequested(item.threadId || "", item.name || "", item.avatar || "", item.localAvatar || "");
                }
            }
        }
    }
}
