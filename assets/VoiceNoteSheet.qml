import bb.cascades 1.4
import bb.multimedia 1.0
import QtQuick 1.0

// Sheet ghi âm nhanh (Voice Note) — ghi ra .m4a rồi gửi như file đính kèm
// thường qua sendFile() (msgType=3, cùng bubble/pipeline với video/file).
// AudioRecorder (bb.multimedia) là QML type có sẵn, không cần cầu nối C++
// nào — import thẳng, dùng record()/reset() và theo dõi mediaState/duration
// qua signal có sẵn của chính nó.
//
// Luồng: mở sheet -> bấm Record -> ghi âm (duration chạy) -> bấm Stop ->
// hiện Send/Discard -> Send thì đóng sheet và emit voiceNoteReady(path) cho
// ChatView xử lý gửi đi; Discard/Cancel thì xoá file tạm rồi đóng sheet
// không gửi gì.
Sheet {
    id: voiceNoteSheetRoot

    // path: đường dẫn file://... .m4a đã ghi xong, sẵn sàng gửi.
    signal voiceNoteReady(string path)
    // Lỗi ghi âm (thiết bị bận, hết bộ nhớ...) — ChatView hiển thị qua
    // errorToast chung của nó thay vì tự có toast riêng trong sheet này.
    signal voiceNoteError(string message)

    // "idle" (chưa ghi) -> "recording" -> "recorded" (đã dừng, chờ gửi/huỷ)
    property string state: "idle"
    property string outputPath: ""
    // Timestamp tạo sheet, dùng làm phần tên file duy nhất — tránh ghi đè
    // lên 1 file .m4a cũ còn sót lại nếu app crash giữa chừng lần ghi trước
    // (dù đã cố xoá ở Cancel/Send, không đảm bảo 100%). Đặt trên chính
    // voiceNoteSheetRoot (không phải Page con) vì outputUrl của AudioRecorder
    // bind vào nó và cần property này resolve được ngay từ lần mở sheet đầu.
    property int created: 0

    function reset() {
        voiceNoteSheetRoot.state = "idle";
        voiceNoteSheetRoot.outputPath = "";
        audioRecorder.reset();
    }

    // Gọi khi mở sheet lần đầu / mở lại sau khi đã gửi hoặc huỷ lần trước —
    // luôn bắt đầu từ trạng thái sạch, không giữ file ghi âm cũ. created
    // đổi mỗi lần mở để không ghi đè lên file .m4a của phiên trước.
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
                    // Huỷ: xoá file tạm nếu đã lỡ ghi rồi mới bấm Cancel,
                    // không gửi gì cả.
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
                // /tmp là sandbox riêng app, ghi tự do không cần access_shared;
                // file chỉ tồn tại tạm cho tới khi gửi qua sendFile() (sendFile
                // tự đọc rồi upload, không cần file nằm ở thư mục chia sẻ).
                outputUrl: "file:///tmp/zalo10_voicenote_" + voiceNoteSheetRoot.created + ".m4a"
                statusInterval: 200

                onMediaStateChanged: {
                    // Chỉ log để debug — UI tự cập nhật qua state property
                    // do nút bấm set trực tiếp (xem onClicked bên dưới),
                    // không phụ thuộc mediaState để tránh race giữa lúc
                    // record() vừa gọi và signal mediaStateChanged tới chậm.
                    console.log("[VoiceNote] mediaState=" + mediaState);
                }
                onError: {
                    console.log("[VoiceNote] error=" + mediaError + " position=" + position);
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

            // Nút Record/Stop — 1 nút duy nhất đổi vai trò theo state, tránh
            // 2 nút chồng nhau chỉ 1 cái visible tại 1 thời điểm (đơn giản
            // hơn để bảo trì, và khớp cách các nút khác trong app xử lý
            // trạng thái qua 1 Button.onClicked duy nhất).
            Button {
                horizontalAlignment: HorizontalAlignment.Center
                text: voiceNoteSheetRoot.state === "recording" ? "Stop" : "Record"
                visible: voiceNoteSheetRoot.state !== "recorded"
                onClicked: {
                    if (voiceNoteSheetRoot.state === "idle") {
                        voiceNoteSheetRoot.state = "recording";
                        audioRecorder.record();
                    } else if (voiceNoteSheetRoot.state === "recording") {
                        audioRecorder.reset(); // reset() = stop + release, giữ file đã ghi trên đĩa
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
