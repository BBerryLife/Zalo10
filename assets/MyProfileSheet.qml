import bb.cascades 1.4
import bb.cascades.pickers 1.0
import bb.system 1.0
import QtQuick 1.0

// My Profile: opened by tapping the avatar / name in the title bar of any tab. Big avatar at the left,
// the account's details below. Tapping the avatar changes the profile picture (existing photo or camera).
Sheet {
    id: mySheet

    property string selfName:   ""
    property string selfAvatar: ""

    property string avatarPath: ""     // full size picture (falls back to the 120px selfAvatar)
    property string coverPath:  ""
    property string bio:        ""
    property string genderText: ""
    property string birthday:   ""
    property string phone:      ""
    property string username:   ""
    property string joined:     ""
    property string shownName:  ""
    property string zaloId:     ""
    property bool   uploading:  false

    function pad(n) { return (n < 10 ? "0" : "") + n }

    function applyProfile(p) {
        var nm = p.displayName || p.zaloName || "";
        if (nm.length > 0) mySheet.shownName = nm;
        mySheet.bio = (p.status || "").replace(/\s+/g, " ").replace(/^\s+|\s+$/g, "");
        mySheet.genderText = (p.gender === 0) ? "Male" : ((p.gender === 1) ? "Female" : "");
        mySheet.birthday = p.sdob || "";
        var ph = (p.phoneNumber || "").toString();
        if (ph.length > 0 && ph.charAt(0) !== "+" && ph.charAt(0) !== "0") ph = "+" + ph;
        mySheet.phone = ph;
        mySheet.username = p.username || "";
        if (p.createdTs && p.createdTs > 0) {
            var d = new Date(p.createdTs * 1000);
            mySheet.joined = mySheet.pad(d.getDate()) + "/" + mySheet.pad(d.getMonth() + 1) + "/" + d.getFullYear();
        }
        var coverUrl = p.cover || p.bgavatar || "";
        if (coverUrl.length > 0) zService.downloadAvatar("bg_self", coverUrl);
    }

    function refresh() {
        zService.fetchSelfProfile();
        zService.fetchFullAvatar(zService.selfUid);
    }

    function openSheet() {
        mySheet.uploading = false;
        mySheet.zaloId = zService.selfUid;   // constant property: read when the sheet opens, not at startup
        mySheet.refresh();
        mySheet.open();
    }

    function useNewPhoto(path) {
        if (!path || path.length === 0) return;
        mySheet.uploading = true;
        zService.changeSelfAvatar(path);
    }

    attachedObjects: [
        SystemDialog {
            id: avatarDialog
            title: qsTr("Change Profile Picture")
            body: qsTr("Where should the new picture come from?")
            confirmButton.label: qsTr("Choose Photo")
            confirmButton.enabled: true
            cancelButton.label: qsTr("Cancel")
            cancelButton.enabled: true
            buttons: [ SystemUiButton { label: qsTr("Open Camera"); enabled: true } ]
            onFinished: {
                if (result === SystemUiResult.ConfirmButtonSelection) {
                    photoPicker.open();
                } else if (result === SystemUiResult.ButtonSelection) {
                    app.captureCameraPhoto();
                }
            }
        },
        FilePicker {
            id: photoPicker
            type: FileType.Picture
            mode: FilePickerMode.Picker
            title: qsTr("Select Picture")
            onFileSelected: mySheet.useNewPhoto(selectedFiles[0])
        },
        SystemToast { id: resultToast },
        Timer {
            // Zalo needs a moment before the new picture is served.
            id: refreshTimer
            interval: 1500
            repeat: false
            onTriggered: mySheet.refresh()
        },
        Connections {
            target: app
            onCameraPhotoCaptured: mySheet.useNewPhoto(path)
        },
        Connections {
            target: zService
            onSelfProfileDetails: mySheet.applyProfile(profile)
            onFullAvatarReady: {
                if (uid === zService.selfUid) zService.downloadAvatar("full_self", url);
            }
            onAvatarReady: {
                if (threadId === "full_self") mySheet.avatarPath = localPath;
                else if (threadId === "bg_self") mySheet.coverPath = localPath;
            }
            onSelfAvatarChanged: {
                mySheet.uploading = false;
                if (ok) {
                    resultToast.body = qsTr("Profile picture updated");
                    mySheet.avatarPath = "";
                    refreshTimer.restart();
                } else {
                    resultToast.body = qsTr("Could not change picture") + (error.length > 0 ? ": " + error : "");
                }
                resultToast.show();
            }
        }
    ]

    content: Page {
        titleBar: TitleBar {
            title: qsTr("My Profile")
            dismissAction: ActionItem {
                title: qsTr("Close")
                onTriggered: mySheet.close()
            }
        }

        ScrollView {
            scrollViewProperties { scrollMode: ScrollMode.Vertical }
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment: VerticalAlignment.Fill

            Container {
                layout: StackLayout { orientation: LayoutOrientation.TopToBottom }
                horizontalAlignment: HorizontalAlignment.Fill

                // Header: cover, a translucent dark band, the big avatar at the left, name and status beside it.
                Container {
                    preferredHeight: ui.du(30)
                    horizontalAlignment: HorizontalAlignment.Fill
                    layout: DockLayout {}
                    background: Color.create("#1a1a2e")

                    ImageView {
                        imageSource: mySheet.coverPath.length > 0
                            ? mySheet.coverPath
                            : "asset:///images/ContactsTab/ProfileView/BB_Pattern_Vivid.jpg"
                        horizontalAlignment: HorizontalAlignment.Fill
                        verticalAlignment: VerticalAlignment.Fill
                        scalingMethod: ScalingMethod.AspectFill
                    }

                    Container {
                        verticalAlignment: VerticalAlignment.Bottom
                        horizontalAlignment: HorizontalAlignment.Fill
                        preferredHeight: ui.du(26)
                        background: Color.create("#99000000")
                    }

                    Container {
                        verticalAlignment: VerticalAlignment.Bottom
                        horizontalAlignment: HorizontalAlignment.Fill
                        layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                        leftPadding: ui.du(2)
                        rightPadding: ui.du(2)
                        bottomPadding: ui.du(2)

                        // Avatar: tap to change. The translucent black strip carries the hint.
                        Container {
                            preferredWidth: ui.du(22)
                            preferredHeight: ui.du(22)
                            minWidth: ui.du(22)
                            background: Color.create("#2575fc")
                            layout: DockLayout {}
                            gestureHandlers: TapHandler {
                                onTapped: { if (!mySheet.uploading) avatarDialog.show(); }
                            }

                            ImageView {
                                imageSource: mySheet.avatarPath.length > 0 ? mySheet.avatarPath
                                           : (mySheet.selfAvatar.length > 0 ? mySheet.selfAvatar
                                              : "asset:///images/ChatView/default_caller.png")
                                horizontalAlignment: HorizontalAlignment.Fill
                                verticalAlignment: VerticalAlignment.Fill
                                scalingMethod: ScalingMethod.AspectFill
                            }
                            Container {
                                verticalAlignment: VerticalAlignment.Bottom
                                horizontalAlignment: HorizontalAlignment.Fill
                                preferredHeight: ui.du(4.5)
                                background: Color.create("#99000000")
                                layout: DockLayout {}
                                Label {
                                    text: qsTr("Change")
                                    horizontalAlignment: HorizontalAlignment.Center
                                    verticalAlignment: VerticalAlignment.Center
                                    textStyle { base: SystemDefaults.TextStyles.SmallText; color: Color.White }
                                }
                            }
                            ActivityIndicator {
                                running: mySheet.uploading
                                visible: mySheet.uploading
                                preferredWidth: ui.du(9)
                                preferredHeight: ui.du(9)
                                horizontalAlignment: HorizontalAlignment.Center
                                verticalAlignment: VerticalAlignment.Center
                            }
                        }

                        Container {
                            layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                            verticalAlignment: VerticalAlignment.Bottom
                            leftMargin: ui.du(2)
                            Label {
                                text: mySheet.shownName.length > 0 ? mySheet.shownName : mySheet.selfName
                                multiline: false
                                textStyle { base: SystemDefaults.TextStyles.BigText; color: Color.White; fontWeight: FontWeight.Bold }
                            }
                            Label {
                                visible: mySheet.bio.length > 0
                                text: mySheet.bio
                                multiline: true
                                textStyle { base: SystemDefaults.TextStyles.BodyText; color: Color.create("#dddddd") }
                            }
                        }
                    }
                }

                ProfileInfoRow { label: qsTr("Status");      value: mySheet.bio }
                ProfileInfoRow { label: qsTr("Phone");       value: mySheet.phone }
                ProfileInfoRow { label: qsTr("Gender");      value: mySheet.genderText }
                ProfileInfoRow { label: qsTr("Birthday");    value: mySheet.birthday }
                ProfileInfoRow { label: qsTr("Username");    value: mySheet.username }
                ProfileInfoRow { label: qsTr("Joined Zalo"); value: mySheet.joined }
                ProfileInfoRow { label: qsTr("Zalo ID");     value: mySheet.zaloId }
            }
        }
    }
}
