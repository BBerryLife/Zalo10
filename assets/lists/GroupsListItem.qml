import bb.cascades 1.4
import bb.system 1.0
import QtQuick 1.0

CustomListItem {
    id: groupRow
    dividerVisible: true
    property int outState: ListItemData.lastMsgIsMine === true
        ? (ListItemData.lastMsgState || 1) : -1
    Container {
        layout: DockLayout {}
        preferredHeight: ui.du(12.0)

        ImageView {
            imageSource: ListItemData.localAvatar ? ListItemData.localAvatar : "asset:///images/GroupsTab/default_group.png"
            preferredWidth: ui.du(12.0)
            preferredHeight: ui.du(12.0)
            horizontalAlignment: HorizontalAlignment.Left
            verticalAlignment: VerticalAlignment.Center
            scalingMethod: ScalingMethod.AspectFill
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

                Label {
                    text: ListItemData.name || "Unknown Group"
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
                    visible: ((ListItemData.lastMessage || "").length > 0)
                    layout: DockLayout {}
                    preferredWidth:  ui.du(4.4)
                    preferredHeight: ui.du(4.4)
                    minWidth:        ui.du(4.4)
                    minHeight:       ui.du(4.4)
                    verticalAlignment: VerticalAlignment.Center
                    horizontalAlignment: HorizontalAlignment.Left
                    ImageView {
                        visible: groupRow.outState === -1 && ListItemData.hasUnread === true
                        imageSource: "asset:///images/ChatView/bubble/message_states/yellow_pellet.png"
                        horizontalAlignment: HorizontalAlignment.Fill
                        verticalAlignment: VerticalAlignment.Fill
                        scalingMethod: ScalingMethod.AspectFit
                    }
                    ImageView {
                        visible: groupRow.outState === -1 && ListItemData.hasUnread !== true
                        imageSource: "asset:///images/ChatView/bubble/message_states/grey_pellet.png"
                        horizontalAlignment: HorizontalAlignment.Fill
                        verticalAlignment: VerticalAlignment.Fill
                        scalingMethod: ScalingMethod.AspectFit
                    }
                    ImageView {
                        visible: groupRow.outState === 0
                        imageSource: "asset:///images/ChatView/bubble/message_states/sending.png"
                        horizontalAlignment: HorizontalAlignment.Fill
                        verticalAlignment: VerticalAlignment.Fill
                        scalingMethod: ScalingMethod.AspectFit
                    }
                    ImageView {
                        visible: groupRow.outState === 1
                        imageSource: "asset:///images/ChatView/bubble/message_states/sent.png"
                        horizontalAlignment: HorizontalAlignment.Fill
                        verticalAlignment: VerticalAlignment.Fill
                        scalingMethod: ScalingMethod.AspectFit
                    }
                    ImageView {
                        visible: groupRow.outState === 2
                        imageSource: "asset:///images/ChatView/bubble/message_states/delivered.png"
                        horizontalAlignment: HorizontalAlignment.Fill
                        verticalAlignment: VerticalAlignment.Fill
                        scalingMethod: ScalingMethod.AspectFit
                    }
                    ImageView {
                        visible: groupRow.outState === 3
                        imageSource: "asset:///images/ChatView/bubble/message_states/read.png"
                        horizontalAlignment: HorizontalAlignment.Fill
                        verticalAlignment: VerticalAlignment.Fill
                        scalingMethod: ScalingMethod.AspectFit
                    }
                    ImageView {
                        visible: groupRow.outState === 4
                        imageSource: "asset:///images/ChatView/bubble/message_states/failed.png"
                        horizontalAlignment: HorizontalAlignment.Fill
                        verticalAlignment: VerticalAlignment.Fill
                        scalingMethod: ScalingMethod.AspectFit
                    }
                }
                Container {
                    horizontalAlignment: HorizontalAlignment.Fill
                    verticalAlignment: VerticalAlignment.Center
                    leftPadding: ((ListItemData.lastMessage || "").length > 0) ? ui.du(5.0) : 0
                    Label {
                        text: groupRow.ListItem.view.lastMessagePreview(ListItemData.lastMessage,
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
