import bb.cascades 1.4
import bb.system 1.0
import QtQuick 1.0

// 1:1 voice call screen.
// Background: audio_bg.png, full bleed on all screens.
// Incoming calls: avatar centred, name + "Incoming Zalo call" below, accept/decline buttons at the bottom.
// Active calls: caller name + state left-aligned at ~42% height, mute + End at the bottom.
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

            // Full-screen background: audio_bg.png for all screens.
            ImageView {
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment: VerticalAlignment.Fill
                scalingMethod: ScalingMethod.AspectFill
                imageSource: "asset:///images/ChatView/call/audio_bg.png"
            }

            // ── INCOMING CALL layout ──────────────────────────────────────────
            // Square avatar (centre), name + "Incoming Zalo call" below, accept/decline at the bottom.
            Container {
                visible: voice.incoming
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment: VerticalAlignment.Fill
                layout: DockLayout {}

                Container {
                    horizontalAlignment: HorizontalAlignment.Center
                    verticalAlignment: VerticalAlignment.Center
                    // Slightly above true centre so the bottom buttons have breathing room.
                    translationY: -(app.squareScreen ? app.screenWidthPx : app.screenHeightPx) * 0.08
                    leftPadding: app.screenWidthPx * 0.04
                    rightPadding: app.screenWidthPx * 0.04
                    layout: StackLayout { orientation: LayoutOrientation.TopToBottom }

                    // Square avatar. The default picture always sits underneath, so a missing or
                    // not-yet-downloaded local avatar never leaves a hole.
                    Container {
                        horizontalAlignment: HorizontalAlignment.Center
                        preferredWidth: app.screenWidthPx * 0.40
                        preferredHeight: app.screenWidthPx * 0.40
                        layout: DockLayout {}

                        ImageView {
                            horizontalAlignment: HorizontalAlignment.Fill
                            verticalAlignment: VerticalAlignment.Fill
                            scalingMethod: ScalingMethod.AspectFill
                            imageSource: "asset:///images/ChatView/call/avatar_default_light.png"
                        }
                        ImageView {
                            horizontalAlignment: HorizontalAlignment.Fill
                            verticalAlignment: VerticalAlignment.Fill
                            scalingMethod: ScalingMethod.AspectFill
                            // Plain local path, like every other avatar in the app (no file:// prefix).
                            imageSource: voice.peerAvatar
                            visible: voice.peerAvatar.length > 0
                        }
                    }

                    Label {
                        topMargin: ui.du(3)
                        horizontalAlignment: HorizontalAlignment.Center
                        text: voice.peerName
                        multiline: true
                        textStyle.base: SystemDefaults.TextStyles.BigText
                        textStyle.color: Color.White
                        textStyle.textAlign: TextAlign.Center
                    }

                    Label {
                        topMargin: ui.du(0.5)
                        horizontalAlignment: HorizontalAlignment.Center
                        text: "Incoming Zalo call"
                        textStyle.color: Color.create("#c8dcf2")
                        textStyle.fontSize: FontSize.Medium
                        textStyle.textAlign: TextAlign.Center
                    }
                }

                // Accept (left) / Decline (right). ImageView + TapHandler instead of ImageButton:
                // ImageButton draws the blue keyboard-focus highlight, ImageView never does.
                Container {
                    horizontalAlignment: HorizontalAlignment.Fill
                    verticalAlignment: VerticalAlignment.Bottom
                    bottomPadding: (app.squareScreen ? app.screenWidthPx : app.screenHeightPx) * 0.06
                    leftPadding: app.screenWidthPx * 0.14
                    rightPadding: app.screenWidthPx * 0.14
                    layout: StackLayout { orientation: LayoutOrientation.LeftToRight }

                    ImageView {
                        id: acceptBtn
                        verticalAlignment: VerticalAlignment.Center
                        preferredWidth: app.screenWidthPx * 0.19
                        preferredHeight: app.screenWidthPx * 0.19
                        scalingMethod: ScalingMethod.AspectFit
                        imageSource: "asset:///images/ChatView/call/accept.png"
                        navigation.focusPolicy: NavigationFocusPolicy.NotFocusable
                        accessibility.name: "Accept"
                        gestureHandlers: [
                            TapHandler { onTapped: voice.answer() }
                        ]
                        onTouch: {
                            if (event.isDown())
                                acceptBtn.imageSource = "asset:///images/ChatView/call/accept_pressed.png";
                            else if (event.isUp() || event.isCancel())
                                acceptBtn.imageSource = "asset:///images/ChatView/call/accept.png";
                        }
                    }

                    Container { layoutProperties: StackLayoutProperties { spaceQuota: 1 } }

                    ImageView {
                        id: declineBtn
                        verticalAlignment: VerticalAlignment.Center
                        preferredWidth: app.screenWidthPx * 0.19
                        preferredHeight: app.screenWidthPx * 0.19
                        scalingMethod: ScalingMethod.AspectFit
                        imageSource: "asset:///images/ChatView/call/decline.png"
                        navigation.focusPolicy: NavigationFocusPolicy.NotFocusable
                        accessibility.name: "Decline"
                        gestureHandlers: [
                            TapHandler { onTapped: voice.hangup() }
                        ]
                        onTouch: {
                            if (event.isDown())
                                declineBtn.imageSource = "asset:///images/ChatView/call/decline_pressed.png";
                            else if (event.isUp() || event.isCancel())
                                declineBtn.imageSource = "asset:///images/ChatView/call/decline.png";
                        }
                    }
                }
            }

            // Video call: the other person's picture, full screen once the first frame is decoded.
            Container {
                visible: voice.video && voice.hasRemoteVideo
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment: VerticalAlignment.Fill
                background: Color.Black
                layout: DockLayout {}
                ImageView {
                    horizontalAlignment: HorizontalAlignment.Fill
                    verticalAlignment: VerticalAlignment.Fill
                    scalingMethod: ScalingMethod.AspectFit
                    image: voice.remoteFrame
                }
            }

            // Video call: our own camera, small, top right.
            Container {
                visible: voice.video && voice.cameraOn
                horizontalAlignment: HorizontalAlignment.Right
                verticalAlignment: VerticalAlignment.Top
                topPadding: ui.du(2)
                rightPadding: ui.du(2)
                ImageView {
                    preferredWidth: app.screenWidthPx * 0.3
                    preferredHeight: app.screenWidthPx * 0.3
                    scalingMethod: ScalingMethod.AspectFit
                    image: voice.localFrame
                }
            }

            // Video call with a picture: name and timer in one line at the top left.
            Container {
                visible: voice.video && voice.hasRemoteVideo
                horizontalAlignment: HorizontalAlignment.Left
                verticalAlignment: VerticalAlignment.Top
                topPadding: ui.du(2)
                leftPadding: ui.du(2)
                Label {
                    text: voice.peerName + "  " + (voice.connected ? voice.duration : voice.status)
                    textStyle.color: Color.White
                    textStyle.fontSize: FontSize.Small
                }
            }

            // ── ACTIVE / CONNECTING CALL layout ──────────────────────────────
            // Caller name and state, left-aligned, the name in big text.
            Container {
                visible: !voice.incoming && !(voice.video && voice.hasRemoteVideo)
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
                // Video call: camera on/off and front/back.
                ImageButton {
                    visible: voice.video
                    verticalAlignment: VerticalAlignment.Center
                    leftMargin: ui.du(2)
                    enabled: voice.connected
                    opacity: voice.cameraOn ? 1.0 : 0.45
                    preferredWidth: ui.du(11); preferredHeight: ui.du(11)
                    defaultImageSource: "asset:///images/ChatView/ic_bbm_video_answer.png"
                    pressedImageSource: "asset:///images/ChatView/ic_bbm_video_answer.png"
                    accessibility.name: voice.cameraOn ? "Turn camera off" : "Turn camera on"
                    onClicked: voice.setCameraOn(!voice.cameraOn)
                }
                ImageButton {
                    visible: voice.video
                    verticalAlignment: VerticalAlignment.Center
                    leftMargin: ui.du(2)
                    enabled: voice.connected && voice.cameraOn
                    defaultImageSource: "asset:///images/ChatView/call/swapcamera_icon.png"
                    pressedImageSource: "asset:///images/ChatView/call/swapcamera_icon_down.png"
                    disabledImageSource: "asset:///images/ChatView/call/swapcamera_icon_disabled.png"
                    accessibility.name: "Switch camera"
                    onClicked: voice.switchCamera()
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
        }
    }
}
