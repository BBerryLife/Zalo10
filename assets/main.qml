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
                title: "Feedback"
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
        aboutSheet.zService = zService;
        if (!zService.loadSession()) {
            loginSheet.open();
        }
    }
    
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
    
    Tab {
        id: contactsTab
        title: "Contacts"
        description: "Friends"
        imageSource: "asset:///images/ic_contact.png"
        ContactsTab {
            id: contactsTabContent
        }
    }
    
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
        Sheet {
            id: loginSheet
            property bool needsQR: false
            onOpened: {
                if (needsQR || !zService.loggedIn) {
                    needsQR = false;
                    zService.startQRLogin();
                }
            }
            LoginView {
                onLoginSuccessful: { loginSheet.close(); }
            }
        },
        
        SettingsSheet { id: settingsSheet },
        AboutSheet    { id: aboutSheet },
        
        Connections {
            target: zService
            onSessionExpired: { loginSheet.needsQR = true; loginSheet.open(); }
            onLoginFailed:    { loginSheet.needsQR = true; loginSheet.open(); }
            onLoginSuccess: {
                if (typeof displayName !== "undefined" && displayName.length > 0) {
                    root.selfName = displayName;
                    chatsTabContent.selfName    = displayName;
                    groupsTabContent.selfName   = displayName;
                    contactsTabContent.selfName = displayName;
                }
            }
        },

        Connections {
            target: app
            onOpenThreadRequested: {
                // Called from C++ ApplicationUI::onInvoked when Hub notification is tapped.
                // threadId: conversation id, isGroup: true=group chat, false=DM
                console.log("openThreadRequested: threadId=" + threadId + " isGroup=" + isGroup);
                if (isGroup) {
                    root.activeTab = groupsTab;
                    groupsTabContent.openThread(threadId, isGroup);
                } else {
                    root.activeTab = chatsTab;
                    chatsTabContent.openThread(threadId, isGroup);
                }
            }
            // Result of app.createTodayEvent() (ChatView.qml's "Create
            // today's event" overflow action) — handled here at the root
            // rather than inside ChatView itself because app/ApplicationUI
            // is a single global instance shared by every Tab, so wiring
            // this per-ChatView-instance would either miss the signal (if
            // that ChatView isn't the active tab when it fires) or risk
            // showing the same result dialog multiple times if more than
            // one ChatView happened to be alive. One handler here covers
            // every caller.
            onEventCreated: {
                eventResultDialog.body = success
                    ? "Da them su kien vao lich hom nay."
                    : ("Khong the tao su kien: " + error);
                eventResultDialog.show();
            }
        },

        InfoDialog {
            id: eventResultDialog
            title: "Zalo10"
        }
    ]
}