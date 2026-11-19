import bb.cascades 1.4
import bb.system 1.0
import QtQuick 1.0
import "lists"

// Feeds tab, laid out like the BBM "Ribbons" screen. Zalo has no timeline API, so the feed shows
// what Zalo10 notices itself: contacts changing their profile picture, cover photo, bio or
// birthday (ZaloService compares each profile with the snapshot it stored earlier, see
// ZaloService_Contacts_Feeds.cpp). Events arrive as { id, uid, name, kind, oldValue, newValue,
// ts, seen, avatar } with kind = avatar | cover | bio | dob.
NavigationPane {
    id: feedsNav
    peekEnabled: false

    property string selfName: ""
    property string selfAvatar: ""
    property int filterIndex: 0              // 0 = All, 1 = Info (bio, birthday), 2 = Photos
    property variant allPosts: []

    function pad(n) { return n < 10 ? "0" + n : "" + n; }

    function formatTime(ts) {
        var t = Number(ts);
        if (!t) return "";
        var d = new Date(t);
        var diff = new Date().getTime() - t;
        if (diff < 60000) return "Just now";
        if (diff < 3600000) return Math.floor(diff / 60000) + " min ago";
        var hm = pad(d.getHours()) + ":" + pad(d.getMinutes());
        var now = new Date();
        if (now.getFullYear() === d.getFullYear() && now.getMonth() === d.getMonth()
                && now.getDate() === d.getDate()) return hm;
        if (diff < 6 * 86400000) {
            var days = ["Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"];
            return days[d.getDay()] + " " + hm;
        }
        return pad(d.getDate()) + "/" + pad(d.getMonth() + 1) + "/" + d.getFullYear();
    }

    function kindLabel(kind) {
        if (kind === "avatar") return "Changed profile picture";
        if (kind === "cover")  return "Changed cover photo";
        if (kind === "bio")    return "Updated bio";
        if (kind === "dob")    return "Updated birthday";
        return "Updated profile";
    }

    function eventText(ev) {
        if (ev.kind === "bio") return ev.newValue.length > 0 ? ev.newValue : "(bio removed)";
        if (ev.kind === "dob") {
            return ev.oldValue.length > 0
                ? "Birthday: " + ev.oldValue + " \u2192 " + ev.newValue
                : "Birthday: " + ev.newValue;
        }
        return "";
    }

    function loadEvents() {
        var evs = zService.getFeedEvents(200);
        var posts = [];
        for (var i = 0; i < evs.length; i++) {
            var ev = evs[i];
            var isPhoto = (ev.kind === "avatar" || ev.kind === "cover");
            posts.push({
                id: ev.id, uid: ev.uid, name: ev.name,
                kind: ev.kind, kindText: kindLabel(ev.kind),
                time: formatTime(ev.ts), text: eventText(ev),
                photoUrl: isPhoto ? ev.newValue : "", avatarUrl: ev.avatar,
                localAvatar: "", localPhoto: "", hasPhoto: false
            });
        }
        feedsNav.allPosts = posts;
        feedsNav.applyFilter();
        // Pictures last: cached ones answer at once through onAvatarReady and fill the rows.
        var asked = {};
        for (var j = 0; j < posts.length; j++) {
            var p = posts[j];
            // Own cache keys ("feedava_"/"feedimg_"): the chat lists use the friend list's link for the
            // same contact, and two different links under one key would keep overwriting each other.
            if (p.avatarUrl && p.avatarUrl.length > 0 && !asked[p.uid]) {
                asked[p.uid] = true;
                zService.downloadAvatar("feedava_" + p.uid, p.avatarUrl);
            }
            if (p.photoUrl.length > 0) zService.downloadAvatar("feedimg_" + p.id, p.photoUrl);
        }
    }

    function applyFilter() {
        feedModel.clear();
        var list = feedsNav.allPosts;
        for (var i = 0; i < list.length; i++) {
            var p = list[i];
            var isPhoto = (p.kind === "avatar" || p.kind === "cover");
            if (feedsNav.filterIndex === 0
                    || (feedsNav.filterIndex === 1 && !isPhoto)
                    || (feedsNav.filterIndex === 2 && isPhoto))
                feedModel.append(p);
        }
        feedEmpty.visible = (feedModel.size() === 0);
    }

    // Pictures arrive under "feedimg_<eventId>" (the photo of a picture/cover event) or
    // "feedava_<uid>" (the contact's avatar in the row). A profile-picture event shows its photo
    // as the row avatar too.
    function applyPicture(id, path) {
        var isPhoto = (id.indexOf("feedimg_") === 0);
        var isAva = (id.indexOf("feedava_") === 0);
        if (!isPhoto && !isAva) return;
        var key = isPhoto ? id.substring(8) : id.substring(8);
        var i;
        var list = feedsNav.allPosts;
        for (i = 0; i < list.length; i++) {
            if (isPhoto && list[i].id === key) {
                list[i].localPhoto = path; list[i].hasPhoto = true;
                if (list[i].kind === "avatar") list[i].localAvatar = path;
            } else if (isAva && list[i].uid === key && list[i].kind !== "avatar") {
                list[i].localAvatar = path;
            }
        }
        for (i = 0; i < feedModel.size(); i++) {
            var item = feedModel.value(i);
            var hit = false;
            if (isPhoto && item.id === key) {
                item.localPhoto = path; item.hasPhoto = true;
                if (item.kind === "avatar") item.localAvatar = path;
                hit = true;
            } else if (isAva && item.uid === key && item.kind !== "avatar") {
                item.localAvatar = path;
                hit = true;
            }
            if (hit) feedModel.replace(i, item);
        }
    }

    onCreationCompleted: {
        zService.startFeedsWatch();
        loadEvents();
    }

    Page {
        actions: [
            DeleteActionItem {
                title: "Clear Feeds"
                ActionBar.placement: ActionBarPlacement.InOverflow
                onTriggered: { clearFeedsDialog.show(); }
            }
        ]
        titleBar: ProfileTitleBar {
            selfName:   feedsNav.selfName
            selfAvatar: feedsNav.selfAvatar
        }

        content: Container {
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment:   VerticalAlignment.Fill

            FocusSink {}

            // All / Info / Photos
            Container {
                horizontalAlignment: HorizontalAlignment.Fill
                topPadding: ui.du(1.0); bottomPadding: ui.du(1.0)
                leftPadding: ui.du(1.5); rightPadding: ui.du(1.5)
                SegmentedControl {
                    horizontalAlignment: HorizontalAlignment.Fill
                    options: [
                        Option { text: "All" },
                        Option { text: "Info" },
                        Option { text: "Photos" }
                    ]
                    onSelectedIndexChanged: {
                        feedsNav.filterIndex = selectedIndex;
                        feedsNav.applyFilter();
                    }
                }
            }

            Container {
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment:   VerticalAlignment.Fill
                layout: DockLayout {}
                layoutProperties: StackLayoutProperties { spaceQuota: 1 }

                ListView {
                    horizontalAlignment: HorizontalAlignment.Fill
                    verticalAlignment:   VerticalAlignment.Fill
                    dataModel: feedModel

                    function itemType(data, indexPath) { return "post"; }

                    listItemComponents: [
                        ListItemComponent {
                            type: "post"
                            CustomListItem {
                                dividerVisible: true
                                Container {
                                    horizontalAlignment: HorizontalAlignment.Fill
                                    topPadding: ui.du(1.5); bottomPadding: ui.du(1.5)
                                    leftPadding: ui.du(1.5); rightPadding: ui.du(1.5)

                                    Container {
                                        horizontalAlignment: HorizontalAlignment.Fill
                                        layout: StackLayout { orientation: LayoutOrientation.LeftToRight }

                                        ImageView {
                                            preferredWidth:  ui.du(9.0)
                                            preferredHeight: ui.du(9.0)
                                            minWidth:        ui.du(9.0)
                                            verticalAlignment: VerticalAlignment.Top
                                            scalingMethod: ScalingMethod.AspectFill
                                            imageSource: ListItemData.localAvatar ? ListItemData.localAvatar : "asset:///images/ChatsTab/default_avt.png"
                                        }

                                        Container {
                                            leftMargin: ui.du(1.5)
                                            verticalAlignment: VerticalAlignment.Center
                                            layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                                            Label {
                                                text: ListItemData.name ? ListItemData.name : ""
                                                textStyle.fontWeight: FontWeight.Bold
                                                bottomMargin: 0
                                            }
                                            Label {
                                                text: ListItemData.kindText ? ListItemData.kindText : ""
                                                textStyle.base: SystemDefaults.TextStyles.SmallText
                                                textStyle.color: Color.create("#7a7a7a")
                                                topMargin: 0
                                            }
                                        }

                                        Label {
                                            text: ListItemData.time ? ListItemData.time : ""
                                            verticalAlignment: VerticalAlignment.Top
                                            textStyle.base: SystemDefaults.TextStyles.SmallText
                                            textStyle.color: Color.create("#7a7a7a")
                                        }
                                    }

                                    Label {
                                        text: ListItemData.text ? ListItemData.text : ""
                                        visible: text.length > 0
                                        multiline: true
                                        topMargin: ui.du(1.0)
                                    }

                                    ImageView {
                                        visible: ListItemData.hasPhoto === true
                                        horizontalAlignment: HorizontalAlignment.Left
                                        preferredWidth:  ui.du(24)
                                        preferredHeight: ui.du(24)
                                        topMargin: ui.du(1.0)
                                        scalingMethod: ScalingMethod.AspectFill
                                        imageSource: ListItemData.localPhoto ? ListItemData.localPhoto : ""
                                    }
                                }
                            }
                        }
                    ]
                }

                Container {
                    id: feedEmpty
                    visible: true
                    horizontalAlignment: HorizontalAlignment.Center
                    verticalAlignment:   VerticalAlignment.Center
                    Label {
                        text: "No updates yet"
                        horizontalAlignment: HorizontalAlignment.Center
                        textStyle.fontWeight: FontWeight.Bold
                    }
                    Label {
                        text: "Changes to your contacts' profile pictures, covers, bios and birthdays show up here."
                        multiline: true
                        horizontalAlignment: HorizontalAlignment.Center
                        textStyle.base: SystemDefaults.TextStyles.SmallText
                        textStyle.color: Color.create("#7a7a7a")
                    }
                }
            }
        }

        attachedObjects: [
            ArrayDataModel { id: feedModel },
            ConfirmDialog {
                id: clearFeedsDialog
                title: "Clear Feeds"
                body: "Remove all feed updates?"
                confirmLabel: "Clear"
                onConfirmed: { zService.clearFeedEvents(); }
            },
            Connections {
                target: zService
                onFeedEventsChanged: { feedsNav.loadEvents(); }
                onAvatarReady: { feedsNav.applyPicture(threadId, localPath); }
            }
        ]
    }
}
