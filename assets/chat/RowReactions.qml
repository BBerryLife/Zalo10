import bb.cascades 1.4
import bb.system 1.0
import QtQuick 1.0

Container {
    id: reactionRow
    horizontalAlignment: HorizontalAlignment.Fill
    visible: rowRoot.kind === "message" && !!(ListItemData.reactions && ListItemData.reactions.length > 0)
    topPadding: 2; bottomPadding: 4
    layout: StackLayout { orientation: LayoutOrientation.LeftToRight }

    Container { preferredWidth: rowRoot.mine ? 18 : 60; minWidth: rowRoot.mine ? 18 : 60; maxWidth: rowRoot.mine ? 18 : 60 }

    Container {
        horizontalAlignment: rowRoot.mine ? HorizontalAlignment.Right : HorizontalAlignment.Left
        maxWidth: rowRoot.bubbleMaxW
        layout: StackLayout { orientation: LayoutOrientation.LeftToRight }

        Container {
            visible: !!(ListItemData.reactions && ListItemData.reactions.length > 0)
            rightMargin: 4
            background: (ListItemData.reactions && ListItemData.reactions[0] && ListItemData.reactions[0].mine)
                ? Color.create("#b8c4cc") : (rowRoot.isDark ? Color.create("#3a3a3a") : Color.create("#e2e2e2"))
            topPadding: 6; bottomPadding: 6; leftPadding: 12; rightPadding: 12
            layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
            gestureHandlers: [ TapHandler { onTapped: {
                if (ListItemData.reactions && ListItemData.reactions[0])
                    rowRoot.ListItem.view.doSendReaction(ListItemData.msgId, ListItemData.cliMsgId, ListItemData.msgType, ListItemData.reactions[0].icon);
            } } ]
            ImageView { imageSource: (ListItemData.reactions && ListItemData.reactions[0]) ? ListItemData.reactions[0].asset : ""; preferredWidth: 32; preferredHeight: 32; verticalAlignment: VerticalAlignment.Center; scalingMethod: ScalingMethod.AspectFit }
            Label { text: (ListItemData.reactions && ListItemData.reactions[0]) ? ("+" + ListItemData.reactions[0].count) : ""; textStyle { fontSize: FontSize.Small; color: rowRoot.isDark ? Color.create("#FFFFFF") : Color.create("#444444") } leftMargin: 4; verticalAlignment: VerticalAlignment.Center }
        }
        Container {
            visible: !!(ListItemData.reactions && ListItemData.reactions.length > 1)
            rightMargin: 4
            background: (ListItemData.reactions && ListItemData.reactions[1] && ListItemData.reactions[1].mine)
                ? Color.create("#b8c4cc") : (rowRoot.isDark ? Color.create("#3a3a3a") : Color.create("#e2e2e2"))
            topPadding: 6; bottomPadding: 6; leftPadding: 12; rightPadding: 12
            layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
            gestureHandlers: [ TapHandler { onTapped: {
                if (ListItemData.reactions && ListItemData.reactions[1])
                    rowRoot.ListItem.view.doSendReaction(ListItemData.msgId, ListItemData.cliMsgId, ListItemData.msgType, ListItemData.reactions[1].icon);
            } } ]
            ImageView { imageSource: (ListItemData.reactions && ListItemData.reactions[1]) ? ListItemData.reactions[1].asset : ""; preferredWidth: 32; preferredHeight: 32; verticalAlignment: VerticalAlignment.Center; scalingMethod: ScalingMethod.AspectFit }
            Label { text: (ListItemData.reactions && ListItemData.reactions[1]) ? ("+" + ListItemData.reactions[1].count) : ""; textStyle { fontSize: FontSize.Small; color: rowRoot.isDark ? Color.create("#FFFFFF") : Color.create("#444444") } leftMargin: 4; verticalAlignment: VerticalAlignment.Center }
        }
        Container {
            visible: !!(ListItemData.reactions && ListItemData.reactions.length > 2)
            rightMargin: 4
            background: (ListItemData.reactions && ListItemData.reactions[2] && ListItemData.reactions[2].mine)
                ? Color.create("#b8c4cc") : (rowRoot.isDark ? Color.create("#3a3a3a") : Color.create("#e2e2e2"))
            topPadding: 6; bottomPadding: 6; leftPadding: 12; rightPadding: 12
            layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
            gestureHandlers: [ TapHandler { onTapped: {
                if (ListItemData.reactions && ListItemData.reactions[2])
                    rowRoot.ListItem.view.doSendReaction(ListItemData.msgId, ListItemData.cliMsgId, ListItemData.msgType, ListItemData.reactions[2].icon);
            } } ]
            ImageView { imageSource: (ListItemData.reactions && ListItemData.reactions[2]) ? ListItemData.reactions[2].asset : ""; preferredWidth: 32; preferredHeight: 32; verticalAlignment: VerticalAlignment.Center; scalingMethod: ScalingMethod.AspectFit }
            Label { text: (ListItemData.reactions && ListItemData.reactions[2]) ? ("+" + ListItemData.reactions[2].count) : ""; textStyle { fontSize: FontSize.Small; color: rowRoot.isDark ? Color.create("#FFFFFF") : Color.create("#444444") } leftMargin: 4; verticalAlignment: VerticalAlignment.Center }
        }
        Container {
            visible: !!(ListItemData.reactions && ListItemData.reactions.length > 3)
            rightMargin: 4
            background: (ListItemData.reactions && ListItemData.reactions[3] && ListItemData.reactions[3].mine)
                ? Color.create("#b8c4cc") : (rowRoot.isDark ? Color.create("#3a3a3a") : Color.create("#e2e2e2"))
            topPadding: 6; bottomPadding: 6; leftPadding: 12; rightPadding: 12
            layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
            gestureHandlers: [ TapHandler { onTapped: {
                if (ListItemData.reactions && ListItemData.reactions[3])
                    rowRoot.ListItem.view.doSendReaction(ListItemData.msgId, ListItemData.cliMsgId, ListItemData.msgType, ListItemData.reactions[3].icon);
            } } ]
            ImageView { imageSource: (ListItemData.reactions && ListItemData.reactions[3]) ? ListItemData.reactions[3].asset : ""; preferredWidth: 32; preferredHeight: 32; verticalAlignment: VerticalAlignment.Center; scalingMethod: ScalingMethod.AspectFit }
            Label { text: (ListItemData.reactions && ListItemData.reactions[3]) ? ("+" + ListItemData.reactions[3].count) : ""; textStyle { fontSize: FontSize.Small; color: rowRoot.isDark ? Color.create("#FFFFFF") : Color.create("#444444") } leftMargin: 4; verticalAlignment: VerticalAlignment.Center }
        }
        Container {
            visible: !!(ListItemData.reactions && ListItemData.reactions.length > 4)
            rightMargin: 4
            background: (ListItemData.reactions && ListItemData.reactions[4] && ListItemData.reactions[4].mine)
                ? Color.create("#b8c4cc") : (rowRoot.isDark ? Color.create("#3a3a3a") : Color.create("#e2e2e2"))
            topPadding: 6; bottomPadding: 6; leftPadding: 12; rightPadding: 12
            layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
            gestureHandlers: [ TapHandler { onTapped: {
                if (ListItemData.reactions && ListItemData.reactions[4])
                    rowRoot.ListItem.view.doSendReaction(ListItemData.msgId, ListItemData.cliMsgId, ListItemData.msgType, ListItemData.reactions[4].icon);
            } } ]
            ImageView { imageSource: (ListItemData.reactions && ListItemData.reactions[4]) ? ListItemData.reactions[4].asset : ""; preferredWidth: 32; preferredHeight: 32; verticalAlignment: VerticalAlignment.Center; scalingMethod: ScalingMethod.AspectFit }
            Label { text: (ListItemData.reactions && ListItemData.reactions[4]) ? ("+" + ListItemData.reactions[4].count) : ""; textStyle { fontSize: FontSize.Small; color: rowRoot.isDark ? Color.create("#FFFFFF") : Color.create("#444444") } leftMargin: 4; verticalAlignment: VerticalAlignment.Center }
        }
        Container {
            visible: !!(ListItemData.reactions && ListItemData.reactions.length > 5)
            background: (ListItemData.reactions && ListItemData.reactions[5] && ListItemData.reactions[5].mine)
                ? Color.create("#b8c4cc") : (rowRoot.isDark ? Color.create("#3a3a3a") : Color.create("#e2e2e2"))
            topPadding: 6; bottomPadding: 6; leftPadding: 12; rightPadding: 12
            layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
            gestureHandlers: [ TapHandler { onTapped: {
                if (ListItemData.reactions && ListItemData.reactions[5])
                    rowRoot.ListItem.view.doSendReaction(ListItemData.msgId, ListItemData.cliMsgId, ListItemData.msgType, ListItemData.reactions[5].icon);
            } } ]
            ImageView { imageSource: (ListItemData.reactions && ListItemData.reactions[5]) ? ListItemData.reactions[5].asset : ""; preferredWidth: 32; preferredHeight: 32; verticalAlignment: VerticalAlignment.Center; scalingMethod: ScalingMethod.AspectFit }
            Label { text: (ListItemData.reactions && ListItemData.reactions[5]) ? ("+" + ListItemData.reactions[5].count) : ""; textStyle { fontSize: FontSize.Small; color: rowRoot.isDark ? Color.create("#FFFFFF") : Color.create("#444444") } leftMargin: 4; verticalAlignment: VerticalAlignment.Center }
        }
    }

    Container { preferredWidth: rowRoot.mine ? 60 : 18; minWidth: rowRoot.mine ? 60 : 18; maxWidth: rowRoot.mine ? 60 : 18 }
}
