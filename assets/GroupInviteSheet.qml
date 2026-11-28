import bb.cascades 1.4
import bb.cascades.pickers 1.0
import bb.system 1.0
import QtQuick 1.0

// "Invite More" of ChatView.
//  mode "create": opened from a one-to-one chat. Header like My Profile: group picture (default_group until a
//                 photo is picked) and a text box for the group name. The person of the chat is already ticked;
//                 the sheet creates the group with everyone ticked.
//  mode "invite": opened from a group. Same list, ticked people are added to the group. The group picture can
//                 be changed (tap it) and the name changed with the checkbox under the header.
// The people list is NewChatSheet's: ten suggestions, search as you type, but with a checkbox per row.
Sheet {
    id: giSheet

    property bool    isDark: false
    property string  sheetMode: "create"          // "create" | "invite"
    property string  groupId: ""
    property string  groupName: ""
    property string  groupAvatar: ""         // picture of the group (invite mode), may be a link
    property string  groupAvatarLocal: ""    // the same picture on disk once downloaded
    property string  seedId: ""              // create mode: the person of the chat, ticked from the start
    property string  seedName: ""
    property string  seedAvatar: ""
    property string  seedLocalAvatar: ""

    property bool    sheetShown: false
    property bool    busy: false
    property bool    loading: false
    property bool    searching: false
    property string  queryText: ""
    property string  statusText: ""
    property string  nameText: ""            // create mode: typed group name
    property string  renameText: ""          // invite mode: typed new name
    property bool    renameChecked: false
    property string  pendingAvatar: ""       // picked picture, applied when the sheet is confirmed
    property bool    wantCamera: false
    property variant people: []              // every friend: { threadId, name, key, avatar, localAvatar }
    property variant avatarMap: ({})         // threadId -> picture file already on disk
    property variant selectedMap: ({})       // threadId -> name of every ticked person
    property int     selectedCount: 0

    // invite mode: the calls run one after the other
    property variant steps: []
    property int     stepPos: 0
    property string  failText: ""
    property string  okText: ""
    property int     askedCount: 0
    property string  createdId: ""
    property string  createdName: ""

    property string  foldFrom: "\u00e0\u00e1\u1ea1\u1ea3\u00e3\u00e2\u1ea7\u1ea5\u1ead\u1ea9\u1eab\u0103\u1eb1\u1eaf\u1eb7\u1eb3\u1eb5\u00e8\u00e9\u1eb9\u1ebb\u1ebd\u00ea\u1ec1\u1ebf\u1ec7\u1ec3\u1ec5\u00ec\u00ed\u1ecb\u1ec9\u0129\u00f2\u00f3\u1ecd\u1ecf\u00f5\u00f4\u1ed3\u1ed1\u1ed9\u1ed5\u1ed7\u01a1\u1edd\u1edb\u1ee3\u1edf\u1ee1\u00f9\u00fa\u1ee5\u1ee7\u0169\u01b0\u1eeb\u1ee9\u1ef1\u1eed\u1eef\u1ef3\u00fd\u1ef5\u1ef7\u1ef9\u0111"
    property string  foldTo:   "aaaaaaaaaaaaaaaaaeeeeeeeeeeeiiiiiooooooooooooooooouuuuuuuuuuuyyyyyd"

    signal groupCreated(string newGroupId, string newName, string newAvatar)
    signal groupRenamed(string changedGroupId, string newName)
    signal groupAvatarUpdated(string changedGroupId, string newAvatar)

    function fold(s) {
        var t = (s || "").toLowerCase();
        var out = "";
        for (var i = 0; i < t.length; i++) {
            var c = t.charAt(i);
            var code = t.charCodeAt(i);
            if (code >= 0x300 && code <= 0x36f) continue;   // combining accent
            var k = giSheet.foldFrom.indexOf(c);
            out += (k >= 0) ? giSheet.foldTo.charAt(k) : c;
        }
        return out;
    }

    function trimmed(s) { return (s || "").replace(/^\s+|\s+$/g, ""); }

    // ---------- opening ----------

    function resetState() {
        giSheet.busy = false;
        giSheet.nameText = "";
        giSheet.renameText = "";
        giSheet.renameChecked = false;
        renameBox.checked = false;
        giSheet.pendingAvatar = "";
        giSheet.wantCamera = false;
        giSheet.selectedMap = ({});
        giSheet.selectedCount = 0;
        giSheet.steps = [];
        giSheet.stepPos = 0;
        giSheet.failText = "";
        giSheet.okText = "";
        giSheet.createdId = "";
        giSheet.queryText = "";
        giSheet.searching = false;
        giSheet.statusText = "";
        searchField.text = "";
        nameField.text = "";
        renameField.text = "";
        listModel.clear();
    }

    function startList() {
        var cached = zService.cachedFriends();
        if (cached.length > 0) {
            giSheet.loading = false;
            giSheet.loadPeople(cached);
            giSheet.showSuggested();
        } else {
            giSheet.people = [];
            giSheet.loading = true;
            giSheet.statusText = "Loading contacts...";
            loadTimeout.restart();
            zService.fetchFriends();
        }
        giSheet.open();
    }

    function openCreate(personId, personName, personAvatar, personLocalAvatar) {
        giSheet.resetState();
        giSheet.sheetMode = "create";
        giSheet.groupId = "";
        giSheet.seedId = personId || "";
        giSheet.seedName = personName || "";
        giSheet.seedAvatar = personAvatar || "";
        giSheet.seedLocalAvatar = personLocalAvatar || "";
        if (giSheet.seedId.length > 0) {
            var m = {};
            m[giSheet.seedId] = giSheet.seedName.length > 0 ? giSheet.seedName : "Chat";
            giSheet.selectedMap = m;
            giSheet.selectedCount = 1;
        }
        giSheet.sheetShown = true;
        giSheet.startList();
        giSheet.updateAccept();
    }

    function openInvite(gid, gname, gavatar) {
        giSheet.resetState();
        giSheet.sheetMode = "invite";
        giSheet.groupId = gid || "";
        giSheet.groupName = gname || "";
        giSheet.renameText = giSheet.groupName;
        renameField.text = giSheet.groupName;
        giSheet.seedId = "";
        giSheet.groupAvatar = gavatar || "";
        giSheet.groupAvatarLocal = "";
        if (giSheet.groupAvatar.length > 0 && giSheet.groupAvatar.indexOf("http") === 0) {
            zService.downloadAvatar("grp_" + giSheet.groupId, giSheet.groupAvatar);
        } else if (giSheet.groupAvatar.length > 0 && giSheet.groupAvatar.indexOf("asset:") !== 0) {
            giSheet.groupAvatarLocal = giSheet.groupAvatar;
        }
        giSheet.sheetShown = true;
        giSheet.startList();
        giSheet.updateAccept();
    }

    function closeSheet() {
        giSheet.loading = false;
        loadTimeout.stop();
        giSheet.close();
    }

    // ---------- people list (NewChatSheet) ----------

    function loadPeople(list) {
        var arr = [];
        for (var i = 0; i < list.length; i++) {
            var f = list[i];
            var tid = f.threadId || f.uid || "";
            var name = f.name || f.displayName || "";
            if (tid.length === 0 || name.length === 0) continue;
            arr.push({
                threadId: tid, name: name, key: giSheet.fold(name),
                avatar: f.avatar || "", localAvatar: f.localAvatar || ""
            });
        }
        giSheet.people = arr;
    }

    // Ten people, any ten. In create mode the person of the chat comes first.
    function showSuggested() {
        var ppl = giSheet.people;
        var idx = [];
        for (var i = 0; i < ppl.length; i++) {
            if (ppl[i].threadId === giSheet.seedId) continue;
            idx.push(i);
        }
        var pick = [];
        if (giSheet.seedId.length > 0) {
            pick.push({ threadId: giSheet.seedId, name: giSheet.seedName.length > 0 ? giSheet.seedName : "Chat",
                        key: "", avatar: giSheet.seedAvatar, localAvatar: giSheet.seedLocalAvatar });
        }
        var want = Math.min(10, idx.length);
        for (var n = 0; n < want; n++) {
            var j = n + Math.floor(Math.random() * (idx.length - n));
            var tmp = idx[n]; idx[n] = idx[j]; idx[j] = tmp;
            pick.push(ppl[idx[n]]);
        }
        giSheet.searching = false;
        giSheet.setRows(pick);
        giSheet.statusText = (ppl.length === 0 && !giSheet.loading && pick.length === 0) ? "You have no contacts yet." : "";
    }

    function runSearch() {
        var q = giSheet.fold(giSheet.queryText).replace(/^\s+|\s+$/g, "");
        if (q.length === 0) { giSheet.showSuggested(); return; }
        if (giSheet.loading) return;
        var ppl = giSheet.people;
        var strong = [];
        var weak = [];
        for (var i = 0; i < ppl.length; i++) {
            var key = ppl[i].key;
            var at = key.indexOf(q);
            if (at < 0) continue;
            if (at === 0 || key.charAt(at - 1) === " ") strong.push(ppl[i]);
            else if (weak.length < 40) weak.push(ppl[i]);
            if (strong.length >= 40) break;
        }
        giSheet.searching = true;
        giSheet.setRows(strong.concat(weak).slice(0, 40));
        giSheet.statusText = (listModel.size() === 0) ? "No one found" : "";
    }

    function setRows(rows) {
        listModel.clear();
        for (var i = 0; i < rows.length; i++) {
            var r = rows[i];
            var local = giSheet.avatarMap[r.threadId] || r.localAvatar || "";
            listModel.append({
                threadId: r.threadId, name: r.name, avatar: r.avatar, localAvatar: local,
                checked: giSheet.selectedMap[r.threadId] !== undefined
            });
            if (local.length === 0 && r.avatar.length > 0) zService.downloadAvatar(r.threadId, r.avatar);
        }
    }

    function applyAvatar(tid, path) {
        var m = {};
        for (var k in giSheet.avatarMap) m[k] = giSheet.avatarMap[k];
        if (m[tid] === path) return;
        m[tid] = path;
        giSheet.avatarMap = m;
        for (var i = 0; i < listModel.size(); i++) {
            var d = listModel.value(i);
            if (d.threadId === tid) { d.localAvatar = path; listModel.replace(i, d); break; }
        }
    }

    function loadFailed() {
        giSheet.loading = false;
        loadTimeout.stop();
        giSheet.statusText = "Could not load your contacts. Close this sheet and try again.";
    }

    function toggleRow(idx) {
        var item = listModel.value(idx);
        if (!item || giSheet.busy) return;
        item.checked = !item.checked;
        listModel.replace(idx, item);
        var m = {};
        var n = 0;
        for (var k in giSheet.selectedMap) { m[k] = giSheet.selectedMap[k]; n++; }
        if (item.checked) {
            if (m[item.threadId] === undefined) n++;
            m[item.threadId] = item.name;
        } else if (m[item.threadId] !== undefined) {
            delete m[item.threadId];
            n--;
        }
        giSheet.selectedMap = m;
        giSheet.selectedCount = n;
        giSheet.updateAccept();
    }

    function selectedIds() {
        var ids = [];
        for (var k in giSheet.selectedMap) ids.push(k);
        return ids;
    }

    // "Alice, Bob, Carol" when the group got no name.
    function autoName() {
        var names = [];
        for (var k in giSheet.selectedMap) {
            names.push(giSheet.selectedMap[k]);
            if (names.length >= 3) break;
        }
        return names.join(", ");
    }

    function updateAccept() {
        var can = false;
        if (!giSheet.busy) {
            if (giSheet.sheetMode === "create") {
                can = giSheet.selectedCount >= 1;
            } else {
                can = giSheet.selectedCount >= 1
                   || (giSheet.renameChecked && giSheet.trimmed(giSheet.renameText).length > 0)
                   || giSheet.pendingAvatar.length > 0;
            }
        }
        confirmAction.enabled = can;
    }

    // ---------- picture ----------

    function usePhoto(path) {
        if (!path || path.length === 0) return;
        giSheet.pendingAvatar = path;
        giSheet.updateAccept();
    }

    // ---------- confirm ----------

    function confirm() {
        if (giSheet.busy) return;
        if (giSheet.sheetMode === "create") {
            if (giSheet.selectedCount < 1) return;
            var nm = giSheet.trimmed(giSheet.nameText);
            if (nm.length === 0) nm = giSheet.autoName();
            giSheet.createdName = nm;
            giSheet.createdId = "";
            giSheet.busy = true;
            giSheet.updateAccept();
            zService.createGroup(nm, giSheet.selectedIds());
        } else {
            var st = [];
            if (giSheet.selectedCount >= 1) st.push("add");
            if (giSheet.renameChecked && giSheet.trimmed(giSheet.renameText).length > 0) st.push("rename");
            if (giSheet.pendingAvatar.length > 0) st.push("avatar");
            if (st.length === 0) return;
            giSheet.steps = st;
            giSheet.stepPos = 0;
            giSheet.failText = "";
            giSheet.okText = "";
            giSheet.busy = true;
            giSheet.updateAccept();
            giSheet.runStep();
        }
    }

    function runStep() {
        if (giSheet.stepPos >= giSheet.steps.length) { giSheet.finishSteps(); return; }
        var s = giSheet.steps[giSheet.stepPos];
        if (s === "add") {
            giSheet.askedCount = giSheet.selectedCount;
            zService.addGroupMembers(giSheet.groupId, giSheet.selectedIds());
        } else if (s === "rename") {
            zService.changeGroupName(giSheet.groupId, giSheet.trimmed(giSheet.renameText));
        } else {
            zService.changeGroupAvatar(giSheet.groupId, giSheet.pendingAvatar);
        }
    }

    // A step ended. Steps that worked are cleared, so pressing the button again only repeats the failed ones.
    function stepDone(what, ok, errText, note) {
        if (ok) {
            if (what === "add") {
                for (var i = 0; i < listModel.size(); i++) {
                    var d = listModel.value(i);
                    if (d.checked) { d.checked = false; listModel.replace(i, d); }
                }
                giSheet.selectedMap = ({});
                giSheet.selectedCount = 0;
                giSheet.okText += (giSheet.okText.length > 0 ? ", " : "") + "members added" + (note.length > 0 ? " (" + note + ")" : "");
            } else if (what === "rename") {
                var newName = giSheet.trimmed(giSheet.renameText);
                giSheet.groupName = newName;
                giSheet.renameChecked = false;
                renameBox.checked = false;
                giSheet.groupRenamed(giSheet.groupId, newName);
                giSheet.okText += (giSheet.okText.length > 0 ? ", " : "") + "name changed";
            } else {
                var shown = giSheet.pendingAvatar;
                giSheet.groupAvatarLocal = shown;
                giSheet.pendingAvatar = "";
                giSheet.groupAvatarUpdated(giSheet.groupId, shown);
                giSheet.okText += (giSheet.okText.length > 0 ? ", " : "") + "picture changed";
            }
        } else {
            var label = (what === "add") ? "Add members" : ((what === "rename") ? "Rename" : "Picture");
            giSheet.failText += (giSheet.failText.length > 0 ? "\n" : "") + label + ": " + errText;
        }
        giSheet.stepPos = giSheet.stepPos + 1;
        giSheet.runStep();
    }

    function finishSteps() {
        giSheet.busy = false;
        giSheet.updateAccept();
        if (giSheet.failText.length > 0) {
            resultToast.body = giSheet.failText;
            resultToast.show();
            return;
        }
        var t = giSheet.okText;
        if (t.length > 0) t = t.charAt(0).toUpperCase() + t.substring(1);
        resultToast.body = t;
        resultToast.show();
        giSheet.closeSheet();
    }

    function finishCreate(avatarOk, errText) {
        giSheet.busy = false;
        var av = (avatarOk && giSheet.pendingAvatar.length > 0) ? giSheet.pendingAvatar : "";
        resultToast.body = avatarOk ? qsTr("Group created") : qsTr("Group created, but the picture could not be set: ") + errText;
        resultToast.show();
        var gid = giSheet.createdId;
        var gname = giSheet.createdName;
        giSheet.closeSheet();
        giSheet.groupCreated(gid, gname, av);
    }

    onClosed: {
        giSheet.sheetShown = false;
        giSheet.busy = false;
        giSheet.wantCamera = false;
        loadTimeout.stop();
    }

    attachedObjects: [
        Timer {
            id: searchTimer
            interval: 200
            repeat: false
            onTriggered: giSheet.runSearch()
        },
        Timer {
            // fetchFriends() can be refused silently (cooldown / 429), so a load cannot wait forever.
            id: loadTimeout
            interval: 15000
            repeat: false
            onTriggered: { if (giSheet.loading) giSheet.loadFailed(); }
        },
        SystemToast { id: resultToast },
        SystemDialog {
            id: avatarDialog
            title: qsTr("Group Picture")
            body: qsTr("Where should the picture come from?")
            confirmButton.label: qsTr("Choose Photo")
            confirmButton.enabled: true
            cancelButton.label: qsTr("Cancel")
            cancelButton.enabled: true
            buttons: [ SystemUiButton { label: qsTr("Open Camera"); enabled: true } ]
            onFinished: {
                if (result === SystemUiResult.ConfirmButtonSelection) {
                    photoPicker.open();
                } else if (result === SystemUiResult.ButtonSelection) {
                    giSheet.wantCamera = true;
                    app.captureCameraPhoto();
                }
            }
        },
        FilePicker {
            id: photoPicker
            type: FileType.Picture
            mode: FilePickerMode.Picker
            title: qsTr("Select Picture")
            onFileSelected: giSheet.usePhoto(selectedFiles[0])
        },
        Connections {
            target: app
            onCameraPhotoCaptured: {
                if (!giSheet.sheetShown || !giSheet.wantCamera) return;
                giSheet.wantCamera = false;
                giSheet.usePhoto(path);
            }
        },
        Connections {
            target: zService
            onFriendsReady: {
                if (!giSheet.sheetShown || !giSheet.loading) return;
                giSheet.loading = false;
                loadTimeout.stop();
                giSheet.statusText = "";
                giSheet.loadPeople(friends);
                giSheet.runSearch();
            }
            onFriendsFetchFailed: {
                if (giSheet.sheetShown && giSheet.loading) giSheet.loadFailed();
            }
            onAvatarReady: {
                if (!giSheet.sheetShown) return;
                if (threadId === "grp_" + giSheet.groupId) giSheet.groupAvatarLocal = localPath;
                else giSheet.applyAvatar(threadId, localPath);
            }
            onCreateGroupDone: {
                if (!giSheet.sheetShown || !giSheet.busy || giSheet.sheetMode !== "create") return;
                if (!ok) {
                    giSheet.busy = false;
                    giSheet.updateAccept();
                    resultToast.body = qsTr("Could not create the group") + (error.length > 0 ? ": " + error : "");
                    resultToast.show();
                    return;
                }
                giSheet.createdId = groupId;
                if (giSheet.pendingAvatar.length > 0) zService.changeGroupAvatar(groupId, giSheet.pendingAvatar);
                else giSheet.finishCreate(true, "");
            }
            onAddGroupMembersDone: {
                if (!giSheet.sheetShown || !giSheet.busy || giSheet.sheetMode !== "invite") return;
                if (giSheet.steps[giSheet.stepPos] !== "add" || groupId !== giSheet.groupId) return;
                if (!ok) { giSheet.stepDone("add", false, error, ""); return; }
                if (failedCount >= giSheet.askedCount) {
                    giSheet.stepDone("add", false, "they could not be added (already members, or their privacy settings)", "");
                    return;
                }
                giSheet.stepDone("add", true, "", failedCount > 0 ? (failedCount + " could not be added") : "");
            }
            onChangeGroupNameDone: {
                if (!giSheet.sheetShown || !giSheet.busy || giSheet.sheetMode !== "invite") return;
                if (giSheet.steps[giSheet.stepPos] !== "rename" || groupId !== giSheet.groupId) return;
                giSheet.stepDone("rename", ok, error, "");
            }
            onChangeGroupAvatarDone: {
                if (!giSheet.sheetShown || !giSheet.busy) return;
                if (giSheet.sheetMode === "create") {
                    if (groupId !== giSheet.createdId) return;
                    giSheet.finishCreate(ok, error);
                } else {
                    if (giSheet.steps[giSheet.stepPos] !== "avatar" || groupId !== giSheet.groupId) return;
                    giSheet.stepDone("avatar", ok, error, "");
                }
            }
        },
        ArrayDataModel { id: listModel }
    ]

    content: Page {
        titleBar: TitleBar {
            title: giSheet.sheetMode === "create" ? qsTr("Create Group") : qsTr("Invite More")
            dismissAction: ActionItem {
                title: qsTr("Cancel")
                onTriggered: { giSheet.closeSheet(); }
            }
            acceptAction: ActionItem {
                id: confirmAction
                title: giSheet.sheetMode === "create" ? qsTr("Create") : qsTr("Add")
                enabled: false
                onTriggered: { giSheet.confirm(); }
            }
        }

        Container {
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment: VerticalAlignment.Fill
            background: giSheet.isDark ? Color.create("#1a1a1a") : Color.White

            // Header laid out like the My Profile view, but without a cover: the group picture sits flush in
            // the top-left corner, the name (a text box when creating) beside it, all on the plain page colour.
            Container {
                preferredHeight: ui.du(26)
                horizontalAlignment: HorizontalAlignment.Fill
                layout: StackLayout { orientation: LayoutOrientation.LeftToRight }

                // Group picture: tap to pick one. The translucent strip carries the hint.
                Container {
                    preferredWidth: ui.du(26)
                    preferredHeight: ui.du(26)
                    minWidth: ui.du(26)
                    background: Color.create("#2575fc")
                    layout: DockLayout {}
                    gestureHandlers: TapHandler {
                        onTapped: { if (!giSheet.busy) avatarDialog.show(); }
                    }

                    ImageView {
                        imageSource: giSheet.pendingAvatar.length > 0 ? giSheet.pendingAvatar
                                   : ((giSheet.sheetMode === "invite" && giSheet.groupAvatarLocal.length > 0)
                                      ? giSheet.groupAvatarLocal
                                      : "asset:///images/GroupsTab/default_group.png")
                        horizontalAlignment: HorizontalAlignment.Fill
                        verticalAlignment: VerticalAlignment.Fill
                        scalingMethod: ScalingMethod.AspectFill
                    }
                    Container {
                        verticalAlignment: VerticalAlignment.Bottom
                        horizontalAlignment: HorizontalAlignment.Fill
                        preferredHeight: ui.du(4.5)
                        background: Color.create("#99000000")
                        layout: DockLayout {}
                        Label {
                            text: qsTr("Change")
                            horizontalAlignment: HorizontalAlignment.Center
                            verticalAlignment: VerticalAlignment.Center
                            textStyle { base: SystemDefaults.TextStyles.SmallText; color: Color.White }
                        }
                    }
                    ActivityIndicator {
                        running: giSheet.busy
                        visible: giSheet.busy
                        preferredWidth: ui.du(9)
                        preferredHeight: ui.du(9)
                        horizontalAlignment: HorizontalAlignment.Center
                        verticalAlignment: VerticalAlignment.Center
                    }
                }

                // Name from the very top of the header, like the name in My Profile. Create: a text box.
                Container {
                    layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                    verticalAlignment: VerticalAlignment.Fill
                    leftPadding: ui.du(2)
                    rightPadding: ui.du(2)
                    topPadding: ui.du(2)
                    TextField {
                        id: nameField
                        visible: giSheet.sheetMode === "create"
                        hintText: qsTr("Group name")
                        horizontalAlignment: HorizontalAlignment.Fill
                        maximumLength: 100
                        input {
                            flags: TextInputFlag.AutoCorrectionOff | TextInputFlag.SpellCheckOff | TextInputFlag.PredictionOff
                        }
                        onTextChanging: giSheet.nameText = text
                    }
                    Label {
                        visible: giSheet.sheetMode === "invite"
                        text: giSheet.groupName.length > 0 ? giSheet.groupName : qsTr("Group")
                        multiline: false
                        textStyle { base: SystemDefaults.TextStyles.BigText; color: giSheet.isDark ? Color.White : Color.Black }
                    }
                    Label {
                        text: giSheet.selectedCount > 0
                            ? (giSheet.selectedCount + (giSheet.selectedCount === 1 ? " person selected" : " people selected"))
                            : "Pick people below"
                        textStyle {
                            base: SystemDefaults.TextStyles.BodyText
                            color: giSheet.isDark ? Color.create("#b0b0b0") : Color.create("#7a7a7a")
                        }
                    }
                }
            }

            // invite mode: change the group name too
            Container {
                visible: giSheet.sheetMode === "invite"
                horizontalAlignment: HorizontalAlignment.Fill
                topPadding: ui.du(0.8); bottomPadding: ui.du(0.8)
                leftPadding: ui.du(1.5); rightPadding: ui.du(1.5)
                CheckBox {
                    id: renameBox
                    text: qsTr("Change group name")
                    onCheckedChanged: {
                        giSheet.renameChecked = checked;
                        giSheet.updateAccept();
                    }
                }
                TextField {
                    id: renameField
                    visible: giSheet.renameChecked
                    hintText: qsTr("New group name")
                    horizontalAlignment: HorizontalAlignment.Fill
                    maximumLength: 100
                    input {
                        flags: TextInputFlag.AutoCorrectionOff | TextInputFlag.SpellCheckOff | TextInputFlag.PredictionOff
                    }
                    onTextChanging: {
                        giSheet.renameText = text;
                        giSheet.updateAccept();
                    }
                }
            }

            Container {
                horizontalAlignment: HorizontalAlignment.Fill
                topPadding: ui.du(1); bottomPadding: ui.du(1)
                leftPadding: ui.du(1.2); rightPadding: ui.du(1.2)
                TextField {
                    id: searchField
                    hintText: qsTr("Search name...")
                    horizontalAlignment: HorizontalAlignment.Fill
                    input {
                        flags: TextInputFlag.AutoCapitalizationOff | TextInputFlag.AutoCorrectionOff
                               | TextInputFlag.SpellCheckOff | TextInputFlag.PredictionOff
                    }
                    onTextChanging: {
                        giSheet.queryText = text;
                        searchTimer.restart();
                    }
                }
            }

            Container {
                visible: !giSheet.searching && listModel.size() > 0
                horizontalAlignment: HorizontalAlignment.Fill
                background: giSheet.isDark ? Color.create("#2a2a2a") : Color.create("#eeeeee")
                topPadding: ui.du(0.8); bottomPadding: ui.du(0.8)
                leftPadding: ui.du(1.5); rightPadding: ui.du(1.5)
                Label {
                    text: qsTr("Suggested")
                    textStyle {
                        base: SystemDefaults.TextStyles.SmallText
                        fontWeight: FontWeight.Bold
                        color: giSheet.isDark ? Color.create("#b0b0b0") : Color.create("#555555")
                    }
                }
            }

            Label {
                visible: giSheet.statusText.length > 0
                text: giSheet.statusText
                multiline: true
                horizontalAlignment: HorizontalAlignment.Center
                topMargin: ui.du(4)
                textStyle {
                    textAlign: TextAlign.Center
                    color: giSheet.isDark ? Color.create("#b0b0b0") : Color.create("#666666")
                }
            }

            ListView {
                id: personList
                layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                horizontalAlignment: HorizontalAlignment.Fill
                dataModel: listModel
                property bool isDarkProxy: giSheet.isDark

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
                                    text: ListItemData.name || qsTr("Unknown")
                                    textStyle {
                                        color: rowRoot.ListItem.view.isDarkProxy ? Color.White : Color.Black
                                        fontSize: FontSize.Medium
                                    }
                                }
                                CheckBox {
                                    verticalAlignment: VerticalAlignment.Center
                                    checked: ListItemData.checked === true
                                    // enabled must stay true or Cascades greys out the checkbox. PassThrough lets taps reach the
                                    // ListItem's onTriggered; otherwise the checkbox flips but the selection never updates.
                                    touchPropagationMode: TouchPropagationMode.PassThrough
                                }
                            }
                        }
                    }
                ]

                onTriggered: { giSheet.toggleRow(indexPath[0]); }
            }
        }
    }
}
