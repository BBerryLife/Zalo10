import bb.cascades 1.4
import QtQuick 1.0

Sheet {
    id: attachPickerSheetRoot

    signal pictureSelected()
    signal videoSelected()
    signal fileSelected()
    signal audioSelected()
    signal voiceNoteSelected()
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
                    // ArrayDataModel has no declarable items property: fill it with qualified append() calls
                    // (attachModel.append); unqualified append()/insertList() throws ReferenceError.
                    Component.onCompleted: {
                        attachModel.append({ key: "picture",     label: "Picture",     icon: "asset:///images/ChatView/bubble/file_types/File Type - Picture (Image).png", enabled: true  });
                        attachModel.append({ key: "video",       label: "Video",       icon: "asset:///images/ChatView/bubble/file_types/File Type - Video.png",           enabled: true  });
                        attachModel.append({ key: "location",    label: "Location",    icon: "asset:///images/ChatView/bubble/file_types/File Type - Location.png",      enabled: false });
                        attachModel.append({ key: "audio",       label: "Audio",       icon: "asset:///images/ChatView/bubble/file_types/File Type - Audio.png",         enabled: true  });
                        attachModel.append({ key: "voicenote",   label: "Voice Note",  icon: "asset:///images/ChatView/bubble/file_types/File Type - Voice Note (Audio Recording).png", enabled: true  });
                        attachModel.append({ key: "contact",     label: "Contact",     icon: "asset:///images/ChatView/bubble/file_types/File Type - VCF (Contanct).png", enabled: true  });
                        attachModel.append({ key: "file",        label: "File",        icon: "asset:///images/ChatView/bubble/file_types/File Type - Generic.png",       enabled: true  });
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
