import bb.cascades 1.4
import bb.system 1.2
import QtQuick 1.0

// ReactionPickerSheet — backed by the REAL bb::system::SystemListDialog
// (confirmed against the actual BB10 NDK header for this class, not
// guessed/reverse-engineered from web docs).
//
// STRUCTURE NOTE — why the root here is QtObject, not SystemListDialog
// directly: on-device testing showed "Cannot assign to non-existent
// property attachedObjects" when a Timer was declared inside
// attachedObjects: [...] on a SystemListDialog root, back when this file
// used a Timer-polling workaround (see history below for why that
// workaround was replaced). Confirmed from the real header:
// "class BB_SYSTEM_EXPORT SystemListDialog : public QObject" — it's a
// plain QObject, not a bb::cascades::Control, so it never had an
// attachedObjects property (that's Control's own property; nothing here
// was misconfigured, it simply doesn't exist on this class). The header
// also confirms SystemListDialog's one Q_CLASSINFO("DefaultProperty",
// "buttons") — meaning an unnamed child inside SystemListDialog {...} is
// interpreted as a SystemUiButton, not a generic container for other
// QObjects either. QtObject (a plain, non-visual QML type with no such
// restriction) as the file's root instead lets SystemListDialog be
// declared as an ordinary named-property child of it — openFor()/reacted
// are then forwarded at this QtObject level so ChatView.qml's existing
// reactionPickerSheet.openFor(...) / onReacted wiring keeps working
// completely unchanged. Kept even though the Timer that originally forced
// this structure is gone (see below), since restructuring back to a bare
// SystemListDialog root now isn't worth the churn.
//
// IMPORTANT LIMITATION, confirmed from the real header: SystemListDialog's
// appendItem() takes only (text[, enabled[, selected]]) — there is no
// icon/imageSource parameter anywhere on this class. The reference
// screenshot's per-row emoji CANNOT be reproduced with this control; this
// dialog shows text-only rows ("Like", "Heart", "Haha", ...).
//
// HISTORY on dismissOnSelection / why bb.system is now 1.3, not 1.0 or 1.1:
// dismissOnSelection carries REVISION 1 in the real header, so it was
// unavailable under "import bb.system 1.0" (confirmed from an on-device
// QML load error the first time this file used it directly). The workaround
// tried next was a Timer polling selectedIndices while the dialog stayed
// open, calling cancel() the moment a selection appeared. CONFIRMED BROKEN
// on-device: the person still had to tap Cancel before a reaction applied.
// The real header's own doc comment on selectedIndices explains why no
// polling-based workaround can ever work: dismissOnSelection's doc
// explicitly states it enables the dialog to be "automatically dismissed
// when a list item is selected" as a dedicated, opt-in behavior — implying
// selection alone does otherwise NOT dismiss the dialog, and combined with
// selectedIndices being NOTIFY-tied to finished (Q_PROPERTY(QVariantList
// selectedIndices READ selectedIndicesQML NOTIFY finished FINAL)), the
// practical effect on-device is that selectedIndices isn't observably
// populated until finished() has already fired — i.e. until the dialog has
// already closed. A Timer polling it while still open can structurally
// never see a selection in time to trigger an early cancel(); there is no
// timing window for that workaround to exploit. dismissOnSelection is the
// dedicated property for exactly this, not an optional nicety layered on
// top of some other observable signal.
//
// Next tried "import bb.system 1.1" — CONFIRMED STILL UNAVAILABLE on this
// same device/build (on-device error: "SystemListDialog.dismissOnSelection
// is not available in bb.system 1.1"). So REVISION 1 in the real header
// evidently doesn't mean "exposed starting at bb.system 1.1" on this
// target's actual QML type registration — it needs something higher still.
// Currently trying "import bb.system 1.3" next (skipping 1.2, per explicit
// direction) to find where on this target's SDK dismissOnSelection actually
// becomes available, if anywhere.
//
// If 1.3 also fails, the answer is: this BB10 target's bb.system tops out
// below wherever dismissOnSelection is actually exposed, and the only
// remaining options are the icon-only tradeoff of manually working within
// bb.system 1.0/1.1's real behavior (person must tap Cancel after picking)
// or going back to the earlier Sheet+RadioGroup custom-built dialog, which
// doesn't have this limitation because it hand-implements the "tap to
// select and close" behavior instead of relying on a system dialog's own.
QtObject {
    id: root

    property string pendingMsgId: ""
    property string pendingCliMsgId: ""
    property int    pendingMsgType: 0

    // Row order fixed to match appendItem() calls in openFor() below — index
    // N in this array is exactly what selectedIndices[0] will report for
    // the Nth appended row, since no header/separator is appended before it.
    property variant reactionIcons: ["like", "heart", "haha", "wow", "cry", "angry"]

    // (msgId, cliMsgId, msgType, icon) — icon is one of reactionIcons above.
    // ChatView.qml wires this straight to msgList.doSendReaction(), which
    // itself treats tapping the SAME icon again as "remove my reaction"
    // (toggle), so this signal only ever needs to report which icon was
    // tapped, never whether it's an add or a remove.
    signal reacted(string msgId, string cliMsgId, int msgType, string icon)

    // currentIcon (existing reaction, if any) is accepted for API
    // compatibility with earlier versions' openFor() signature, but
    // SystemListDialog's appendItem(text, enabled, selected) 3rd argument
    // only pre-highlights a row — it does not change what a subsequent tap
    // reports, so pre-selecting the existing reaction here is a pure
    // visual nicety, not required for correctness.
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

        // finished(SystemUiResult::Type) fires once the dialog closes.
        // With dismissOnSelection true, that now happens automatically the
        // instant a row is tapped — buttonSelection() would report 0 in
        // that case (per the header's own doc), which is why this reads
        // selectedIndices instead to learn which row was picked.
        // selectedIndices is only non-empty in that tap case; a manual
        // Cancel tap (still available via the dialog's own default Cancel
        // button) leaves it empty, so the length check below is what tells
        // the two apart.
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
