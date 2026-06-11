import bb.cascades 1.4
import bb.system 1.0
import QtQuick 1.0

TabbedPane {
    id: root
    showTabsOnActionBar: false
    sidebarState: SidebarState.VisibleCompact

    property string selfName: "Me"
    property int chatsUnreadCount: 0
    property int groupsUnreadCount: 0

    onChatsUnreadCountChanged: {
        chatsTab.title = chatsUnreadCount > 0
            ? "Chats (" + chatsUnreadCount + ")"
            : "Chats";
    }
    onGroupsUnreadCountChanged: {
        groupsTab.title = groupsUnreadCount > 0
            ? "Groups (" + groupsUnreadCount + ")"
            : "Groups";
    }
    onActiveTabChanged: {
        if (activeTab === chatsTab)  root.chatsUnreadCount  = 0;
        if (activeTab === groupsTab) root.groupsUnreadCount = 0;
    }

    Menu.definition: MenuDefinition {
        actions: [
            ActionItem {
                title: "About"
                imageSource: "asset:///images/ic_info.png"
                onTriggered: { aboutSheet.open(); }
            },
            ActionItem {
                title: "Settings"
                imageSource: "asset:///images/ic_settings.png"
                onTriggered: { settingsSheet.open(); }
            },
            ActionItem {
                title: "Email"
                imageSource: "asset:///images/ic_mail.png"
                onTriggered: { app.invokeEmail("Berrylife2025@gmail.com", "Zalo10 Feedback"); }
            }
        ]
    }

    function formatTime(timestamp) {
        if (!timestamp || timestamp === "") return "";
        var date = new Date(timestamp * 1);
        var now  = new Date();
        if (date.toDateString() === now.toDateString()) {
            var h = date.getHours(), m = date.getMinutes();
            var ampm = h >= 12 ? "PM" : "AM";
            h = h % 12 || 12;
            return h + ":" + (m < 10 ? "0" : "") + m + " " + ampm;
        }
        if ((now - date) < 7 * 24 * 60 * 60 * 1000) {
            return ["Sun","Mon","Tue","Wed","Thu","Fri","Sat"][date.getDay()];
        }
        var mon = ["Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"][date.getMonth()];
        return mon + " " + date.getDate();
    }

    onCreationCompleted: {
        // app.splashImage() đọc /pps/services/display/display0 → đúng mọi thiết bị
        splashImg.imageSource = app.splashImage();
        splashDialog.open();
        splashTimer.start();
    }



    // Chats tab
    Tab {
        id: chatsTab
        title: "Chats"
        description: "Messages"
        imageSource: "asset:///images/ic_bbm.png"
        ChatsTab {
            id: chatsTabContent
            onOnUnreadMessage: {
                if (root.activeTab !== chatsTab)
                    root.chatsUnreadCount++;
            }
        }
    }

    // Contacts tab
    Tab {
        id: contactsTab
        title: "Contacts"
        description: "Friends"
        imageSource: "asset:///images/ic_contact.png"
        ContactsTab {
            id: contactsTabContent
        }
    }

    // Groups tab
    Tab {
        id: groupsTab
        title: "Groups"
        description: "Group chats"
        imageSource: "asset:///images/ic_groups_white.png"
        GroupsTab {
            id: groupsTabContent
            onOnUnreadMessage: {
                if (root.activeTab !== groupsTab)
                    root.groupsUnreadCount++;
            }
        }
    }

    // Invites tab
    Tab {
        id: invitesTab
        title: "Invites"
        description: "Friend requests"
        imageSource: "asset:///images/ic_add_contact.png"
        InvitesTab {
            id: invitesTabContent
        }
    }

    attachedObjects: [
        Dialog {
            id: splashDialog
            Container {
                id: splashRoot
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment: VerticalAlignment.Fill
                layout: DockLayout {}
                ImageView {
                    id: splashImg
                    horizontalAlignment: HorizontalAlignment.Fill
                    verticalAlignment: VerticalAlignment.Fill
                    scalingMethod: ScalingMethod.Fill
                    imageSource: "asset:///images/splash.png"
                }
                // Dùng width binding: khi Dialog mở, Container có width thực
                // Q10/Q20/Passport = 720px = 90du; Z10 portrait = 768px = 96du
                onWidthChanged: {
                    if (width <= 0) return;
                    var duWidth = Math.round(width / ui.du(1));
                    if (width == height) {
                        // Màn vuông (Q10/Q20/Passport)
                        splashImg.imageSource = "asset:///images/splash720.png";
                    } else if (width > height) {
                        // Landscape
                        splashImg.imageSource = "asset:///images/splashLS.png";
                    } else {
                        // Portrait (Z10/Z30/Z3/Leap)
                        splashImg.imageSource = "asset:///images/splash.png";
                    }
                }
            }
        },

        Timer {
            id: splashTimer
            interval: 2000
            repeat: false
            onTriggered: {
                splashDialog.close();
                if (!zService.loadSession()) {
                    loginSheet.open();
                }
                // If loadSession() == true: refreshSessionKey() runs async
                // and emits loginSuccess when done — QML handlers fetch then.
            }
        },

        Sheet {
            id: loginSheet
            LoginView {
                onLoginSuccessful: { loginSheet.close(); }
            }
        },

        SettingsSheet { id: settingsSheet },
        AboutSheet    { id: aboutSheet },

        Connections {
            target: zService
            onSessionExpired: { loginSheet.open(); }
            onLoginFailed:    { loginSheet.open(); }
            onLoginSuccess: {
                if (typeof displayName !== "undefined" && displayName.length > 0) {
                    root.selfName = displayName;
                    chatsTabContent.selfName    = displayName;
                    groupsTabContent.selfName   = displayName;
                    contactsTabContent.selfName = displayName;
                }
            }
        },

        SystemToast {
            id: sessionToast
            body: "Phiên đăng nhập đã hết hạn. Vào Settings → Logout để đăng nhập lại."
            position: SystemUiPosition.MiddleCenter
        }
    ]
}
