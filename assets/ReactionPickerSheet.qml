import bb.cascades 1.4
import bb.system 1.2
import QtQuick 1.0

// Backed by bb::system::SystemListDialog. The root is a QtObject because the dialog's default
// property is "buttons", so it can't host a Timer or other children. Rows are text-only
// (appendItem() has no icon parameter). Needs "import bb.system 1.3" for dismissOnSelection;
// without it the dialog doesn't auto-close on tap.
QtObject {
    id: root

    property string pendingMsgId: ""
    property string pendingCliMsgId: ""
    property int    pendingMsgType: 0

    // Order must match the appendItem() calls in openFor(): index N is what selectedIndices[0] reports.
    property variant reactionIcons: ["like", "heart", "haha", "wow", "cry", "angry"]

    signal reacted(string msgId, string cliMsgId, int msgType, string icon)

    // existingIcon only pre-highlights a row (3rd arg of appendItem()); it doesn't affect what a tap reports.
    function openFor(msgId, cliMsgId, msgType, existingIcon) {
        root.pendingMsgId    = msgId || "";
        root.pendingCliMsgId = cliMsgId || "";
        root.pendingMsgType  = msgType || 0;

        dlg.clearList();
        var cur = existingIcon || "";
        dlg.appendItem(qsTr("Like"),      true, cur === "like");
        dlg.appendItem(qsTr("Heart"),     true, cur === "heart");
        dlg.appendItem(qsTr("Haha"),      true, cur === "haha");
        dlg.appendItem(qsTr("Surprised"), true, cur === "wow");
        dlg.appendItem(qsTr("Sad"),       true, cur === "cry");
        dlg.appendItem(qsTr("Angry"),     true, cur === "angry");

        dlg.show();
    }

    property SystemListDialog dlgObj: SystemListDialog {
        id: dlg

        title: qsTr("Reactions")
        selectionMode: ListSelectionMode.Single
        dismissOnSelection: true
        confirmButton.label: ""

        onFinished: {
            if (dlg.selectedIndices && dlg.selectedIndices.length > 0) {
                var idx = dlg.selectedIndices[0];
                if (idx >= 0 && idx < root.reactionIcons.length) {
                    var iconId = root.reactionIcons[idx];
                    root.reacted(root.pendingMsgId, root.pendingCliMsgId,
                                 root.pendingMsgType, iconId);
                }
            }
        }
    }
}
