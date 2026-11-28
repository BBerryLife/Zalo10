import bb.cascades 1.4
import bb.system 1.0
import QtQuick 1.0

CustomListItem {
    id: boardRow
    highlightAppearance: HighlightAppearance.None
    dividerVisible: true

    Container {
        horizontalAlignment: HorizontalAlignment.Fill
        leftPadding: ui.du(1.0); rightPadding: ui.du(1.0)
        topPadding: ui.du(1.2); bottomPadding: ui.du(1.2)
        layout: StackLayout { orientation: LayoutOrientation.TopToBottom }

        Container {
            horizontalAlignment: HorizontalAlignment.Fill
            layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
            bottomMargin: ui.du(0.6)

            Label {
                layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                text: boardRow.ListItem.view.creatorLabel(ListItemData.creatorId)
                verticalAlignment: VerticalAlignment.Center
                textStyle { fontWeight: FontWeight.Bold; fontSize: FontSize.Medium }
            }
            Label {
                text: ListItemData.boardType === "poll" ? qsTr("Poll")
                    : ListItemData.boardType === "note" ? "Note"
                    : "Pinned Message"
                verticalAlignment: VerticalAlignment.Center
                textStyle { color: Color.Gray; fontSize: FontSize.Small }
            }
        }

        Container {
            visible: ListItemData.boardType === "poll"
            horizontalAlignment: HorizontalAlignment.Fill
            leftPadding: ui.du(0.8); rightPadding: ui.du(0.8)
            layout: StackLayout { orientation: LayoutOrientation.TopToBottom }

            Label {
                text: ListItemData.question || ""
                multiline: true
                textStyle { fontWeight: FontWeight.Bold; fontSize: FontSize.Medium }
            }
            Label {
                text: ListItemData.allowMultiChoices ? qsTr("Choose multiple options") : qsTr("Choose one option")
                textStyle { color: Color.Gray; fontSize: FontSize.XSmall }
                topMargin: ui.du(0.2); bottomMargin: ui.du(0.6)
            }
            Label {
                text: (ListItemData.numVote || 0) + qsTr(" member") + ((ListItemData.numVote === 1) ? "" : "s") + qsTr(" voted")
                textStyle { color: Color.create("#2575fc"); fontSize: FontSize.XSmall }
                bottomMargin: ui.du(0.6)
                gestureHandlers: [ TapHandler { onTapped: {
                    boardRow.ListItem.view.openPollVoters(ListItemData.id);
                } } ]
            }
            Container {
                visible: !!(ListItemData.options && ListItemData.options.length > 0)
                horizontalAlignment: HorizontalAlignment.Fill
                background: (ListItemData.options && ListItemData.options[0] && ListItemData.options[0].voted) ? (boardRow.ListItem.view.isDark ? Color.create("#2a4560") : Color.create("#cfe3fa")) : (boardRow.ListItem.view.isDark ? Color.create("#33404a") : Color.create("#f0f0f0"))
                topPadding: ui.du(0.8); bottomPadding: ui.du(0.8)
                leftPadding: ui.du(1); rightPadding: ui.du(1)
                bottomMargin: ui.du(0.5)
                layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                gestureHandlers: [
                    TapHandler {
                        onTapped: {
                            if (ListItemData.closed) return;
                            boardRow.ListItem.view.doVoteOption(ListItemData.id, ListItemData.options[0].optionId,
                                !!ListItemData.allowMultiChoices, ListItemData.options);
                        }
                    }
                ]
                Label {
                    layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                    text: (ListItemData.options && ListItemData.options[0]) ? (ListItemData.options[0].content || "") : ""
                    multiline: true
                }
                Label {
                    text: (ListItemData.options && ListItemData.options[0]) ? String(ListItemData.options[0].votes || 0) : ""
                    textStyle { color: Color.Gray }
                }
            }
            Container {
                visible: !!(ListItemData.options && ListItemData.options.length > 1)
                horizontalAlignment: HorizontalAlignment.Fill
                background: (ListItemData.options && ListItemData.options[1] && ListItemData.options[1].voted) ? (boardRow.ListItem.view.isDark ? Color.create("#2a4560") : Color.create("#cfe3fa")) : (boardRow.ListItem.view.isDark ? Color.create("#33404a") : Color.create("#f0f0f0"))
                topPadding: ui.du(0.8); bottomPadding: ui.du(0.8)
                leftPadding: ui.du(1); rightPadding: ui.du(1)
                bottomMargin: ui.du(0.5)
                layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                gestureHandlers: [
                    TapHandler {
                        onTapped: {
                            if (ListItemData.closed) return;
                            boardRow.ListItem.view.doVoteOption(ListItemData.id, ListItemData.options[1].optionId,
                                !!ListItemData.allowMultiChoices, ListItemData.options);
                        }
                    }
                ]
                Label {
                    layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                    text: (ListItemData.options && ListItemData.options[1]) ? (ListItemData.options[1].content || "") : ""
                    multiline: true
                }
                Label {
                    text: (ListItemData.options && ListItemData.options[1]) ? String(ListItemData.options[1].votes || 0) : ""
                    textStyle { color: Color.Gray }
                }
            }
            Container {
                visible: !!(ListItemData.options && ListItemData.options.length > 2)
                horizontalAlignment: HorizontalAlignment.Fill
                background: (ListItemData.options && ListItemData.options[2] && ListItemData.options[2].voted) ? (boardRow.ListItem.view.isDark ? Color.create("#2a4560") : Color.create("#cfe3fa")) : (boardRow.ListItem.view.isDark ? Color.create("#33404a") : Color.create("#f0f0f0"))
                topPadding: ui.du(0.8); bottomPadding: ui.du(0.8)
                leftPadding: ui.du(1); rightPadding: ui.du(1)
                bottomMargin: ui.du(0.5)
                layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                gestureHandlers: [
                    TapHandler {
                        onTapped: {
                            if (ListItemData.closed) return;
                            boardRow.ListItem.view.doVoteOption(ListItemData.id, ListItemData.options[2].optionId,
                                !!ListItemData.allowMultiChoices, ListItemData.options);
                        }
                    }
                ]
                Label {
                    layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                    text: (ListItemData.options && ListItemData.options[2]) ? (ListItemData.options[2].content || "") : ""
                    multiline: true
                }
                Label {
                    text: (ListItemData.options && ListItemData.options[2]) ? String(ListItemData.options[2].votes || 0) : ""
                    textStyle { color: Color.Gray }
                }
            }
            Container {
                visible: !!(ListItemData.options && ListItemData.options.length > 3)
                horizontalAlignment: HorizontalAlignment.Fill
                background: (ListItemData.options && ListItemData.options[3] && ListItemData.options[3].voted) ? (boardRow.ListItem.view.isDark ? Color.create("#2a4560") : Color.create("#cfe3fa")) : (boardRow.ListItem.view.isDark ? Color.create("#33404a") : Color.create("#f0f0f0"))
                topPadding: ui.du(0.8); bottomPadding: ui.du(0.8)
                leftPadding: ui.du(1); rightPadding: ui.du(1)
                bottomMargin: ui.du(0.5)
                layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                gestureHandlers: [
                    TapHandler {
                        onTapped: {
                            if (ListItemData.closed) return;
                            boardRow.ListItem.view.doVoteOption(ListItemData.id, ListItemData.options[3].optionId,
                                !!ListItemData.allowMultiChoices, ListItemData.options);
                        }
                    }
                ]
                Label {
                    layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                    text: (ListItemData.options && ListItemData.options[3]) ? (ListItemData.options[3].content || "") : ""
                    multiline: true
                }
                Label {
                    text: (ListItemData.options && ListItemData.options[3]) ? String(ListItemData.options[3].votes || 0) : ""
                    textStyle { color: Color.Gray }
                }
            }
            Container {
                visible: !!(ListItemData.options && ListItemData.options.length > 4)
                horizontalAlignment: HorizontalAlignment.Fill
                background: (ListItemData.options && ListItemData.options[4] && ListItemData.options[4].voted) ? (boardRow.ListItem.view.isDark ? Color.create("#2a4560") : Color.create("#cfe3fa")) : (boardRow.ListItem.view.isDark ? Color.create("#33404a") : Color.create("#f0f0f0"))
                topPadding: ui.du(0.8); bottomPadding: ui.du(0.8)
                leftPadding: ui.du(1); rightPadding: ui.du(1)
                bottomMargin: ui.du(0.5)
                layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                gestureHandlers: [
                    TapHandler {
                        onTapped: {
                            if (ListItemData.closed) return;
                            boardRow.ListItem.view.doVoteOption(ListItemData.id, ListItemData.options[4].optionId,
                                !!ListItemData.allowMultiChoices, ListItemData.options);
                        }
                    }
                ]
                Label {
                    layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                    text: (ListItemData.options && ListItemData.options[4]) ? (ListItemData.options[4].content || "") : ""
                    multiline: true
                }
                Label {
                    text: (ListItemData.options && ListItemData.options[4]) ? String(ListItemData.options[4].votes || 0) : ""
                    textStyle { color: Color.Gray }
                }
            }
            Container {
                visible: !!(ListItemData.options && ListItemData.options.length > 5)
                horizontalAlignment: HorizontalAlignment.Fill
                background: (ListItemData.options && ListItemData.options[5] && ListItemData.options[5].voted) ? (boardRow.ListItem.view.isDark ? Color.create("#2a4560") : Color.create("#cfe3fa")) : (boardRow.ListItem.view.isDark ? Color.create("#33404a") : Color.create("#f0f0f0"))
                topPadding: ui.du(0.8); bottomPadding: ui.du(0.8)
                leftPadding: ui.du(1); rightPadding: ui.du(1)
                bottomMargin: ui.du(0.5)
                layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                gestureHandlers: [
                    TapHandler {
                        onTapped: {
                            if (ListItemData.closed) return;
                            boardRow.ListItem.view.doVoteOption(ListItemData.id, ListItemData.options[5].optionId,
                                !!ListItemData.allowMultiChoices, ListItemData.options);
                        }
                    }
                ]
                Label {
                    layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                    text: (ListItemData.options && ListItemData.options[5]) ? (ListItemData.options[5].content || "") : ""
                    multiline: true
                }
                Label {
                    text: (ListItemData.options && ListItemData.options[5]) ? String(ListItemData.options[5].votes || 0) : ""
                    textStyle { color: Color.Gray }
                }
            }
            Container {
                visible: !!(ListItemData.options && ListItemData.options.length > 0) && !ListItemData.closed
                horizontalAlignment: HorizontalAlignment.Fill
                topMargin: ui.du(0.6)
                preferredHeight: ui.du(5.8)
                layout: DockLayout {}
                background: boardRow.ListItem.view.isDark ? Color.create("#2a4560") : Color.create("#dbe9fb")
                gestureHandlers: [ TapHandler { onTapped: {
                    boardRow.ListItem.view.openPollVote(ListItemData.id);
                } } ]
                Label {
                    horizontalAlignment: HorizontalAlignment.Center
                    verticalAlignment: VerticalAlignment.Center
                    text: qsTr("Vote")
                    textStyle { color: Color.create("#2575fc"); fontSize: FontSize.Large; fontWeight: FontWeight.Bold }
                }
            }
        }

        Container {
            visible: ListItemData.boardType === "note" || ListItemData.boardType === "pin"
            horizontalAlignment: HorizontalAlignment.Fill
            layout: StackLayout { orientation: LayoutOrientation.TopToBottom }

            Label {
                text: ListItemData.title || ""
                multiline: true
                textStyle { fontSize: FontSize.Medium }
                topMargin: ui.du(0.2); bottomMargin: ui.du(0.4)
            }
            Label {
                text: ListItemData.boardType === "note" ? qsTr("View note") : qsTr("Jump to message")
                textStyle { color: Color.create("#2575fc"); fontSize: FontSize.Small }
                gestureHandlers: [
                    TapHandler {
                        onTapped: {
                            if (ListItemData.boardType === "pin") {
                                boardRow.ListItem.view.jumpToPinned(ListItemData.msgId || ListItemData.id || "");
                            } else if (ListItemData.boardType === "note") {
                                boardRow.ListItem.view.openNote(ListItemData.title || "", ListItemData.creatorId || "");
                            }
                        }
                    }
                ]
            }
        }
    }
}
