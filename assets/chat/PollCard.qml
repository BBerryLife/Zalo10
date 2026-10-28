import bb.cascades 1.4
import bb.system 1.0
import QtQuick 1.0

Container {
    id: pollCardRoot
    visible: rowRoot.kind === "poll"
    horizontalAlignment: HorizontalAlignment.Fill
    topPadding: 12; bottomPadding: 14; leftPadding: 20; rightPadding: 20

    Container {
        horizontalAlignment: HorizontalAlignment.Fill
        layout: StackLayout { orientation: LayoutOrientation.TopToBottom }
        background: rowRoot.isDark ? Color.create("#26323d") : Color.White
        topPadding: ui.du(0.8); bottomPadding: ui.du(1.4)
        leftPadding: ui.du(1.0); rightPadding: ui.du(1.0)

        Container {
            horizontalAlignment: HorizontalAlignment.Fill
            layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
            bottomMargin: ui.du(0.4)
            Container {
                layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                layout: DockLayout {}
                minHeight: ui.du(5.6)
                maxHeight: (ListItemData.pollQuestion || "").length <= 16 ? ui.du(5.6) : 100000
                Label {
                    horizontalAlignment: HorizontalAlignment.Left
                    verticalAlignment: VerticalAlignment.Center
                    text: ListItemData.pollQuestion || ""
                    multiline: true
                    textStyle { base: SystemDefaults.TextStyles.BigText
                                color: rowRoot.isDark ? Color.White : Color.Black }
                }
            }
            Label {
                visible: (ListItemData.pollNumVote || 0) > 0
                verticalAlignment: VerticalAlignment.Center
                text: String(ListItemData.pollNumVote || 0) + " voted"
                textStyle { color: Color.create("#2575fc"); fontSize: FontSize.Small }
                gestureHandlers: [ TapHandler { onTapped: {
                    rowRoot.ListItem.view.viewPollVotersProxy(ListItemData.pollId);
                } } ]
            }
        }

        Container {
            visible: !!(ListItemData.pollOptions && ListItemData.pollOptions.length > 0)
            horizontalAlignment: HorizontalAlignment.Fill
            layout: DockLayout {}
            background: rowRoot.isDark ? Color.create("#33404a") : Color.create("#ececec")
            bottomMargin: ui.du(0.7)
            minHeight: ui.du(5.6)
            maxHeight: ((ListItemData.pollOptions && ListItemData.pollOptions[0]) ? (ListItemData.pollOptions[0].content || "") : "").length <= 26 ? ui.du(5.6) : 100000
            Container {
                visible: rowRoot.pollFillPct(ListItemData.pollOptions, ListItemData.pollNumVote, 0) > 0
                horizontalAlignment: HorizontalAlignment.Left
                verticalAlignment: VerticalAlignment.Fill
                preferredWidth: rowRoot.pollInnerW * rowRoot.pollFillPct(ListItemData.pollOptions, ListItemData.pollNumVote, 0) / 100
                background: rowRoot.isDark ? Color.create("#2c5f94") : Color.create("#a8cdf8")
            }
            Container {
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment: VerticalAlignment.Center
                layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                leftPadding: ui.du(1.2); rightPadding: ui.du(1.2)
                Label {
                    layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                    verticalAlignment: VerticalAlignment.Center
                    text: ((ListItemData.pollOptions && ListItemData.pollOptions[0]) ? (ListItemData.pollOptions[0].content || "") : "")
                    multiline: true
                    textStyle { fontSize: FontSize.Large; color: rowRoot.isDark ? Color.White : Color.Black }
                }
                Label {
                    verticalAlignment: VerticalAlignment.Center
                    text: (ListItemData.pollOptions && ListItemData.pollOptions[0]) ? String(ListItemData.pollOptions[0].votes || 0) : ""
                    textStyle { fontSize: FontSize.Large; color: Color.Gray }
                }
            }
        }
        Container {
            visible: !!(ListItemData.pollOptions && ListItemData.pollOptions.length > 1)
            horizontalAlignment: HorizontalAlignment.Fill
            layout: DockLayout {}
            background: rowRoot.isDark ? Color.create("#33404a") : Color.create("#ececec")
            bottomMargin: ui.du(0.7)
            minHeight: ui.du(5.6)
            maxHeight: ((ListItemData.pollOptions && ListItemData.pollOptions[1]) ? (ListItemData.pollOptions[1].content || "") : "").length <= 26 ? ui.du(5.6) : 100000
            Container {
                visible: rowRoot.pollFillPct(ListItemData.pollOptions, ListItemData.pollNumVote, 1) > 0
                horizontalAlignment: HorizontalAlignment.Left
                verticalAlignment: VerticalAlignment.Fill
                preferredWidth: rowRoot.pollInnerW * rowRoot.pollFillPct(ListItemData.pollOptions, ListItemData.pollNumVote, 1) / 100
                background: rowRoot.isDark ? Color.create("#2c5f94") : Color.create("#a8cdf8")
            }
            Container {
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment: VerticalAlignment.Center
                layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                leftPadding: ui.du(1.2); rightPadding: ui.du(1.2)
                Label {
                    layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                    verticalAlignment: VerticalAlignment.Center
                    text: ((ListItemData.pollOptions && ListItemData.pollOptions[1]) ? (ListItemData.pollOptions[1].content || "") : "")
                    multiline: true
                    textStyle { fontSize: FontSize.Large; color: rowRoot.isDark ? Color.White : Color.Black }
                }
                Label {
                    verticalAlignment: VerticalAlignment.Center
                    text: (ListItemData.pollOptions && ListItemData.pollOptions[1]) ? String(ListItemData.pollOptions[1].votes || 0) : ""
                    textStyle { fontSize: FontSize.Large; color: Color.Gray }
                }
            }
        }
        Container {
            visible: !!(ListItemData.pollOptions && ListItemData.pollOptions.length > 2)
            horizontalAlignment: HorizontalAlignment.Fill
            layout: DockLayout {}
            background: rowRoot.isDark ? Color.create("#33404a") : Color.create("#ececec")
            bottomMargin: ui.du(0.7)
            minHeight: ui.du(5.6)
            maxHeight: ((ListItemData.pollOptions && ListItemData.pollOptions[2]) ? (ListItemData.pollOptions[2].content || "") : "").length <= 26 ? ui.du(5.6) : 100000
            Container {
                visible: rowRoot.pollFillPct(ListItemData.pollOptions, ListItemData.pollNumVote, 2) > 0
                horizontalAlignment: HorizontalAlignment.Left
                verticalAlignment: VerticalAlignment.Fill
                preferredWidth: rowRoot.pollInnerW * rowRoot.pollFillPct(ListItemData.pollOptions, ListItemData.pollNumVote, 2) / 100
                background: rowRoot.isDark ? Color.create("#2c5f94") : Color.create("#a8cdf8")
            }
            Container {
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment: VerticalAlignment.Center
                layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                leftPadding: ui.du(1.2); rightPadding: ui.du(1.2)
                Label {
                    layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                    verticalAlignment: VerticalAlignment.Center
                    text: ((ListItemData.pollOptions && ListItemData.pollOptions[2]) ? (ListItemData.pollOptions[2].content || "") : "")
                    multiline: true
                    textStyle { fontSize: FontSize.Large; color: rowRoot.isDark ? Color.White : Color.Black }
                }
                Label {
                    verticalAlignment: VerticalAlignment.Center
                    text: (ListItemData.pollOptions && ListItemData.pollOptions[2]) ? String(ListItemData.pollOptions[2].votes || 0) : ""
                    textStyle { fontSize: FontSize.Large; color: Color.Gray }
                }
            }
        }
        Container {
            visible: !!(ListItemData.pollOptions && ListItemData.pollOptions.length > 3)
            horizontalAlignment: HorizontalAlignment.Fill
            layout: DockLayout {}
            background: rowRoot.isDark ? Color.create("#33404a") : Color.create("#ececec")
            bottomMargin: ui.du(0.7)
            minHeight: ui.du(5.6)
            maxHeight: ((ListItemData.pollOptions && ListItemData.pollOptions[3]) ? (ListItemData.pollOptions[3].content || "") : "").length <= 26 ? ui.du(5.6) : 100000
            Container {
                visible: rowRoot.pollFillPct(ListItemData.pollOptions, ListItemData.pollNumVote, 3) > 0
                horizontalAlignment: HorizontalAlignment.Left
                verticalAlignment: VerticalAlignment.Fill
                preferredWidth: rowRoot.pollInnerW * rowRoot.pollFillPct(ListItemData.pollOptions, ListItemData.pollNumVote, 3) / 100
                background: rowRoot.isDark ? Color.create("#2c5f94") : Color.create("#a8cdf8")
            }
            Container {
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment: VerticalAlignment.Center
                layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                leftPadding: ui.du(1.2); rightPadding: ui.du(1.2)
                Label {
                    layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                    verticalAlignment: VerticalAlignment.Center
                    text: ((ListItemData.pollOptions && ListItemData.pollOptions[3]) ? (ListItemData.pollOptions[3].content || "") : "")
                    multiline: true
                    textStyle { fontSize: FontSize.Large; color: rowRoot.isDark ? Color.White : Color.Black }
                }
                Label {
                    verticalAlignment: VerticalAlignment.Center
                    text: (ListItemData.pollOptions && ListItemData.pollOptions[3]) ? String(ListItemData.pollOptions[3].votes || 0) : ""
                    textStyle { fontSize: FontSize.Large; color: Color.Gray }
                }
            }
        }
        Container {
            visible: !!(ListItemData.pollOptions && ListItemData.pollOptions.length > 4)
            horizontalAlignment: HorizontalAlignment.Fill
            layout: DockLayout {}
            background: rowRoot.isDark ? Color.create("#33404a") : Color.create("#ececec")
            bottomMargin: ui.du(0.7)
            minHeight: ui.du(5.6)
            maxHeight: ((ListItemData.pollOptions && ListItemData.pollOptions[4]) ? (ListItemData.pollOptions[4].content || "") : "").length <= 26 ? ui.du(5.6) : 100000
            Container {
                visible: rowRoot.pollFillPct(ListItemData.pollOptions, ListItemData.pollNumVote, 4) > 0
                horizontalAlignment: HorizontalAlignment.Left
                verticalAlignment: VerticalAlignment.Fill
                preferredWidth: rowRoot.pollInnerW * rowRoot.pollFillPct(ListItemData.pollOptions, ListItemData.pollNumVote, 4) / 100
                background: rowRoot.isDark ? Color.create("#2c5f94") : Color.create("#a8cdf8")
            }
            Container {
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment: VerticalAlignment.Center
                layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                leftPadding: ui.du(1.2); rightPadding: ui.du(1.2)
                Label {
                    layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                    verticalAlignment: VerticalAlignment.Center
                    text: ((ListItemData.pollOptions && ListItemData.pollOptions[4]) ? (ListItemData.pollOptions[4].content || "") : "")
                    multiline: true
                    textStyle { fontSize: FontSize.Large; color: rowRoot.isDark ? Color.White : Color.Black }
                }
                Label {
                    verticalAlignment: VerticalAlignment.Center
                    text: (ListItemData.pollOptions && ListItemData.pollOptions[4]) ? String(ListItemData.pollOptions[4].votes || 0) : ""
                    textStyle { fontSize: FontSize.Large; color: Color.Gray }
                }
            }
        }
        Container {
            visible: !!(ListItemData.pollOptions && ListItemData.pollOptions.length > 5)
            horizontalAlignment: HorizontalAlignment.Fill
            layout: DockLayout {}
            background: rowRoot.isDark ? Color.create("#33404a") : Color.create("#ececec")
            bottomMargin: ui.du(0.7)
            minHeight: ui.du(5.6)
            maxHeight: ((ListItemData.pollOptions && ListItemData.pollOptions[5]) ? (ListItemData.pollOptions[5].content || "") : "").length <= 26 ? ui.du(5.6) : 100000
            Container {
                visible: rowRoot.pollFillPct(ListItemData.pollOptions, ListItemData.pollNumVote, 5) > 0
                horizontalAlignment: HorizontalAlignment.Left
                verticalAlignment: VerticalAlignment.Fill
                preferredWidth: rowRoot.pollInnerW * rowRoot.pollFillPct(ListItemData.pollOptions, ListItemData.pollNumVote, 5) / 100
                background: rowRoot.isDark ? Color.create("#2c5f94") : Color.create("#a8cdf8")
            }
            Container {
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment: VerticalAlignment.Center
                layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                leftPadding: ui.du(1.2); rightPadding: ui.du(1.2)
                Label {
                    layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                    verticalAlignment: VerticalAlignment.Center
                    text: ((ListItemData.pollOptions && ListItemData.pollOptions[5]) ? (ListItemData.pollOptions[5].content || "") : "")
                    multiline: true
                    textStyle { fontSize: FontSize.Large; color: rowRoot.isDark ? Color.White : Color.Black }
                }
                Label {
                    verticalAlignment: VerticalAlignment.Center
                    text: (ListItemData.pollOptions && ListItemData.pollOptions[5]) ? String(ListItemData.pollOptions[5].votes || 0) : ""
                    textStyle { fontSize: FontSize.Large; color: Color.Gray }
                }
            }
        }

        Container {
            visible: !!(ListItemData.pollOptions && ListItemData.pollOptions.length > 0)
            horizontalAlignment: HorizontalAlignment.Fill
            topMargin: ui.du(0.6)
            preferredHeight: ui.du(5.8)
            layout: DockLayout {}
            background: rowRoot.isDark ? Color.create("#2a3f57") : Color.create("#dbe9fb")
            gestureHandlers: [ TapHandler { onTapped: {
                rowRoot.ListItem.view.openPollVoteProxy(ListItemData.pollId);
            } } ]
            Label {
                horizontalAlignment: HorizontalAlignment.Center
                verticalAlignment: VerticalAlignment.Center
                text: "Vote"
                textStyle { color: Color.create("#2575fc"); fontSize: FontSize.Large; fontWeight: FontWeight.Bold }
            }
        }
    }
}
