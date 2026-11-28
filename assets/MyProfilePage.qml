import bb.cascades 1.4
import bb.cascades.pickers 1.0
import bb.system 1.0
import QtQuick 1.0

// My Profile: pushed on the tab's NavigationPane when the avatar / name in a tab's title bar is tapped.
// Big avatar in the top-left corner, name and status on a translucent black layer beside it, the other
// details below. Tap the avatar to change the picture, tap the cover to view it.
Page {
    id: myPage

    property variant navigationPane: null
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
    property double lastAvatarTap: 0

    function pad(n) { return (n < 10 ? "0" : "") + n }

    function applyProfile(p) {
        var nm = p.displayName || p.zaloName || "";
        if (nm.length > 0) myPage.shownName = nm;
        myPage.bio = (p.status || "").replace(/\s+/g, " ").replace(/^\s+|\s+$/g, "");
        myPage.genderText = (p.gender === 0) ? "Male" : ((p.gender === 1) ? "Female" : "");
        myPage.birthday = p.sdob || "";
        var ph = (p.phoneNumber || "").toString();
        if (ph.length > 0 && ph.charAt(0) !== "+" && ph.charAt(0) !== "0") ph = "+" + ph;
        myPage.phone = ph;
        myPage.username = p.username || "";
        if (p.createdTs && p.createdTs > 0) {
            var d = new Date(p.createdTs * 1000);
            myPage.joined = myPage.pad(d.getDate()) + "/" + myPage.pad(d.getMonth() + 1) + "/" + d.getFullYear();
        }
        var coverUrl = p.cover || p.bgavatar || "";
        if (coverUrl.length > 0) zService.downloadAvatar("bg_self", coverUrl);
    }

    function refresh() {
        zService.fetchSelfProfile();
        zService.fetchFullAvatar(zService.selfUid);
    }

    function showImage(path) {
        if (!path || path.length === 0) return;
        imgSheet.imageSource = path;
        imgSheet.open();
    }

    function useNewPhoto(path) {
        if (!path || path.length === 0) return;
        myPage.uploading = true;
        zService.changeSelfAvatar(path);
    }

    onCreationCompleted: {
        myPage.zaloId = zService.selfUid;   // constant property: read when the page is made, not at startup
        myPage.refresh();
    }

    ScrollView {
        scrollViewProperties { scrollMode: ScrollMode.Vertical }
        horizontalAlignment: HorizontalAlignment.Fill
        verticalAlignment: VerticalAlignment.Fill

        Container {
            layout: StackLayout { orientation: LayoutOrientation.TopToBottom }
            horizontalAlignment: HorizontalAlignment.Fill

            // Header: the cover (tap to view), the avatar flush in the top-left corner, and the black
            // translucent layer with name and status over the rest of the cover, top to bottom.
            Container {
                preferredHeight: ui.du(26)
                horizontalAlignment: HorizontalAlignment.Fill
                layout: DockLayout {}
                background: Color.create("#1a1a2e")
                gestureHandlers: TapHandler {
                    onTapped: {
                        if ((new Date().getTime() - myPage.lastAvatarTap) > 400)
                            myPage.showImage(myPage.coverPath);
                    }
                }

                ImageView {
                    imageSource: myPage.coverPath.length > 0
                        ? myPage.coverPath
                        : "asset:///images/ContactsTab/ProfileView/BB_Pattern_Vivid.jpg"
                    horizontalAlignment: HorizontalAlignment.Fill
                    verticalAlignment: VerticalAlignment.Fill
                    scalingMethod: ScalingMethod.AspectFill
                }

                Container {
                    horizontalAlignment: HorizontalAlignment.Fill
                    verticalAlignment: VerticalAlignment.Fill
                    layout: StackLayout { orientation: LayoutOrientation.LeftToRight }

                    // Avatar: tap to change. The translucent black strip carries the hint.
                    Container {
                        preferredWidth: ui.du(26)
                        preferredHeight: ui.du(26)
                        minWidth: ui.du(26)
                        background: Color.create("#2575fc")
                        layout: DockLayout {}
                        gestureHandlers: TapHandler {
                            onTapped: {
                                myPage.lastAvatarTap = new Date().getTime();
                                if (!myPage.uploading) avatarDialog.show();
                            }
                        }

                        ImageView {
                            imageSource: myPage.avatarPath.length > 0 ? myPage.avatarPath
                                       : (myPage.selfAvatar.length > 0 ? myPage.selfAvatar
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
                            running: myPage.uploading
                            visible: myPage.uploading
                            preferredWidth: ui.du(9)
                            preferredHeight: ui.du(9)
                            horizontalAlignment: HorizontalAlignment.Center
                            verticalAlignment: VerticalAlignment.Center
                        }
                    }

                    // Name and status on the black translucent layer, from the very top of the header.
                    Container {
                        layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                        verticalAlignment: VerticalAlignment.Fill
                        background: Color.create("#99000000")
                        leftPadding: ui.du(2)
                        rightPadding: ui.du(2)
                        topPadding: ui.du(2)
                        Label {
                            text: myPage.shownName.length > 0 ? myPage.shownName : myPage.selfName
                            multiline: false
                            textStyle { base: SystemDefaults.TextStyles.BigText; color: Color.White }
                        }
                        Label {
                            visible: myPage.bio.length > 0
                            text: myPage.bio
                            multiline: true
                            textStyle { base: SystemDefaults.TextStyles.BodyText; color: Color.create("#dddddd") }
                        }
                    }
                }
            }

            ProfileInfoRow { label: qsTr("Phone");       value: myPage.phone }
            ProfileInfoRow { label: qsTr("Gender");      value: myPage.genderText }
            ProfileInfoRow { label: qsTr("Birthday");    value: myPage.birthday }
            ProfileInfoRow { label: qsTr("Username");    value: myPage.username }
            ProfileInfoRow { label: qsTr("Joined Zalo"); value: myPage.joined }
            ProfileInfoRow { label: qsTr("Zalo ID");     value: myPage.zaloId }
        }
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
            onFileSelected: myPage.useNewPhoto(selectedFiles[0])
        },
        SystemToast { id: resultToast },
        Timer {
            // Zalo needs a moment before the new picture is served.
            id: refreshTimer
            interval: 1500
            repeat: false
            onTriggered: myPage.refresh()
        },
        Sheet {
            id: imgSheet
            property string imageSource: ""
            Page {
                actions: [
                    ActionItem {
                        title: qsTr("Close")
                        imageSource: "asset:///images/SettingsSheet/ic_close_white.png"
                        ActionBar.placement: ActionBarPlacement.Signature
                        onTriggered: imgSheet.close()
                    }
                ]
                Container {
                    background: Color.Black
                    layout: DockLayout {}
                    ScrollView {
                        horizontalAlignment: HorizontalAlignment.Fill
                        verticalAlignment: VerticalAlignment.Fill
                        scrollViewProperties {
                            scrollMode: ScrollMode.Both
                            pinchToZoomEnabled: true
                            minContentScale: 1.0
                            maxContentScale: 5.0
                        }
                        ImageView {
                            imageSource: imgSheet.imageSource
                            scalingMethod: ScalingMethod.AspectFit
                            horizontalAlignment: HorizontalAlignment.Fill
                            verticalAlignment: VerticalAlignment.Fill
                        }
                    }
                }
            }
        },
        Connections {
            target: app
            onCameraPhotoCaptured: myPage.useNewPhoto(path)
        },
        Connections {
            target: zService
            onSelfProfileDetails: myPage.applyProfile(profile)
            onFullAvatarReady: {
                if (uid === zService.selfUid) zService.downloadAvatar("full_self", url);
            }
            onAvatarReady: {
                if (threadId === "full_self") myPage.avatarPath = localPath;
                else if (threadId === "bg_self") myPage.coverPath = localPath;
                else if (threadId === "self") myPage.selfAvatar = localPath;
            }
            onSelfAvatarChanged: {
                myPage.uploading = false;
                if (ok) {
                    resultToast.body = qsTr("Profile picture updated");
                    myPage.avatarPath = "";
                    refreshTimer.restart();
                } else {
                    resultToast.body = qsTr("Could not change picture") + (error.length > 0 ? ": " + error : "");
                }
                resultToast.show();
            }
        }
    ]
}
