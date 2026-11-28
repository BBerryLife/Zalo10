import bb.cascades 1.4
import bb.system 1.0

// Choose the friend or group an unmatched phone-sync conversation belongs to, then import it
// (zService.phoneSyncAssign()).
Page {
    id: pick
    property variant sheet
    property variant matchPage
    property variant item            // one entry of zService.phoneSyncUnmatched()
    property variant allCandidates: []
    property variant chosen          // {id, name}

    titleBar: SettingsTitleBar {
        heading: (pick.item && pick.item.isGroup) ? "Choose Group" : "Choose Friend"
    }

    function filter(q) {
        var s = (q || "").toLowerCase();
        var all = pick.allCandidates;
        var out = [];
        for (var i = 0; i < all.length; i++) {
            var n = (all[i].name || "").toLowerCase();
            if (s.length === 0 || n.indexOf(s) >= 0) out.push(all[i]);
        }
        candModel.clear();
        if (out.length > 0) candModel.append(out);
        noneLabel.visible = (out.length === 0);
    }

    onItemChanged: {
        if (!pick.item) return;
        pick.allCandidates = zService.phoneSyncCandidates(pick.item.isGroup === true);
        pick.filter("");
    }

    function choose(c) {
        pick.chosen = c;
        confirmDialog.body = "Import " + pick.item.count + (pick.item.count === 1 ? " message" : " messages")
                             + " into \"" + c.name + "\"?\n\nZalo10 will remember this choice for later syncs.";
        confirmDialog.show();
    }

    function doAssign() {
        var n = zService.phoneSyncAssign(pick.item.kind, pick.item.shortId, pick.chosen.id);
        if (n < 0) {
            doneToast.body = "Could not import right now (a sync may still be running). Try again in a moment.";
            doneToast.show();
            return;
        }
        doneToast.body = "Imported " + n + (n === 1 ? " message" : " messages") + " into \"" + pick.chosen.name + "\".";
        doneToast.show();
        if (pick.matchPage) pick.matchPage.reload();
        if (pick.sheet) pick.sheet.popPage();
    }

    Container {
        horizontalAlignment: HorizontalAlignment.Fill
        verticalAlignment: VerticalAlignment.Fill

        Container {
            horizontalAlignment: HorizontalAlignment.Fill
            leftPadding: ui.du(2); rightPadding: ui.du(2); topPadding: ui.du(1.5); bottomPadding: ui.du(1.5)
            background: Color.create("#1A808080")
            Label {
                text: "Last messages in this conversation:"
                textStyle.base: SystemDefaults.TextStyles.SmallText
                textStyle.fontWeight: FontWeight.Bold
            }
            Label {
                text: pick.item ? (pick.item.preview || "") : ""
                multiline: true
                textStyle.base: SystemDefaults.TextStyles.SmallText
            }
        }

        TextField {
            id: searchField
            hintText: (pick.item && pick.item.isGroup) ? "Search groups" : "Search friends"
            horizontalAlignment: HorizontalAlignment.Fill
            topMargin: ui.du(1)
            onTextChanging: { pick.filter(text); }
        }

        Label {
            id: noneLabel
            visible: false
            horizontalAlignment: HorizontalAlignment.Center
            topMargin: ui.du(3)
            text: (pick.item && pick.item.isGroup)
                  ? "No groups found. Open the Groups tab once so Zalo10 loads your groups."
                  : "No friends found. Open the Chats tab once so Zalo10 loads your friends."
            multiline: true
            textStyle.color: Color.Gray
        }

        ListView {
            horizontalAlignment: HorizontalAlignment.Fill
            dataModel: candModel
            listItemComponents: [
                ListItemComponent {
                    type: ""
                    StandardListItem { title: ListItemData.name }
                }
            ]
            onTriggered: {
                var c = dataModel.data(indexPath);
                if (c) pick.choose(c);
            }
        }
    }

    attachedObjects: [
        ArrayDataModel { id: candModel },
        SystemDialog {
            id: confirmDialog
            title: "Import Messages"
            confirmButton.label: "Import"
            cancelButton.label: "Cancel"
            onFinished: {
                if (value === SystemUiResult.ConfirmButtonSelection) pick.doAssign();
            }
        },
        SystemToast { id: doneToast }
    ]
}
