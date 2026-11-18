import bb.cascades 1.4
import bb.system 1.0
import QtQuick 1.0
import "lists"

// Feeds tab: a status/news feed laid out like the BBM "Ribbons" screen (header, All / Contacts /
// Channels switch, "What are you thinking?" composer, list of posts).
//
// There is no data source yet: zca-js (and so Zalo10) has no call to read or post the Zalo
// timeline, so the list stays empty and Publish only shows the "under development" dialog.
// When a backend exists, hand the posts to setPosts() as objects of the form
//   { name, localAvatar, kind: "New status", time: "Fri 7:31 PM", text, source: "contacts"|"channels" }
// and the All / Contacts / Channels switch will filter them by `source`.
NavigationPane {
    id: feedsNav
    peekEnabled: false

    property string selfName: ""
    property string selfAvatar: ""
    property int filterIndex: 0              // 0 = All, 1 = Contacts, 2 = Channels
    property variant allPosts: []

    function setPosts(posts) {
        feedsNav.allPosts = posts;
        feedsNav.applyFilter();
    }

    function applyFilter() {
        feedModel.clear();
        var list = feedsNav.allPosts;
        for (var i = 0; i < list.length; i++) {
            var p = list[i];
            if (feedsNav.filterIndex === 0
                    || (feedsNav.filterIndex === 1 && p.source === "contacts")
                    || (feedsNav.filterIndex === 2 && p.source === "channels"))
                feedModel.append(p);
        }
        feedEmpty.visible = (feedModel.size() === 0);
    }

    Page {
        titleBar: ProfileTitleBar {
            selfName:   feedsNav.selfName
            selfAvatar: feedsNav.selfAvatar
        }

        content: Container {
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment:   VerticalAlignment.Fill

            FocusSink {}

            // All / Contacts / Channels
            Container {
                horizontalAlignment: HorizontalAlignment.Fill
                topPadding: ui.du(1.0); bottomPadding: ui.du(1.0)
                leftPadding: ui.du(1.5); rightPadding: ui.du(1.5)
                SegmentedControl {
                    horizontalAlignment: HorizontalAlignment.Fill
                    options: [
                        Option { text: "All" },
                        Option { text: "Contacts" },
                        Option { text: "Channels" }
                    ]
                    onSelectedIndexChanged: {
                        feedsNav.filterIndex = selectedIndex;
                        feedsNav.applyFilter();
                    }
                }
            }

            // Composer: text box + Publish
            Container {
                horizontalAlignment: HorizontalAlignment.Fill
                leftPadding: ui.du(1.5); rightPadding: ui.du(1.5)
                bottomPadding: ui.du(1.0)
                layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                TextField {
                    id: composeField
                    hintText: "What are you thinking?"
                    verticalAlignment: VerticalAlignment.Center
                    layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                    input.submitKey: SubmitKey.Submit
                    input.onSubmitted: { publishDevDialog.show(); }
                }
                Button {
                    text: "Publish"
                    verticalAlignment: VerticalAlignment.Center
                    preferredWidth: ui.du(18)
                    onClicked: { publishDevDialog.show(); }
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
                                                text: ListItemData.kind ? ListItemData.kind : "New status"
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
                                        multiline: true
                                        topMargin: ui.du(1.0)
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
                        text: "No posts yet"
                        horizontalAlignment: HorizontalAlignment.Center
                        textStyle.fontWeight: FontWeight.Bold
                    }
                    Label {
                        text: "Timeline sync is still under development."
                        horizontalAlignment: HorizontalAlignment.Center
                        textStyle.base: SystemDefaults.TextStyles.SmallText
                        textStyle.color: Color.create("#7a7a7a")
                    }
                }
            }
        }

        attachedObjects: [
            ArrayDataModel { id: feedModel },
            InfoDialog {
                id: publishDevDialog
                title: "Publish"
                body: "Posting to your Zalo timeline is still under development."
            }
        ]
    }
}
