import bb.cascades 1.4
import bb.system 1.0
import QtQuick 1.0

// BBM-style 1:1 voice call screen (layout modelled on BBM's call/AudioDisplay + ControlBox):
// full-bleed call background, avatar and caller name on top, mute + wide red End button at the
// bottom; incoming calls show Answer / Decline instead. Driven entirely by the "voice" context
// property (VoiceCallBridge). Presented automatically while voice.active.
Sheet {
    id: callSheet
    property bool showing: false
    property bool closing: false

    function sync() {
        if (voice.active && !showing && !closing) {
            showing = true;
            open();
        } else if (!voice.active && showing && !closing) {
            closing = true;
            close();
        }
    }

    onOpened: {
        voice.setForeground(true);
        if (!voice.active) { closing = true; close(); }
    }
    onClosed: {
        showing = false;
        closing = false;
        voice.setForeground(false);
        sync();                       // a new call may have started while we were closing
    }
    onCreationCompleted: sync()

    attachedObjects: [
        Connections {
            target: voice
            onChanged: callSheet.sync()
            onCallEnded: {
                // Only explain calls that ended without ever connecting (declined / busy / no answer
                // / engine failure); a normal hang-up needs no message.
                if (seconds === 0 && reason !== "local_hangup") {
                    endToast.body = (reason === "engine_error") ? "Voice calling is unavailable"
                                                                : voice.status;
                    endToast.show();
                }
            }
        },
        SystemToast {
            id: endToast
            position: SystemUiPosition.MiddleCenter
        }
    ]

    Page {
        Container {
            layout: DockLayout {}
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment: VerticalAlignment.Fill
            background: callBg.imagePaint
            attachedObjects: ImagePaintDefinition {
                id: callBg
                imageSource: "asset:///images/ChatView/call/audio_bg.png"
                repeatPattern: RepeatPattern.Fill
            }

            // Caller block (avatar + name + state), top-centre like BBM's audio display.
            Container {
                horizontalAlignment: HorizontalAlignment.Center
                verticalAlignment: VerticalAlignment.Top
                topPadding: ui.du(9)
                leftPadding: ui.du(3)
                rightPadding: ui.du(3)
                layout: StackLayout { orientation: LayoutOrientation.TopToBottom }

                Container {
                    horizontalAlignment: HorizontalAlignment.Center
                    layout: DockLayout {}
                    preferredWidth: ui.du(20)
                    preferredHeight: ui.du(20)
                    ImageView {
                        imageSource: "asset:///images/ChatView/call/audioonly_avatar_bg.png"
                        scalingMethod: ScalingMethod.AspectFit
                        horizontalAlignment: HorizontalAlignment.Fill
                        verticalAlignment: VerticalAlignment.Fill
                    }
                    ImageView {
                        imageSource: "asset:///images/ChatView/call/avatar_default_light.png"
                        scalingMethod: ScalingMethod.AspectFit
                        horizontalAlignment: HorizontalAlignment.Center
                        verticalAlignment: VerticalAlignment.Center
                        preferredWidth: ui.du(18)
                        preferredHeight: ui.du(18)
                    }
                }
                Label {
                    horizontalAlignment: HorizontalAlignment.Center
                    topMargin: ui.du(3)
                    text: voice.peerName
                    textStyle.color: Color.White
                    textStyle.fontSize: FontSize.XLarge
                    textStyle.textAlign: TextAlign.Center
                    multiline: true
                }
                Label {
                    horizontalAlignment: HorizontalAlignment.Center
                    text: voice.connected ? voice.duration : voice.status
                    textStyle.color: Color.create("#c8dcf2")
                    textStyle.fontSize: FontSize.Medium
                    textStyle.textAlign: TextAlign.Center
                }
                Label {
                    visible: voice.connected
                    horizontalAlignment: HorizontalAlignment.Center
                    text: voice.status
                    textStyle.color: Color.create("#8fb0d4")
                    textStyle.fontSize: FontSize.XSmall
                }
            }

            // In-call controls (mute + End), BBM ControlBox layout: mute left, End right.
            Container {
                visible: !voice.incoming
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment: VerticalAlignment.Bottom
                leftPadding: ui.du(2)
                rightPadding: ui.du(2)
                bottomPadding: ui.du(2)
                layout: StackLayout { orientation: LayoutOrientation.LeftToRight }

                ImageButton {
                    verticalAlignment: VerticalAlignment.Center
                    enabled: voice.connected
                    defaultImageSource: voice.muted ? "asset:///images/ChatView/call/mute_icon_down.png"
                                                    : "asset:///images/ChatView/call/mute_icon.png"
                    pressedImageSource: "asset:///images/ChatView/call/mute_icon_down.png"
                    disabledImageSource: "asset:///images/ChatView/call/mute_icon_disabled.png"
                    accessibility.name: voice.muted ? "Unmute" : "Mute"
                    onClicked: voice.setMuted(!voice.muted)
                }
                Container { layoutProperties: StackLayoutProperties { spaceQuota: 1 } }
                ImageButton {
                    verticalAlignment: VerticalAlignment.Center
                    defaultImageSource: "asset:///images/ChatView/call/end_call_icon.png"
                    pressedImageSource: "asset:///images/ChatView/call/end_call_icon_down.png"
                    accessibility.name: "End call"
                    onClicked: voice.hangup()
                }
            }

            // Incoming call: Decline / Answer.
            Container {
                visible: voice.incoming
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment: VerticalAlignment.Bottom
                bottomPadding: ui.du(5)
                layout: StackLayout { orientation: LayoutOrientation.LeftToRight }

                Container { layoutProperties: StackLayoutProperties { spaceQuota: 1 } }
                ImageButton {
                    defaultImageSource: "asset:///images/ChatView/call/decline_call.png"
                    preferredWidth: ui.du(17); preferredHeight: ui.du(17)
                    accessibility.name: "Decline"
                    onClicked: voice.hangup()
                }
                Container { layoutProperties: StackLayoutProperties { spaceQuota: 2 } }
                ImageButton {
                    defaultImageSource: "asset:///images/ChatView/call/answer_call.png"
                    preferredWidth: ui.du(17); preferredHeight: ui.du(17)
                    accessibility.name: "Answer"
                    onClicked: voice.answer()
                }
                Container { layoutProperties: StackLayoutProperties { spaceQuota: 1 } }
            }
        }
    }
}
