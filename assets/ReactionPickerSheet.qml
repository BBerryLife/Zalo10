import bb.cascades 1.4
import bb.system 1.2
import QtQuick 1.0

// Reaction picker backed by the real bb::system::SystemListDialog
//
// Root is QtObject, not SystemListDialog directly — SystemListDialog is a plain
// QObject (no attachedObjects) with its default property bound to "buttons", so
// it can't host a Timer or other children directly. QtObject wraps it instead;
// openFor()/reacted are forwarded at this level so ChatView.qml's existing wiring
// still works unchanged.
//
// Limitation: appendItem() only takes (text[, enabled[, selected]]) — no icon
// param exists on this class, so rows are text-only ("Like", "Heart", "Haha", ...),
// not the emoji icons in the reference design.
//
// Needs "import bb.system 1.3" for dismissOnSelection to be available on this
// target — 1.0 and 1.1 don't expose it, and without it the dialog won't auto-close
// on tap (selectedIndices only populates after finished() fires, so a polling
// workaround can't catch the selection in time).
QtObject {
    id: root

    property string pendingMsgId: ""
    property string pendingCliMsgId: ""
    property int    pendingMsgType: 0

    // Order must match the appendItem() calls in openFor() below — index N here
    // is what selectedIndices[0] reports for the Nth row
    property variant reactionIcons: ["like", "heart", "haha", "wow", "cry", "angry"]

    // icon is one of reactionIcons above. ChatView.qml's doSendReaction() treats
    // tapping the same icon again as "remove", so this just reports which icon was tapped
    signal reacted(string msgId, string cliMsgId, int msgType, string icon)

    // existingIcon just pre-highlights a row via appendItem()'s 3rd arg —
    // purely visual, doesn't affect what a subsequent tap reports
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

        // finished() fires when the dialog closes, either from a row tap
        // (dismissOnSelection) or a Cancel tap. selectedIndices is only
        // non-empty in the tap case, which is how we tell the two apart.
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
