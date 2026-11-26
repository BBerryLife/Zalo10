import bb.cascades 1.4
import bb.system 1.0
import QtQuick 1.0

// 1:1 voice call screen, laid out like the stock Phone app's call screen.
// Background: audio_bg_no_avatar.png on square screens, default_caller_2.png on portrait screens, full bleed.
// Caller name (big) and state sit left-aligned around 42% of the screen height, as in the stock design.
// Incoming calls show the slide control at the bottom (IncomingSlider.qml); active calls show mute + End.
// Driven entirely by the "voice" context property (VoiceCallBridge). Presented while voice.active.
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
            background: Color.Black

            // Full-screen background picture.
            ImageView {
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment: VerticalAlignment.Fill
                scalingMethod: ScalingMethod.AspectFill
                imageSource: app.squareScreen
                             ? "asset:///images/ChatView/call/audio_bg_no_avatar.png"
                             : "asset:///images/ChatView/default_caller_2.png"
            }

            // Slide tint while the incoming-call slider is being dragged: green towards answer,
            // red towards decline (the stock Phone app's look).
            Container {
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment: VerticalAlignment.Fill
                background: Color.create("#3c5c22")
                opacity: incomingSlider.progress < 0 ? Math.min(1, -incomingSlider.progress) * 0.85 : 0
            }
            Container {
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment: VerticalAlignment.Fill
                background: Color.create("#7a1c20")
                opacity: incomingSlider.progress > 0 ? Math.min(1, incomingSlider.progress) * 0.85 : 0
            }

            // Caller name and state, left-aligned, the name in big text.
            Container {
                horizontalAlignment: HorizontalAlignment.Left
                verticalAlignment: VerticalAlignment.Top
                topPadding: (app.squareScreen ? app.screenWidthPx : app.screenHeightPx) * 0.42
                leftPadding: app.screenWidthPx * 0.04
                rightPadding: app.screenWidthPx * 0.04
                layout: StackLayout { orientation: LayoutOrientation.TopToBottom }

                Label {
                    horizontalAlignment: HorizontalAlignment.Left
                    text: voice.peerName
                    textStyle.color: Color.White
                    textStyle.fontSize: FontSize.XXLarge
                    textStyle.textAlign: TextAlign.Left
                    multiline: true
                }
                Label {
                    horizontalAlignment: HorizontalAlignment.Left
                    text: voice.connected ? voice.duration : voice.status
                    textStyle.color: Color.create("#c8dcf2")
                    textStyle.fontSize: FontSize.Medium
                    textStyle.textAlign: TextAlign.Left
                }
                Label {
                    visible: voice.connected
                    horizontalAlignment: HorizontalAlignment.Left
                    text: voice.status
                    textStyle.color: Color.create("#8fb0d4")
                    textStyle.fontSize: FontSize.XSmall
                    textStyle.textAlign: TextAlign.Left
                }
            }

            // In-call controls (mute + End): shown once the call is no longer incoming.
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

            // Incoming call: slide left to answer, right to decline (see IncomingSlider.qml).
            IncomingSlider {
                id: incomingSlider
                visible: voice.incoming
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment: VerticalAlignment.Bottom
                // Kept clear of the bottom edge: a horizontal drag right at the bottom edge seems to start the system
                // "peek" gesture, which pulls this page aside and steals the drag.
                bottomPadding: (app.squareScreen ? app.screenWidthPx : app.screenHeightPx) * 0.09
                onAnswered: voice.answer()
                onDeclined: voice.hangup()
            }
        }
    }
}
