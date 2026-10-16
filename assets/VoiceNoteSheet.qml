import bb.cascades 1.4
import bb.multimedia 1.0
import QtQuick 1.0

Sheet {
    id: voiceNoteSheetRoot

    signal voiceNoteReady(string path)
    signal voiceNoteError(string message)

    property string state: "idle"
    property string outputPath: ""
    // Creation timestamp used as a unique file name so a leftover .m4a (e.g. after a crash) isn't
    // overwritten. Declared on the root because AudioRecorder.outputUrl binds to it.
    property int created: 0

    function reset() {
        voiceNoteSheetRoot.state = "idle";
        voiceNoteSheetRoot.outputPath = "";
        audioRecorder.reset();
    }

    onOpened: {
        voiceNoteSheetRoot.created = new Date().getTime();
        voiceNoteSheetRoot.reset();
    }

    Page {
        titleBar: TitleBar {
            title: "Voice Note"
            dismissAction: ActionItem {
                title: "Cancel"
                onTriggered: {
                    if (voiceNoteSheetRoot.outputPath !== "") {
                        zService.deleteLocalFile(voiceNoteSheetRoot.outputPath);
                    }
                    voiceNoteSheetRoot.reset();
                    voiceNoteSheetRoot.close();
                }
            }
        }

        attachedObjects: [
            AudioRecorder {
                id: audioRecorder
                outputUrl: "file:///tmp/zalo10_voicenote_" + voiceNoteSheetRoot.created + ".m4a"
                statusInterval: 200

                onMediaStateChanged: {
                    // UI follows the state property set by the buttons, not mediaState, to avoid a race with the
                    // late mediaStateChanged signal.
                }
                onError: {
                    voiceNoteSheetRoot.reset();
                    voiceNoteSheetRoot.close();
                    voiceNoteSheetRoot.voiceNoteError("Recording error");
                }
            }
        ]

        Container {
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment: VerticalAlignment.Center
            layout: StackLayout { orientation: LayoutOrientation.TopToBottom }
            topPadding: 60; bottomPadding: 60

            ImageView {
                imageSource: "asset:///images/File Types/File Type - Voice Note (Audio Recording).png"
                scalingMethod: ScalingMethod.AspectFit
                horizontalAlignment: HorizontalAlignment.Center
                preferredWidth: 120; preferredHeight: 120
                opacity: voiceNoteSheetRoot.state === "recording" ? 1.0 : 0.6
                bottomMargin: 20
            }

            Label {
                horizontalAlignment: HorizontalAlignment.Center
                text: voiceNoteSheetRoot.state === "idle"     ? "Tap record to start" :
                      voiceNoteSheetRoot.state === "recording" ? "Recording..." :
                                                                  "Recorded — send or discard"
                textStyle.base: SystemDefaults.TextStyles.SubtitleText
                bottomMargin: 30
            }

            Button {
                horizontalAlignment: HorizontalAlignment.Center
                text: voiceNoteSheetRoot.state === "recording" ? "Stop" : "Record"
                visible: voiceNoteSheetRoot.state !== "recorded"
                onClicked: {
                    if (voiceNoteSheetRoot.state === "idle") {
                        voiceNoteSheetRoot.state = "recording";
                        audioRecorder.record();
                    } else if (voiceNoteSheetRoot.state === "recording") {
                        audioRecorder.reset();
                        voiceNoteSheetRoot.outputPath = audioRecorder.outputUrl.toString().replace("file://", "");
                        voiceNoteSheetRoot.state = "recorded";
                    }
                }
            }

            Container {
                visible: voiceNoteSheetRoot.state === "recorded"
                horizontalAlignment: HorizontalAlignment.Center
                layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                topMargin: 10

                Button {
                    text: "Discard"
                    onClicked: {
                        zService.deleteLocalFile(voiceNoteSheetRoot.outputPath);
                        voiceNoteSheetRoot.reset();
                    }
                    rightMargin: 10
                }
                Button {
                    text: "Send"
                    onClicked: {
                        var p = voiceNoteSheetRoot.outputPath;
                        voiceNoteSheetRoot.state = "idle";
                        voiceNoteSheetRoot.outputPath = "";
                        voiceNoteSheetRoot.close();
                        voiceNoteSheetRoot.voiceNoteReady(p);
                    }
                }
            }
        }
    }
}
