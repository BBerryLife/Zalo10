import bb.cascades 1.4
import QtQuick 1.0

// Attach sheet giống list "Attach" gốc của BB10 Hub (icon vuông màu + tên).
// Picture/Video/File/Audio/Voice Note/Contact hoạt động thật — chỉ còn
// Location hiển thị mờ (disabled), sẽ nối sau. Appointment bị bỏ hẳn khỏi
// danh sách (không phải chỉ disable) theo yêu cầu. Không dùng Repeater
// (không tồn tại ở QtQuick1/Cascades) — build tĩnh từng CustomListItem.
Sheet {
    id: attachPickerSheetRoot

    signal pictureSelected()
    signal videoSelected()
    signal fileSelected()
    // Audio = chọn 1 file âm thanh có sẵn trên máy (mp3/flac/m4a/...) qua
    // FilePicker — khác Voice Note (ghi âm mới ngay trong app).
    signal audioSelected()
    // Voice Note = mở màn ghi âm (AudioRecorder, bb.multimedia) ngay trong
    // app, ghi ra .m4a rồi gửi như file đính kèm thường.
    signal voiceNoteSelected()
    // Contact = mở ContactPicker (bb.cascades.pickers) chọn 1 danh bạ máy,
    // build .vcf từ Contact rồi gửi như file đính kèm thường.
    signal contactSelected()

    Page {
        titleBar: TitleBar {
            title: "Attach"
            dismissAction: ActionItem {
                title: "Cancel"
                onTriggered: { attachPickerSheetRoot.close(); }
            }
        }

        Container {
            horizontalAlignment: HorizontalAlignment.Fill

            ListView {
                dataModel: ArrayDataModel {
                    id: attachModel
                    // enabled=true cho Picture/Video/File/Audio/Voice Note/Contact —
                    // chỉ Location còn disabled, giữ nguyên thứ tự/tên của Attach
                    // sheet gốc BB10. Appointment bị loại bỏ hẳn (không append).
                    // ArrayDataModel không có property "items" để gán mảng khai báo
                    // trực tiếp — phải nạp dữ liệu bằng append() trong JS, gọi có định
                    // danh id (attachModel.append), giống pattern dùng ở ChatsTab/
                    // GroupsTab/ForwardPickerSheet... Gọi insertList()/append() trần
                    // (không qualify) sẽ ra ReferenceError vì không tự suy ra "this".
                    Component.onCompleted: {
                        attachModel.append({ key: "picture",     label: "Picture",     icon: "asset:///images/File Types/File Type - Picture (Image).png", enabled: true  });
                        attachModel.append({ key: "video",       label: "Video",       icon: "asset:///images/File Types/File Type - Video.png",           enabled: true  });
                        attachModel.append({ key: "location",    label: "Location",    icon: "asset:///images/File Types/File Type - Location.png",      enabled: false });
                        attachModel.append({ key: "audio",       label: "Audio",       icon: "asset:///images/File Types/File Type - Audio.png",         enabled: true  });
                        attachModel.append({ key: "voicenote",   label: "Voice Note",  icon: "asset:///images/File Types/File Type - Voice Note (Audio Recording).png", enabled: true  });
                        attachModel.append({ key: "contact",     label: "Contact",     icon: "asset:///images/File Types/File Type - VCF (Contanct).png", enabled: true  });
                        attachModel.append({ key: "file",        label: "File",        icon: "asset:///images/File Types/File Type - Generic.png",       enabled: true  });
                    }
                }

                listItemComponents: [
                    ListItemComponent {
                        CustomListItem {
                            dividerVisible: true
                            highlightAppearance: ListItemData.enabled ? HighlightAppearance.Default : HighlightAppearance.None

                            Container {
                                layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                                verticalAlignment: VerticalAlignment.Center
                                leftPadding: 16; rightPadding: 16; topPadding: 10; bottomPadding: 10
                                opacity: ListItemData.enabled ? 1.0 : 0.35

                                ImageView {
                                    imageSource: ListItemData.icon
                                    scalingMethod: ScalingMethod.AspectFit
                                    verticalAlignment:   VerticalAlignment.Center
                                    horizontalAlignment: HorizontalAlignment.Left
                                    minWidth: 70
                                    preferredWidth:  68
                                    preferredHeight: 68
                                }
                                Label {
                                    text: ListItemData.label
                                    verticalAlignment: VerticalAlignment.Center
                                    layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                                    leftMargin: 10
                                    textStyle.base: SystemDefaults.TextStyles.TitleText
                                }
                            }
                        }
                    }
                ]

                onTriggered: {
                    var item = dataModel.data(indexPath);
                    if (!item.enabled) return;
                    attachPickerSheetRoot.close();
                    if (item.key === "picture")     attachPickerSheetRoot.pictureSelected();
                    else if (item.key === "video")  attachPickerSheetRoot.videoSelected();
                    else if (item.key === "file")   attachPickerSheetRoot.fileSelected();
                    else if (item.key === "audio")     attachPickerSheetRoot.audioSelected();
                    else if (item.key === "voicenote") attachPickerSheetRoot.voiceNoteSelected();
                    else if (item.key === "contact")   attachPickerSheetRoot.contactSelected();
                }
            }
        }
    }
}
