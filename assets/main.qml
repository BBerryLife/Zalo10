import bb.cascades 1.4
import bb.system 1.0
import QtQuick 1.0
import "sticker"

TabbedPane {
    id: root
    // Tabs go on the action bar (like CrackBerry): the system fits as many as the screen allows,
    // the rest stay in the overflow. Pages only keep their own actions (Refresh, Search...) in overflow.
    showTabsOnActionBar: true
    sidebarState: SidebarState.VisibleCompact
    
    property string selfName: "Me"
    property string selfAvatar: ""
    property int feedsUnreadCount: 0

    onActiveTabChanged: {
        zService.setFeedsViewing(activeTab === feedsTab);
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
    
    function startLogin() {
        if (!zService.loadSession()) {
            loginSheet.open();
        }
    }

    // My Profile goes on the NavigationPane of the tab whose title bar was tapped.
    function openMyProfile(nav) {
        var page = myProfileDef.createObject();
        if (!page) return;
        page.navigationPane = nav;
        page.selfName   = root.selfName;
        page.selfAvatar = root.selfAvatar;
        nav.push(page);
    }

    onCreationCompleted: {
        aboutSheet.zService = zService;
        if (app.getTosAccepted()) {
            startLogin();
        } else {
            tosTimer.start();
        }
    }
    
    Tab {
        id: chatsTab
        title: "Chats"
        // Subtitle like the old BBM list: "N Active Chats" (singular for 1).
        description: chatsTabContent.onlineChatCount + (chatsTabContent.onlineChatCount === 1 ? " Active Chat" : " Active Chats")
        imageSource: "asset:///images/ic_bbm.png"
        // Native red spark on the tab: right in the menu AND on the navigation bar (a custom icon
        // with a baked-in spark got tinted dark there). Chats stay lit while any chat is unread.
        newContentAvailable: chatsTabContent.unreadCount > 0
        ChatsTab {
            id: chatsTabContent
            onMyProfileRequested: root.openMyProfile(chatsTabContent)
            selfAvatar: root.selfAvatar
        }
    }
    
    Tab {
        id: feedsTab
        title: "Feeds"
        description: root.feedsUnreadCount + (root.feedsUnreadCount === 1 ? " New Update" : " New Updates")
        imageSource: "asset:///images/updates.png"
        newContentAvailable: root.feedsUnreadCount > 0
        FeedsTab {
            id: feedsTabContent
            onMyProfileRequested: root.openMyProfile(feedsTabContent)
            selfName: root.selfName
            selfAvatar: root.selfAvatar
        }
    }

    Tab {
        id: contactsTab
        title: "Contacts"
        description: contactsTabContent.contactCount + (contactsTabContent.contactCount === 1 ? " Contact" : " Contacts")
        imageSource: "asset:///images/ic_contact.png"
        ContactsTab {
            id: contactsTabContent
            onMyProfileRequested: root.openMyProfile(contactsTabContent)
            selfAvatar: root.selfAvatar
        }
    }
    
    Tab {
        id: groupsTab
        title: "Groups"
        description: groupsTabContent.groupCount + (groupsTabContent.groupCount === 1 ? " Group" : " Groups")
        imageSource: "asset:///images/ic_groups_white.png"
        newContentAvailable: groupsTabContent.unreadCount > 0
        GroupsTab {
            id: groupsTabContent
            onMyProfileRequested: root.openMyProfile(groupsTabContent)
            selfAvatar: root.selfAvatar
        }
    }
    
    Tab {
        id: invitesTab
        title: "Invites"
        description: invitesTabContent.inviteCount + (invitesTabContent.inviteCount === 1 ? " New Invite" : " New Invites")
        imageSource: "asset:///images/ic_add_contact.png"
        newContentAvailable: invitesTabContent.inviteCount > 0
        InvitesTab {
            id: invitesTabContent
            onMyProfileRequested: root.openMyProfile(invitesTabContent)
            selfName: root.selfName
            selfAvatar: root.selfAvatar
        }
    }
    
    // Sticker Shop (free sticker packs from BBerryLife.github.io).
    Tab {
        id: shopTab
        title: "Shop"
        description: "Store"
        imageSource: "asset:///images/ic_buy.png"
        StickerShopPage {
            id: shopTabContent
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
        ComponentDefinition {
            id: myProfileDef
            source: "asset:///MyProfilePage.qml"
        },
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
                zService.fetchSelfProfile();
                zService.startFeedsWatch();
                root.feedsUnreadCount = zService.unseenFeedCount();
            }
            onFeedEventsChanged: { root.feedsUnreadCount = unseen; }
            // Own avatar: me-v2 gives the link, the normal avatar downloader caches it under the id "self".
            onSelfProfileReady: {
                if (displayName.length > 0) {
                    root.selfName = displayName;
                    chatsTabContent.selfName    = displayName;
                    groupsTabContent.selfName   = displayName;
                    contactsTabContent.selfName = displayName;
                }
                if (avatarUrl.length > 0) zService.downloadAvatar("self", avatarUrl);
            }
            // Fallback path (getprofiles/v2 for our own uid) when me-v2 is refused.
            onProfileDetailsReady: {
                if (uid === zService.selfUid && profile.avatar && profile.avatar.length > 0)
                    zService.downloadAvatar("self", profile.avatar);
            }
            onAvatarReady: {
                if (threadId === "self") root.selfAvatar = localPath;
            }
        },

        Connections {
            target: app
            onOpenFeedsRequested: { root.activeTab = feedsTab; }
            onOpenThreadRequested: {
                // A Hub "Zalo10 Feeds" item must open the Feeds tab, never a chat (its id is
                // "feed:updates"; some invoke paths only deliver the part after the colon).
                if (threadId.indexOf("feed") === 0 || threadId === "updates") {
                    root.activeTab = feedsTab;
                    return;
                }
                if (isGroup) {
                    root.activeTab = groupsTab;
                    groupsTabContent.openThread(threadId, isGroup);
                } else {
                    root.activeTab = chatsTab;
                    chatsTabContent.openThread(threadId, isGroup);
                }
            }
            onEventCreated: {
                eventResultDialog.body = success
                    ? "Added event to today's calendar"
                    : ("Cannot create event: " + error);
                eventResultDialog.show();
            }
        },

        Timer {
            id: tosTimer
            interval: 600
            repeat: false
            onTriggered: { tosDialog.show(); }
        },

        SystemDialog {
            id: tosDialog
            title: "Terms of Service"
            body: "Informal Disclaimer:\nZalo10 is an independent, non-profit personal project. It has no official affiliation or connection with Zalo (VNG).\n\nAccount Risks:\nUsing a third-party application based on an emulation library may violate Zalo's terms of service. Please consider using a secondary account. The author takes no responsibility if your Zalo account is suspended or banned.\n\nData Policy:\nThe application runs directly on your device and serves strictly as a display interface. It does not store or forward your personal data or login credentials to any third party.\n\nMaintenance and Issues:\nThe application is provided \"As-Is\". Features and functionality may be interrupted at any time if Zalo updates or changes their system."
            confirmButton.label: "OK"
            cancelButton.label: "Cancel"
            onFinished: {
                if (value === SystemUiResult.ConfirmButtonSelection) {
                    app.setTosAccepted(true);
                    root.startLogin();
                } else {
                    app.quitApp();
                }
            }
        },

        InfoDialog {
            id: eventResultDialog
            title: "Zalo10"
        }
    ]
}