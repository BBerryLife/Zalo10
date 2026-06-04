// ProfileView.qml
import bb.cascades 1.4
import QtQuick 1.0

Page {
    id: profilePage

    property string contactId:    ""
    property string contactName:  ""
    property string avatarPath:   ""
    property string bgAvatarPath: ""
    property string avatarUrl:    ""  // fallback nếu chưa download
    property string bgAvatarUrl:  ""

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
                    text: profilePage.contactName.length > 0 ? profilePage.contactName : "Profile"
                    verticalAlignment: VerticalAlignment.Center
                    horizontalAlignment: HorizontalAlignment.Left
                    textStyle { color: Color.White; base: SystemDefaults.TextStyles.TitleText; fontWeight: FontWeight.Bold }
                    topMargin: 0; bottomMargin: 0
                }
            }
        }
    }

    onCreationCompleted: {
        // Trigger download avatar nếu chưa có localPath
        if (profilePage.avatarPath.length === 0 && profilePage.avatarUrl.length > 0)
            zService.downloadAvatar(profilePage.contactId, profilePage.avatarUrl)
        // Trigger download bgavatar — dùng prefix "bg_" để phân biệt với avatar thường
        if (profilePage.bgAvatarPath.length === 0 && profilePage.bgAvatarUrl.length > 0)
            zService.downloadAvatar("bg_" + profilePage.contactId, profilePage.bgAvatarUrl)
    }

    actions: [
        ActionItem {
            title: "Send Message"
            imageSource: "asset:///images/ic_bbm.png"
            ActionBar.placement: ActionBarPlacement.OnBar
            onTriggered: {
                var chatPage = chatDef.createObject()
                if (!chatPage) return
                chatPage.threadId   = profilePage.contactId
                chatPage.threadName = profilePage.contactName
                chatPage.isGroup    = false
                navigationPane.push(chatPage)
            }
        }
    ]

    ScrollView {
        scrollViewProperties {
            scrollMode: ScrollMode.Vertical
        }
        horizontalAlignment: HorizontalAlignment.Fill
        verticalAlignment: VerticalAlignment.Fill

        Container {
            layout: StackLayout { orientation: LayoutOrientation.TopToBottom }
            horizontalAlignment: HorizontalAlignment.Fill

            // ── Ảnh nền ──────────────────────────────────────────
            Container {
                preferredHeight: ui.du(28)
                horizontalAlignment: HorizontalAlignment.Fill
                layout: DockLayout {}
                background: Color.create("#1a1a2e")

                // Bg avatar
                ImageView {
                    id: bgImage
                    imageSource: {
                        if (profilePage.bgAvatarPath.length > 0) return profilePage.bgAvatarPath
                        return "asset:///images/blank.png"
                    }
                    horizontalAlignment: HorizontalAlignment.Fill
                    verticalAlignment: VerticalAlignment.Fill
                    scalingMethod: ScalingMethod.AspectFill
                }

                // Gradient overlay phía dưới để chữ dễ đọc
                Container {
                    verticalAlignment: VerticalAlignment.Bottom
                    horizontalAlignment: HorizontalAlignment.Fill
                    preferredHeight: ui.du(14)
                    background: Color.create("#99000000")
                }

                // Avatar tròn đè lên góc dưới-giữa
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
                            imageSource: {
                                if (profilePage.avatarPath.length > 0) return profilePage.avatarPath
                                return "asset:///images/blank.png"
                            }
                            horizontalAlignment: HorizontalAlignment.Fill
                            verticalAlignment: VerticalAlignment.Fill
                            scalingMethod: ScalingMethod.AspectFill
                        }
                    }
                }
            }

            // ── Tên ──────────────────────────────────────────────
            Container {
                topPadding: ui.du(2)
                bottomPadding: ui.du(1)
                horizontalAlignment: HorizontalAlignment.Center

                Label {
                    text: profilePage.contactName
                    textStyle {
                        base: SystemDefaults.TextStyles.BigText
                        fontWeight: FontWeight.Bold
                        textAlign: TextAlign.Center
                    }
                    horizontalAlignment: HorizontalAlignment.Center
                    multiline: false
                }
            }

            // ── Divider ───────────────────────────────────────────
            Divider { topMargin: ui.du(1); bottomMargin: ui.du(1) }

            // ── Nút hành động ─────────────────────────────────────
            Container {
                layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                horizontalAlignment: HorizontalAlignment.Center
                topPadding: ui.du(1)
                bottomPadding: ui.du(2)

            }

            // ── Thông tin thêm (placeholder cho sau) ─────────────
            Container {
                leftPadding: ui.du(3)
                rightPadding: ui.du(3)
                topPadding: ui.du(1)

                Label {
                    text: "Zalo ID"
                    textStyle {
                        base: SystemDefaults.TextStyles.SmallText
                        color: Color.Gray
                    }
                }
                Label {
                    text: profilePage.contactId
                    textStyle {
                        base: SystemDefaults.TextStyles.BodyText
                    }
                    topMargin: ui.du(0.3)
                    bottomMargin: ui.du(2)
                }
            }
        }
    }

    // Lắng nghe avatarReady để update ảnh nếu chưa load kịp
    attachedObjects: [
        ComponentDefinition {
            id: chatDef
            source: "asset:///ChatView.qml"
        },
        Connections {
            target: zService
            onAvatarReady: {
                if (threadId === profilePage.contactId && profilePage.avatarPath.length === 0)
                    profilePage.avatarPath = localPath
                if (threadId === ("bg_" + profilePage.contactId) && profilePage.bgAvatarPath.length === 0)
                    profilePage.bgAvatarPath = localPath
            }
        }
    ]

    // navigationPane: được set từ bên ngoài khi push, hoặc lookup từ parent chain
    property variant navigationPane: null
}
