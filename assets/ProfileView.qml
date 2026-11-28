import bb.cascades 1.4
import QtQuick 1.0

Page {
    id: profilePage

    property string contactId:    ""
    property string contactName:  ""
    property string avatarPath:   ""
    property string bgAvatarPath: ""
    property string avatarUrl:    ""
    property string bgAvatarUrl:  ""
    property string selfName:     ""
    // From getprofiles/v2; an empty value hides its row.
    property string bio:          ""
    property string genderText:   ""
    property string birthday:     ""
    property string lastActive:   ""
    property string joined:       ""
    property string bizLabel:     ""
    property string bizDesc:      ""
    property string bizAddr:      ""
    property string bizWebsite:   ""
    property string bizEmail:     ""

    property double lastAvatarTap: 0

    function showImage(path) {
        if (!path || path.length === 0) return
        imgSheet.imageSource = path
        imgSheet.open()
    }

    function pad(n) { return (n < 10 ? "0" : "") + n }
    function fmtDate(d) {
        return pad(d.getDate()) + "/" + pad(d.getMonth() + 1) + "/" + d.getFullYear()
    }
    function fmtDateTime(d) { return fmtDate(d) + " " + pad(d.getHours()) + ":" + pad(d.getMinutes()) }
    function applyDetails(p) {
        profilePage.bio = (p.status || "").replace(/\s+/g, " ").replace(/^\s+|\s+$/g, "")
        profilePage.genderText = (p.gender === 0) ? "Male" : ((p.gender === 1) ? "Female" : "")
        profilePage.birthday = p.sdob || ""
        if (p.lastActionTime && p.lastActionTime > 0) {
            // Zalo may return seconds or milliseconds.
            var la = Number(p.lastActionTime)
            profilePage.lastActive = fmtDateTime(new Date(la < 100000000000 ? la * 1000 : la))
        }
        if (p.createdTs && p.createdTs > 0)
            profilePage.joined = fmtDate(new Date(p.createdTs * 1000))
        if (p.bizPkg && typeof p.bizPkg.label === "object" && p.bizPkg.label)
            profilePage.bizLabel = p.bizPkg.label.VI || p.bizPkg.label.EN || ""
        // Business account: also fetch description, address, website, email.
        if (profilePage.bizLabel.length > 0) zService.fetchBizAccount(profilePage.contactId)
        // Zalo returns the cover in "cover" when "bgavatar" is empty.
        var coverUrl = p.cover || p.bgavatar || ""
        if (coverUrl.length > 0 && profilePage.bgAvatarPath.length === 0) {
            profilePage.bgAvatarUrl = coverUrl
            zService.downloadAvatar("bg_" + profilePage.contactId, coverUrl)
        }
    }

    titleBar: TitleBar {
        scrollBehavior: TitleBarScrollBehavior.Sticky
        kind: TitleBarKind.FreeForm
        kindProperties: FreeFormTitleBarKindProperties {
            content: Container {
                background: Color.create("#2575fc")
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment:   VerticalAlignment.Fill
                layout: DockLayout {}
                leftPadding: ui.du(2.5)

                Label {
                    text: profilePage.contactName.length > 0 ? profilePage.contactName : qsTr("Profile")
                    verticalAlignment: VerticalAlignment.Center
                    horizontalAlignment: HorizontalAlignment.Left
                    textStyle { color: Color.White; base: SystemDefaults.TextStyles.TitleText; fontWeight: FontWeight.Bold }
                    topMargin: 0; bottomMargin: 0
                }
            }
        }
    }

    property bool detailsRequested: false

    function loadDetails() {
        if (detailsRequested || contactId.length === 0) return
        detailsRequested = true
        if (avatarPath.length === 0 && avatarUrl.length > 0)
            zService.downloadAvatar(contactId, avatarUrl)
        if (bgAvatarPath.length === 0 && bgAvatarUrl.length > 0)
            zService.downloadAvatar("bg_" + contactId, bgAvatarUrl)
        zService.fetchProfileDetails(contactId)
        zService.fetchFullAvatar(contactId)
    }

    // Deferred: the opener assigns contactId before avatarUrl etc., so wait until all are set.
    onContactIdChanged: loadTimer.restart()
    onCreationCompleted: loadTimer.restart()

    actions: [
        ActionItem {
            title: qsTr("Send Message")
            imageSource: "asset:///images/ContactsTab/ProfileView/ic_bbm.png"
            ActionBar.placement: ActionBarPlacement.OnBar
            onTriggered: {
                var chatPage = chatDef.createObject()
                if (!chatPage) return
                chatPage.threadId   = profilePage.contactId
                chatPage.threadName = profilePage.contactName
                chatPage.isGroup    = false
                chatPage.avatarUrl  = profilePage.avatarPath.length > 0 ? profilePage.avatarPath : profilePage.avatarUrl
                chatPage.selfName   = profilePage.selfName
                chatPage.startChat()
                popWatcher.target = chatPage
                navigationPane.push(chatPage)
            }
        }
    ]

    ScrollView {
        scrollViewProperties { scrollMode: ScrollMode.Vertical }
        horizontalAlignment: HorizontalAlignment.Fill
        verticalAlignment: VerticalAlignment.Fill

        Container {
            layout: StackLayout { orientation: LayoutOrientation.TopToBottom }
            horizontalAlignment: HorizontalAlignment.Fill

            Container {
                preferredHeight: ui.du(28)
                horizontalAlignment: HorizontalAlignment.Fill
                layout: DockLayout {}
                background: Color.create("#1a1a2e")
                gestureHandlers: TapHandler {
                    onTapped: {
                        if ((new Date().getTime() - profilePage.lastAvatarTap) > 400)
                            profilePage.showImage(profilePage.bgAvatarPath)
                    }
                }

                ImageView {
                    id: bgImage
                    imageSource: profilePage.bgAvatarPath.length > 0
                        ? profilePage.bgAvatarPath
                        : "asset:///images/ContactsTab/ProfileView/BB_Pattern_Vivid.jpg"
                    horizontalAlignment: HorizontalAlignment.Fill
                    verticalAlignment: VerticalAlignment.Fill
                    scalingMethod: ScalingMethod.AspectFill
                }

                Container {
                    verticalAlignment: VerticalAlignment.Bottom
                    horizontalAlignment: HorizontalAlignment.Fill
                    preferredHeight: ui.du(14)
                    background: Color.create("#99000000")
                }

                Container {
                    verticalAlignment: VerticalAlignment.Bottom
                    horizontalAlignment: HorizontalAlignment.Center
                    bottomPadding: ui.du(1)
                    layout: DockLayout {}

                    Container {
                        preferredWidth: ui.du(16)
                        preferredHeight: ui.du(16)
                        background: Color.create("#2575fc")
                        layout: DockLayout {}

                        ImageView {
                            id: avatarImage
                            gestureHandlers: TapHandler {
                                onTapped: {
                                    profilePage.lastAvatarTap = new Date().getTime()
                                    profilePage.showImage(profilePage.avatarPath)
                                }
                            }
                            imageSource: profilePage.avatarPath.length > 0
                                ? profilePage.avatarPath
                                : "asset:///images/ChatView/default_caller.png"
                            horizontalAlignment: HorizontalAlignment.Fill
                            verticalAlignment: VerticalAlignment.Fill
                            scalingMethod: ScalingMethod.AspectFill
                        }
                    }
                }
            }

            Container {
                topPadding: ui.du(2)
                bottomPadding: ui.du(1)
                horizontalAlignment: HorizontalAlignment.Center

                Label {
                    text: profilePage.contactName
                    textStyle {
                        base: SystemDefaults.TextStyles.BigText
                        textAlign: TextAlign.Center
                    }
                    horizontalAlignment: HorizontalAlignment.Center
                    multiline: false
                }
            }

            Divider { topMargin: ui.du(1); bottomMargin: ui.du(1) }

            Container {
                layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                horizontalAlignment: HorizontalAlignment.Center
                topPadding: ui.du(1)
                bottomPadding: ui.du(2)
            }



            Container {
                visible: profilePage.bio.length > 0
                leftPadding: ui.du(3)
                rightPadding: ui.du(3)
                topPadding: ui.du(1)
                Label {
                    text: qsTr("Status")
                    textStyle { base: SystemDefaults.TextStyles.SmallText; color: Color.Gray }
                }
                Label {
                    text: profilePage.bio
                    multiline: true
                    textStyle { base: SystemDefaults.TextStyles.BodyText }
                    topMargin: ui.du(0.3)
                    bottomMargin: ui.du(1)
                }
            }

            Container {
                visible: profilePage.bizLabel.length > 0
                leftPadding: ui.du(3)
                rightPadding: ui.du(3)
                topPadding: ui.du(1)
                Label {
                    text: qsTr("Account")
                    textStyle { base: SystemDefaults.TextStyles.SmallText; color: Color.Gray }
                }
                Label {
                    text: profilePage.bizLabel
                    multiline: true
                    textStyle { base: SystemDefaults.TextStyles.BodyText }
                    topMargin: ui.du(0.3)
                    bottomMargin: ui.du(1)
                }
            }

            Container {
                visible: profilePage.genderText.length > 0
                leftPadding: ui.du(3)
                rightPadding: ui.du(3)
                topPadding: ui.du(1)
                Label {
                    text: qsTr("Gender")
                    textStyle { base: SystemDefaults.TextStyles.SmallText; color: Color.Gray }
                }
                Label {
                    text: profilePage.genderText
                    multiline: true
                    textStyle { base: SystemDefaults.TextStyles.BodyText }
                    topMargin: ui.du(0.3)
                    bottomMargin: ui.du(1)
                }
            }

            Container {
                visible: profilePage.birthday.length > 0
                leftPadding: ui.du(3)
                rightPadding: ui.du(3)
                topPadding: ui.du(1)
                Label {
                    text: qsTr("Birthday")
                    textStyle { base: SystemDefaults.TextStyles.SmallText; color: Color.Gray }
                }
                Label {
                    text: profilePage.birthday
                    multiline: true
                    textStyle { base: SystemDefaults.TextStyles.BodyText }
                    topMargin: ui.du(0.3)
                    bottomMargin: ui.du(1)
                }
            }

            Container {
                visible: profilePage.lastActive.length > 0
                leftPadding: ui.du(3)
                rightPadding: ui.du(3)
                topPadding: ui.du(1)
                Label {
                    text: qsTr("Last active")
                    textStyle { base: SystemDefaults.TextStyles.SmallText; color: Color.Gray }
                }
                Label {
                    text: profilePage.lastActive
                    multiline: true
                    textStyle { base: SystemDefaults.TextStyles.BodyText }
                    topMargin: ui.du(0.3)
                    bottomMargin: ui.du(1)
                }
            }

            Container {
                visible: profilePage.joined.length > 0
                leftPadding: ui.du(3)
                rightPadding: ui.du(3)
                topPadding: ui.du(1)
                Label {
                    text: qsTr("Joined Zalo")
                    textStyle { base: SystemDefaults.TextStyles.SmallText; color: Color.Gray }
                }
                Label {
                    text: profilePage.joined
                    multiline: true
                    textStyle { base: SystemDefaults.TextStyles.BodyText }
                    topMargin: ui.du(0.3)
                    bottomMargin: ui.du(1)
                }
            }


            Container {
                visible: profilePage.bizDesc.length > 0
                leftPadding: ui.du(3)
                rightPadding: ui.du(3)
                topPadding: ui.du(1)
                Label {
                    text: qsTr("Business info")
                    textStyle { base: SystemDefaults.TextStyles.SmallText; color: Color.Gray }
                }
                Label {
                    text: profilePage.bizDesc
                    multiline: true
                    textStyle { base: SystemDefaults.TextStyles.BodyText }
                    topMargin: ui.du(0.3)
                    bottomMargin: ui.du(1)
                }
            }

            Container {
                visible: profilePage.bizAddr.length > 0
                leftPadding: ui.du(3)
                rightPadding: ui.du(3)
                topPadding: ui.du(1)
                Label {
                    text: qsTr("Address")
                    textStyle { base: SystemDefaults.TextStyles.SmallText; color: Color.Gray }
                }
                Label {
                    text: profilePage.bizAddr
                    multiline: true
                    textStyle { base: SystemDefaults.TextStyles.BodyText }
                    topMargin: ui.du(0.3)
                    bottomMargin: ui.du(1)
                }
            }

            Container {
                visible: profilePage.bizWebsite.length > 0
                leftPadding: ui.du(3)
                rightPadding: ui.du(3)
                topPadding: ui.du(1)
                Label {
                    text: qsTr("Website")
                    textStyle { base: SystemDefaults.TextStyles.SmallText; color: Color.Gray }
                }
                Label {
                    text: profilePage.bizWebsite
                    multiline: true
                    textStyle { base: SystemDefaults.TextStyles.BodyText }
                    topMargin: ui.du(0.3)
                    bottomMargin: ui.du(1)
                }
            }

            Container {
                visible: profilePage.bizEmail.length > 0
                leftPadding: ui.du(3)
                rightPadding: ui.du(3)
                topPadding: ui.du(1)
                Label {
                    text: qsTr("Email")
                    textStyle { base: SystemDefaults.TextStyles.SmallText; color: Color.Gray }
                }
                Label {
                    text: profilePage.bizEmail
                    multiline: true
                    textStyle { base: SystemDefaults.TextStyles.BodyText }
                    topMargin: ui.du(0.3)
                    bottomMargin: ui.du(1)
                }
            }


            Container {
                leftPadding: ui.du(3)
                rightPadding: ui.du(3)
                topPadding: ui.du(1)

                Label {
                    text: qsTr("Zalo ID")
                    textStyle {
                        base: SystemDefaults.TextStyles.SmallText
                        color: Color.Gray
                    }
                }
                Label {
                    text: profilePage.contactId
                    textStyle { base: SystemDefaults.TextStyles.BodyText }
                    topMargin: ui.du(0.3)
                    bottomMargin: ui.du(2)
                }
            }
        }
    }

    attachedObjects: [
        Timer {
            id: loadTimer
            interval: 50
            repeat: false
            onTriggered: profilePage.loadDetails()
        },
        Connections {
            id: popWatcher
            target: null
            onPopRequestedChanged: {
                if (target && target.popRequested) {
                    target.popRequested = false
                    if (navigationPane) navigationPane.pop()
                }
            }
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
        ComponentDefinition {
            id: chatDef
            source: "asset:///ChatView.qml"
        },
        Connections {
            target: zService
            onBizAccountReady: {
                if (uid !== profilePage.contactId) return
                var d = (biz.desc || "").replace(/\s+/g, " ").replace(/^\s+|\s+$/g, "")
                // Many shops reuse the bio as description; don't show it twice.
                profilePage.bizDesc    = (d === profilePage.bio) ? "" : d
                profilePage.bizAddr    = biz.addr || ""
                profilePage.bizWebsite = biz.website || ""
                profilePage.bizEmail   = biz.email || ""
            }
            onFullAvatarReady: {
                if (uid === profilePage.contactId) zService.downloadAvatar("full_" + profilePage.contactId, url)
            }
            onProfileDetailsReady: {
                if (uid === profilePage.contactId) profilePage.applyDetails(profile)
            }
            onAvatarReady: {
                // Full-size avatar replaces the 120px one.
                if (threadId === ("full_" + profilePage.contactId)) profilePage.avatarPath = localPath
                if (threadId === profilePage.contactId && profilePage.avatarPath.length === 0)
                    profilePage.avatarPath = localPath
                if (threadId === ("bg_" + profilePage.contactId) && profilePage.bgAvatarPath.length === 0)
                    profilePage.bgAvatarPath = localPath
            }
        }
    ]

    property variant navigationPane: null
}
