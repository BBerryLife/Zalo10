import bb.cascades 1.4
import bb.system 1.0
import QtQuick 1.0

CustomListItem {
    id: friendRow
    dividerVisible: true
    // Long-press menu. Read by the actions below (ListItemData is only safe here, at the row's top level).
    property bool   rowUnread: ListItemData.hasUnread === true
    property string rowTitle:  ListItemData.name || ListItemData.displayName || "Chat"
    contextActions: [
        ActionSet {
            title: friendRow.rowTitle
            ActionItem {
                title: friendRow.rowUnread ? "Mark as Read" : "Mark as Unread"
                imageSource: friendRow.rowUnread ? "asset:///images/ChatsTab/ic_mark_opened.png"
                                          : "asset:///images/ChatsTab/ic_mark_unopened.png"
                onTriggered: { friendRow.ListItem.view.toggleUnread(friendRow.ListItem.indexPath); }
            }
        }
    ]
    property int outState: ListItemData.lastMsgIsMine === true
        ? (ListItemData.lastMsgState || 1) : -1
    Container {
        layout: DockLayout {}
        preferredHeight: ui.du(12.0)

        Container {
            preferredWidth:  ui.du(12.0)
            preferredHeight: ui.du(12.0)
            horizontalAlignment: HorizontalAlignment.Left
            verticalAlignment: VerticalAlignment.Center
            layout: DockLayout {}
            ImageView {
                imageSource: ListItemData.localAvatar ? ListItemData.localAvatar : "asset:///images/ChatsTab/default_avt.png"
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment: VerticalAlignment.Fill
                scalingMethod: ScalingMethod.AspectFill
            }
        }

        Container {
            leftPadding: ui.du(13.0)
            rightPadding: ui.du(2.0)
            verticalAlignment: VerticalAlignment.Top
            layout: StackLayout { orientation: LayoutOrientation.TopToBottom }
            horizontalAlignment: HorizontalAlignment.Fill
            layoutProperties: StackLayoutProperties { spaceQuota: 1 }

            Container {
                layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                horizontalAlignment: HorizontalAlignment.Fill

                // The title Label adds ~2du of left inset of its own, so a 3du slot puts the name
                // at the same x as the last-message text (13du + 5du).
                Container {
                    preferredWidth: ui.du(3.0)
                    minWidth:       ui.du(3.0)
                    verticalAlignment: VerticalAlignment.Center
                    layout: DockLayout {}
                    ImageView {
                        visible: friendRow.ListItem.view.onlineMap[ListItemData.threadId || ListItemData.uid || ""] === true
                        imageSource: "asset:///images/ChatsTab/online_dot.png"
                        preferredWidth:  ui.du(2.6)
                        preferredHeight: ui.du(2.6)
                        // Centre of the dot = centre of the pellet below (4.4du box -> 2.2du from the same left edge).
                        leftMargin: ui.du(0.9)
                        horizontalAlignment: HorizontalAlignment.Left
                        verticalAlignment: VerticalAlignment.Center
                        scalingMethod: ScalingMethod.AspectFit
                    }
                }
                Label {
                    text: ListItemData.name || ListItemData.displayName || "Unknown User"
                    textStyle { base: SystemDefaults.TextStyles.TitleText }
                    verticalAlignment: VerticalAlignment.Center
                    layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                }
                Label {
                    text: ListItemData.lastTime || ""
                    textStyle {
                        base: SystemDefaults.TextStyles.SubtitleText
                        color: Color.Gray
                        fontSize: FontSize.Small
                    }
                    verticalAlignment: VerticalAlignment.Center
                }
            }

            Container {
                topMargin: ui.du(1.0)
                layout: DockLayout {}
                horizontalAlignment: HorizontalAlignment.Fill

                Container {
                    visible: ((ListItemData.lastMessage || ListItemData.lastMsg || "").length > 0)
                    layout: DockLayout {}
                    preferredWidth:  ui.du(4.4)
                    preferredHeight: ui.du(4.4)
                    minWidth:        ui.du(4.4)
                    minHeight:       ui.du(4.4)
                    verticalAlignment: VerticalAlignment.Center
                    horizontalAlignment: HorizontalAlignment.Left
                    ImageView {
                        visible: ListItemData.hasUnread === true
                        imageSource: "asset:///images/ChatView/bubble/message_states/yellow_pellet.png"
                        horizontalAlignment: HorizontalAlignment.Fill
                        verticalAlignment: VerticalAlignment.Fill
                        scalingMethod: ScalingMethod.AspectFit
                    }
                    ImageView {
                        visible: friendRow.outState === -1 && ListItemData.hasUnread !== true
                        imageSource: "asset:///images/ChatView/bubble/message_states/grey_pellet.png"
                        horizontalAlignment: HorizontalAlignment.Fill
                        verticalAlignment: VerticalAlignment.Fill
                        scalingMethod: ScalingMethod.AspectFit
                    }
                    ImageView {
                        visible: friendRow.outState === 0 && ListItemData.hasUnread !== true
                        imageSource: "asset:///images/ChatView/bubble/message_states/sending.png"
                        horizontalAlignment: HorizontalAlignment.Fill
                        verticalAlignment: VerticalAlignment.Fill
                        scalingMethod: ScalingMethod.AspectFit
                    }
                    ImageView {
                        visible: friendRow.outState === 1 && ListItemData.hasUnread !== true
                        imageSource: "asset:///images/ChatView/bubble/message_states/sent.png"
                        horizontalAlignment: HorizontalAlignment.Fill
                        verticalAlignment: VerticalAlignment.Fill
                        scalingMethod: ScalingMethod.AspectFit
                    }
                    ImageView {
                        visible: friendRow.outState === 2 && ListItemData.hasUnread !== true
                        imageSource: "asset:///images/ChatView/bubble/message_states/delivered.png"
                        horizontalAlignment: HorizontalAlignment.Fill
                        verticalAlignment: VerticalAlignment.Fill
                        scalingMethod: ScalingMethod.AspectFit
                    }
                    ImageView {
                        visible: friendRow.outState === 3 && ListItemData.hasUnread !== true
                        imageSource: "asset:///images/ChatView/bubble/message_states/read.png"
                        horizontalAlignment: HorizontalAlignment.Fill
                        verticalAlignment: VerticalAlignment.Fill
                        scalingMethod: ScalingMethod.AspectFit
                    }
                    ImageView {
                        visible: friendRow.outState === 4 && ListItemData.hasUnread !== true
                        imageSource: "asset:///images/ChatView/bubble/message_states/failed.png"
                        horizontalAlignment: HorizontalAlignment.Fill
                        verticalAlignment: VerticalAlignment.Fill
                        scalingMethod: ScalingMethod.AspectFit
                    }
                }
                // Pin icon right under the time, for pinned chats/groups. Dark icon on the light
                // theme, white one on the dark theme.
                ImageView {
                    visible: ListItemData.isPinned === true
                    imageSource: friendRow.ListItem.view.isDark ? "asset:///images/ChatView/ic_pin.png"
                                                         : "asset:///images/ChatView/ic_pin_dark.png"
                    preferredWidth:  ui.du(5.0)
                    preferredHeight: ui.du(5.0)
                    horizontalAlignment: HorizontalAlignment.Right
                    verticalAlignment: VerticalAlignment.Center
                    scalingMethod: ScalingMethod.AspectFit
                }
                Container {
                    horizontalAlignment: HorizontalAlignment.Fill
                    verticalAlignment: VerticalAlignment.Center
                    leftPadding: ui.du(5.0)
                    rightPadding: ListItemData.isPinned === true ? ui.du(6.5) : 0
                    Label {
                        text: friendRow.ListItem.view.lastMessagePreview(ListItemData.lastMessage, ListItemData.lastMsg,
                              ListItemData.lastMsgIsMine, ListItemData.lastSenderName)
                        textStyle {
                            base: SystemDefaults.TextStyles.SubtitleText
                            color: Color.DarkGray
                            fontSize: FontSize.Small
                        }
                        topMargin: 0
                        bottomMargin: 0
                        multiline: false
                    }
                }
            }
        }
    }
}
