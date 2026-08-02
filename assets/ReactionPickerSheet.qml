import bb.cascades 1.4
import QtQuick 1.0

// ReactionPickerSheet — tap-to-react strip (Like / Love / Haha / Wow / Sad /
// Angry, same order as the reference screenshot). A plain Sheet with one row
// of 6 image buttons, not a floating popover anchored to the tapped bubble's
// screen position — Cascades (bb.cascades 1.4 / QtQuick 1.0, same version
// this whole app is built against) has no supported "popover anchored to an
// arbitrary Control's global coordinates" API this codebase could confidently
// rely on (same class of gamble ForwardPickerSheet.qml's header comment
// already declined for SystemListDialog's multi-select), so this reuses the
// same full-sheet pattern already proven reliable elsewhere in this app
// (ForwardPickerSheet/PollVotersSheet). Tapping an icon reacts AND closes
// immediately — a single tap is the whole action, no separate confirm step.
//
// 6 FIXED icon slots, not a loop/Repeater — this codebase has already
// established (see ChatView.qml/GroupBoardSheet.qml/PollVotersSheet.qml's own
// comments on their fixed 0..5 poll-option slots) that this QtQuick version
// has no Repeater item, so a small constant list is always spelled out by
// hand instead.
Sheet {
    id: reactionSheet

    property bool   isDark: false
    property string pendingMsgId: ""
    property string pendingCliMsgId: ""
    property int    pendingMsgType: 0
    // This user's existing reaction on the message being opened ("like",
    // "heart", ... or "" if none) — highlights that icon so re-opening the
    // picker shows what's already selected, same as Zalo/Messenger's own bar.
    property string currentIcon: ""

    // (msgId, cliMsgId, msgType, icon) — icon is one of the 6 ids below.
    // ChatView.qml wires this straight to msgList.doSendReaction(), which
    // itself treats tapping the SAME icon again as "remove my reaction"
    // (toggle), so this signal only ever needs to report which icon was
    // tapped, never whether it's an add or a remove.
    signal reacted(string msgId, string cliMsgId, int msgType, string icon)

    function openFor(msgId, cliMsgId, msgType, existingIcon) {
        reactionSheet.pendingMsgId    = msgId || "";
        reactionSheet.pendingCliMsgId = cliMsgId || "";
        reactionSheet.pendingMsgType  = msgType || 0;
        reactionSheet.currentIcon     = existingIcon || "";
        reactionSheet.open();
    }

    function pick(iconId) {
        reactionSheet.reacted(reactionSheet.pendingMsgId, reactionSheet.pendingCliMsgId,
                               reactionSheet.pendingMsgType, iconId);
        reactionSheet.close();
    }

    // Matches the native Cascades ActionSet card chrome this app's own
    // "Message" long-press menu already renders (see bubbleActionsMember/
    // bubbleActionsAdmin in ChatView.qml, title: "Message") — a centered
    // white card, a divider line, and a bold gray "Cancel" footer bar — just
    // with the row content replaced by the 6 tappable icons instead of a
    // vertical list of text rows.
    content: Page {
        Container {
            id: backdrop
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment: VerticalAlignment.Fill
            background: Color.Transparent
            layout: DockLayout {}
            gestureHandlers: [ TapHandler { onTapped: { reactionSheet.close(); } } ]

            Container {
                id: card
                horizontalAlignment: HorizontalAlignment.Center
                verticalAlignment: VerticalAlignment.Center
                preferredWidth: ui.du(58)
                background: reactionSheet.isDark ? Color.create("#2a2a2a") : Color.White
                // Swallow taps on the card itself so they don't fall through
                // to the backdrop's own TapHandler and close the sheet.
                gestureHandlers: [ TapHandler { onTapped: {} } ]

                // Icon row — same slot content as before, just now inside the
                // card-with-footer chrome instead of floating on its own.
                Container {
                    horizontalAlignment: HorizontalAlignment.Center
                    topPadding: ui.du(2.5); bottomPadding: ui.du(2.5)
                    leftPadding: ui.du(2); rightPadding: ui.du(2)
                    layout: StackLayout { orientation: LayoutOrientation.LeftToRight }

                    // Slot 1/6 — Like (👍)
                    Container {
                        rightMargin: ui.du(1.2)
                        background: (reactionSheet.currentIcon === "like") ? Color.create("#cfe3fa") : Color.Transparent
                        ImageView {
                            imageSource: "asset:///images/emoji/people/emoji_1f44d_64.png"
                            preferredWidth: ui.du(6.5); preferredHeight: ui.du(6.5)
                            scalingMethod: ScalingMethod.AspectFit
                            gestureHandlers: [ TapHandler { onTapped: { reactionSheet.pick("like"); } } ]
                        }
                    }
                    // Slot 2/6 — Heart / Love (❤️)
                    Container {
                        rightMargin: ui.du(1.2)
                        background: (reactionSheet.currentIcon === "heart") ? Color.create("#cfe3fa") : Color.Transparent
                        ImageView {
                            imageSource: "asset:///images/emoji/people/emoji_2764_64.png"
                            preferredWidth: ui.du(6.5); preferredHeight: ui.du(6.5)
                            scalingMethod: ScalingMethod.AspectFit
                            gestureHandlers: [ TapHandler { onTapped: { reactionSheet.pick("heart"); } } ]
                        }
                    }
                    // Slot 3/6 — Haha (😄)
                    Container {
                        rightMargin: ui.du(1.2)
                        background: (reactionSheet.currentIcon === "haha") ? Color.create("#cfe3fa") : Color.Transparent
                        ImageView {
                            imageSource: "asset:///images/emoji/people/emoji_1f604_64.png"
                            preferredWidth: ui.du(6.5); preferredHeight: ui.du(6.5)
                            scalingMethod: ScalingMethod.AspectFit
                            gestureHandlers: [ TapHandler { onTapped: { reactionSheet.pick("haha"); } } ]
                        }
                    }
                    // Slot 4/6 — Wow / surprised (😱)
                    Container {
                        rightMargin: ui.du(1.2)
                        background: (reactionSheet.currentIcon === "wow") ? Color.create("#cfe3fa") : Color.Transparent
                        ImageView {
                            imageSource: "asset:///images/emoji/people/emoji_1f631_64.png"
                            preferredWidth: ui.du(6.5); preferredHeight: ui.du(6.5)
                            scalingMethod: ScalingMethod.AspectFit
                            gestureHandlers: [ TapHandler { onTapped: { reactionSheet.pick("wow"); } } ]
                        }
                    }
                    // Slot 5/6 — Sad / cry (😭)
                    Container {
                        rightMargin: ui.du(1.2)
                        background: (reactionSheet.currentIcon === "cry") ? Color.create("#cfe3fa") : Color.Transparent
                        ImageView {
                            imageSource: "asset:///images/emoji/people/emoji_1f62d_64.png"
                            preferredWidth: ui.du(6.5); preferredHeight: ui.du(6.5)
                            scalingMethod: ScalingMethod.AspectFit
                            gestureHandlers: [ TapHandler { onTapped: { reactionSheet.pick("cry"); } } ]
                        }
                    }
                    // Slot 6/6 — Angry (😡)
                    Container {
                        background: (reactionSheet.currentIcon === "angry") ? Color.create("#cfe3fa") : Color.Transparent
                        ImageView {
                            imageSource: "asset:///images/emoji/people/emoji_1f621_64.png"
                            preferredWidth: ui.du(6.5); preferredHeight: ui.du(6.5)
                            scalingMethod: ScalingMethod.AspectFit
                            gestureHandlers: [ TapHandler { onTapped: { reactionSheet.pick("angry"); } } ]
                        }
                    }
                }

                // Divider line, same purpose as the hairline between "Select"
                // and "Cancel" in the reference screenshot.
                Container {
                    horizontalAlignment: HorizontalAlignment.Fill
                    preferredHeight: 1; minHeight: 1; maxHeight: 1
                    background: reactionSheet.isDark ? Color.create("#3a3a3a") : Color.create("#e0e0e0")
                }

                // Bold gray "Cancel" footer bar — same role/placement as the
                // reference screenshot's own Cancel bar.
                Container {
                    horizontalAlignment: HorizontalAlignment.Fill
                    verticalAlignment: VerticalAlignment.Center
                    topPadding: ui.du(1.8); bottomPadding: ui.du(1.8)
                    background: reactionSheet.isDark ? Color.create("#1f1f1f") : Color.create("#f0f0f0")
                    gestureHandlers: [ TapHandler { onTapped: { reactionSheet.close(); } } ]
                    Label {
                        text: "Cancel"
                        horizontalAlignment: HorizontalAlignment.Center
                        textStyle { fontWeight: FontWeight.Bold; fontSize: FontSize.Medium; color: reactionSheet.isDark ? Color.White : Color.create("#222222") }
                    }
                }
            }
        }
    }
}
