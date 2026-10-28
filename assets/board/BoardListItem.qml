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

            ImageView {
                verticalAlignment: VerticalAlignment.Center
                preferredWidth: ui.du(4); preferredHeight: ui.du(4)
                rightMargin: ui.du(1)
                imageSource: ListItemData.boardType === "poll" ? "asset:///images/ChatView/ic_list.png"
                           : ListItemData.boardType === "note" ? "asset:///images/ChatView/ic_sb_notes.png"
                           : "asset:///images/ChatView/ic_textmessage.png"
            }
            Label {
                layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                text: groupBoardPage.creatorLabel(ListItemData.creatorId)
                verticalAlignment: VerticalAlignment.Center
                textStyle { fontWeight: FontWeight.Bold; fontSize: FontSize.Medium }
            }
            Label {
                text: ListItemData.boardType === "poll" ? "Poll"
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
                text: ListItemData.allowMultiChoices ? "Choose multiple options" : "Choose one option"
                textStyle { color: Color.Gray; fontSize: FontSize.XSmall }
                topMargin: ui.du(0.2); bottomMargin: ui.du(0.6)
            }
            Label {
                text: (ListItemData.numVote || 0) + " member" + ((ListItemData.numVote === 1) ? "" : "s") + " voted"
                textStyle { color: Color.create("#2575fc"); fontSize: FontSize.XSmall }
                bottomMargin: ui.du(0.6)
            }
            Container {
                visible: !!(ListItemData.options && ListItemData.options.length > 0)
                horizontalAlignment: HorizontalAlignment.Fill
                background: (ListItemData.options && ListItemData.options[0] && ListItemData.options[0].voted) ? Color.create("#cfe3fa") : Color.create("#f0f0f0")
                topPadding: ui.du(0.8); bottomPadding: ui.du(0.8)
                leftPadding: ui.du(1); rightPadding: ui.du(1)
                bottomMargin: ui.du(0.5)
                layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                gestureHandlers: [
                    TapHandler {
                        onTapped: {
                            if (ListItemData.closed) return;
                            groupBoardPage.doVoteOption(ListItemData.id, ListItemData.options[0].optionId,
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
                background: (ListItemData.options && ListItemData.options[1] && ListItemData.options[1].voted) ? Color.create("#cfe3fa") : Color.create("#f0f0f0")
                topPadding: ui.du(0.8); bottomPadding: ui.du(0.8)
                leftPadding: ui.du(1); rightPadding: ui.du(1)
                bottomMargin: ui.du(0.5)
                layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                gestureHandlers: [
                    TapHandler {
                        onTapped: {
                            if (ListItemData.closed) return;
                            groupBoardPage.doVoteOption(ListItemData.id, ListItemData.options[1].optionId,
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
                background: (ListItemData.options && ListItemData.options[2] && ListItemData.options[2].voted) ? Color.create("#cfe3fa") : Color.create("#f0f0f0")
                topPadding: ui.du(0.8); bottomPadding: ui.du(0.8)
                leftPadding: ui.du(1); rightPadding: ui.du(1)
                bottomMargin: ui.du(0.5)
                layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                gestureHandlers: [
                    TapHandler {
                        onTapped: {
                            if (ListItemData.closed) return;
                            groupBoardPage.doVoteOption(ListItemData.id, ListItemData.options[2].optionId,
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
                background: (ListItemData.options && ListItemData.options[3] && ListItemData.options[3].voted) ? Color.create("#cfe3fa") : Color.create("#f0f0f0")
                topPadding: ui.du(0.8); bottomPadding: ui.du(0.8)
                leftPadding: ui.du(1); rightPadding: ui.du(1)
                bottomMargin: ui.du(0.5)
                layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                gestureHandlers: [
                    TapHandler {
                        onTapped: {
                            if (ListItemData.closed) return;
                            groupBoardPage.doVoteOption(ListItemData.id, ListItemData.options[3].optionId,
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
                background: (ListItemData.options && ListItemData.options[4] && ListItemData.options[4].voted) ? Color.create("#cfe3fa") : Color.create("#f0f0f0")
                topPadding: ui.du(0.8); bottomPadding: ui.du(0.8)
                leftPadding: ui.du(1); rightPadding: ui.du(1)
                bottomMargin: ui.du(0.5)
                layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                gestureHandlers: [
                    TapHandler {
                        onTapped: {
                            if (ListItemData.closed) return;
                            groupBoardPage.doVoteOption(ListItemData.id, ListItemData.options[4].optionId,
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
                background: (ListItemData.options && ListItemData.options[5] && ListItemData.options[5].voted) ? Color.create("#cfe3fa") : Color.create("#f0f0f0")
                topPadding: ui.du(0.8); bottomPadding: ui.du(0.8)
                leftPadding: ui.du(1); rightPadding: ui.du(1)
                bottomMargin: ui.du(0.5)
                layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                gestureHandlers: [
                    TapHandler {
                        onTapped: {
                            if (ListItemData.closed) return;
                            groupBoardPage.doVoteOption(ListItemData.id, ListItemData.options[5].optionId,
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
                text: ListItemData.boardType === "note" ? "View note" : "Jump to message"
                textStyle { color: Color.create("#2575fc"); fontSize: FontSize.Small }
                gestureHandlers: [
                    TapHandler {
                        onTapped: {
                            if (ListItemData.boardType === "pin") {
                                groupBoardPage.groupsNavRef.pop();
                                if (groupBoardPage.groupsNavRef.currentPage) {
                                    groupBoardPage.groupsNavRef.currentPage.jumpToMessage(ListItemData.id || "");
                                }
                            } else if (ListItemData.boardType === "note") {
                                noteViewerSheet.noteContent = ListItemData.title || "";
                                noteViewerSheet.creatorLabel = groupBoardPage.creatorLabel(ListItemData.creatorId);
                                noteViewerSheet.open();
                            }
                        }
                    }
                ]
            }
        }
    }
}
