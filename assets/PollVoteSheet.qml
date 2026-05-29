import bb.cascades 1.4

// Poll voting sheet. Opened by the "Vote" button on the inline poll card in ChatView.
//
// Look & feel follows AboutSheet/SettingsSheet (blue title bar with an X to close). Each option
// has a checkbox; a single-choice poll lets exactly one be ticked, a multi-choice poll any
// number. "Vote" is disabled until the selection is non-empty AND differs from what the user
// already voted (so re-opening a poll you've voted in doesn't offer a pointless re-submit).
//
// The result comes back through zService.votePollDone, which ChatView already handles to refresh
// the card — this sheet only sends the vote and closes.
Sheet {
    id: pollVoteSheet

    property string pollId: ""
    property string groupId: ""
    property bool   isDark: false
    property string question: ""
    property string creatorLine: ""
    property bool   allowMulti: false
    property int    numVote: 0
    property int    selectedCount: 0
    property bool   selectionChanged: false

    function pad2(n) { return n < 10 ? "0" + n : "" + n; }

    function formatWhen(ts) {
        var t = Number(ts);
        if (!t || t <= 0) return "";
        if (t < 1e12) t = t * 1000;                   // seconds -> ms
        var d = new Date(t);
        return pad2(d.getDate()) + "/" + pad2(d.getMonth() + 1) + "/" + d.getFullYear()
             + " " + pad2(d.getHours()) + ":" + pad2(d.getMinutes());
    }

    // `row` is the poll's row from ChatView's message model (pollId, pollQuestion, pollOptions,
    // pollAllowMulti, pollCreatorId, pollCreatedTs, pollNumVote).
    function openFor(row, gid) {
        pollVoteSheet.pollId     = row.pollId || "";
        pollVoteSheet.groupId    = gid || "";
        pollVoteSheet.question   = row.pollQuestion || "";
        pollVoteSheet.allowMulti = !!row.pollAllowMulti;
        pollVoteSheet.numVote    = row.pollNumVote || 0;

        var who = "";
        if (row.pollCreatorId && row.pollCreatorId.length > 0) {
            who = zService.memberDisplayName(row.pollCreatorId) || "";
        }
        var when = pollVoteSheet.formatWhen(row.pollCreatedTs);
        pollVoteSheet.creatorLine = (who.length > 0 && when.length > 0) ? (who + "  \u2022  " + when)
                                                                       : (who.length > 0 ? who : when);

        optionModel.clear();
        var opts = row.pollOptions || [];
        for (var i = 0; i < opts.length; i++) {
            var voted = !!opts[i].voted;
            // `checked` is what the user is editing; `voted` is what the server has
            optionModel.append({
                optionId: opts[i].optionId,
                content:  opts[i].content || "",
                votes:    opts[i].votes || 0,
                voted:    voted,
                checked:  voted
            });
        }
        pollVoteSheet.recomputeSelection();
        pollVoteSheet.open();
    }

    function recomputeSelection() {
        var n = 0;
        var changed = false;
        for (var i = 0; i < optionModel.size(); i++) {
            var it = optionModel.value(i);
            if (it.checked) n++;
            if (!!it.checked !== !!it.voted) changed = true;
        }
        pollVoteSheet.selectedCount    = n;
        pollVoteSheet.selectionChanged = changed;
    }

    function toggleAt(idx) {
        var it = optionModel.value(idx);
        if (!it) return;
        if (pollVoteSheet.allowMulti) {
            it.checked = !it.checked;
            optionModel.replace(idx, it);
        } else {
            // Single choice: behaves like a radio group — tapping an option selects it and
            // clears the others; tapping the selected one leaves it selected.
            for (var i = 0; i < optionModel.size(); i++) {
                var o = optionModel.value(i);
                var want = (i === idx);
                if (!!o.checked !== want) {
                    o.checked = want;
                    optionModel.replace(i, o);
                }
            }
        }
        pollVoteSheet.recomputeSelection();
    }

    function submit() {
        var ids = [];
        for (var i = 0; i < optionModel.size(); i++) {
            var it = optionModel.value(i);
            if (it.checked) ids.push(it.optionId);
        }
        if (ids.length === 0 || pollVoteSheet.pollId.length === 0) return;
        zService.voteGroupPoll(pollVoteSheet.groupId, pollVoteSheet.pollId, ids);
        pollVoteSheet.close();
    }

    attachedObjects: [
        ArrayDataModel { id: optionModel }
    ]

    Page {
        titleBar: TitleBar {
            scrollBehavior: TitleBarScrollBehavior.Sticky
            kind: TitleBarKind.FreeForm
            kindProperties: FreeFormTitleBarKindProperties {
                content: Container {
                    background: Color.create("#2575fc")
                    horizontalAlignment: HorizontalAlignment.Fill
                    verticalAlignment:   VerticalAlignment.Fill
                    layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                    leftPadding: ui.du(1)

                    ImageButton {
                        verticalAlignment: VerticalAlignment.Center
                        preferredWidth:  ui.du(6); preferredHeight: ui.du(6)
                        defaultImageSource: "asset:///images/AboutSheet/ic_close_white.png"
                        pressedImageSource: "asset:///images/AboutSheet/ic_close_white.png"
                        rightMargin: ui.du(0.5)
                        onClicked: { pollVoteSheet.close() }
                    }

                    Label {
                        text: "Poll"
                        layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                        verticalAlignment: VerticalAlignment.Center
                        textStyle { color: Color.White; base: SystemDefaults.TextStyles.TitleText; fontWeight: FontWeight.Bold }
                        topMargin: 0; bottomMargin: 0
                    }
                }
            }
        }

        Container {
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment:   VerticalAlignment.Fill
            layout: StackLayout { orientation: LayoutOrientation.TopToBottom }
            background: pollVoteSheet.isDark ? Color.create("#1a1a1a") : Color.White

            // ---- Header: name, creator • time, poll type, voter count ----
            Container {
                horizontalAlignment: HorizontalAlignment.Fill
                layout: StackLayout { orientation: LayoutOrientation.TopToBottom }
                topPadding: ui.du(2); bottomPadding: ui.du(1.2)
                leftPadding: ui.du(2); rightPadding: ui.du(2)

                // Same style as the poll name on the chat card: BigText, regular weight
                Label {
                    text: pollVoteSheet.question
                    multiline: true
                    textStyle { base: SystemDefaults.TextStyles.BigText
                                color: pollVoteSheet.isDark ? Color.White : Color.Black }
                }
                Label {
                    visible: pollVoteSheet.creatorLine.length > 0
                    text: pollVoteSheet.creatorLine
                    textStyle { color: Color.Gray; fontSize: FontSize.Small }
                }
                Label {
                    text: pollVoteSheet.allowMulti ? "Choose multiple options" : "Choose one option"
                    textStyle { color: pollVoteSheet.isDark ? Color.create("#dddddd") : Color.create("#444444")
                                fontSize: FontSize.Medium }
                    topMargin: ui.du(1)
                }
                Label {
                    visible: pollVoteSheet.numVote > 0
                    text: pollVoteSheet.numVote + (pollVoteSheet.numVote === 1 ? " member voted" : " members voted")
                    textStyle { color: Color.create("#2575fc"); fontSize: FontSize.Medium }
                    topMargin: ui.du(1)
                }
            }

            // ---- Options (checkbox per row) ----
            ListView {
                id: optionList
                layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                dataModel: optionModel
                property bool isDarkProxy: pollVoteSheet.isDark

                listItemComponents: [
                    ListItemComponent {
                        CustomListItem {
                            id: optRoot
                            dividerVisible: false
                            Container {
                                horizontalAlignment: HorizontalAlignment.Fill
                                topPadding: ui.du(0.4); bottomPadding: ui.du(0.4)
                                leftPadding: ui.du(1.5); rightPadding: ui.du(1.5)
                                background: optRoot.ListItem.view.isDarkProxy ? Color.create("#1a1a1a") : Color.White

                                Container {
                                    horizontalAlignment: HorizontalAlignment.Fill
                                    layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                                    topPadding: ui.du(1.2); bottomPadding: ui.du(1.2)
                                    leftPadding: ui.du(1.2); rightPadding: ui.du(1.5)
                                    background: ListItemData.checked === true
                                                ? (optRoot.ListItem.view.isDarkProxy ? Color.create("#2a4560") : Color.create("#cfe3fa"))
                                                : (optRoot.ListItem.view.isDarkProxy ? Color.create("#33404a") : Color.create("#f0f0f0"))

                                    CheckBox {
                                        verticalAlignment: VerticalAlignment.Center
                                        checked: ListItemData.checked === true
                                        // enabled must stay true or Cascades greys the checkbox out.
                                        // PassThrough so a tap directly on the checkbox still reaches
                                        // the ListView's onTriggered (that is where selection changes)
                                        touchPropagationMode: TouchPropagationMode.PassThrough
                                    }
                                    Label {
                                        layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                                        verticalAlignment: VerticalAlignment.Center
                                        text: ListItemData.content || ""
                                        multiline: true
                                        textStyle { fontSize: FontSize.Large
                                                    color: optRoot.ListItem.view.isDarkProxy ? Color.White : Color.Black }
                                    }
                                    Label {
                                        verticalAlignment: VerticalAlignment.Center
                                        text: String(ListItemData.votes || 0)
                                        textStyle { color: Color.Gray; fontSize: FontSize.Large }
                                    }
                                }
                            }
                        }
                    }
                ]

                onTriggered: {
                    pollVoteSheet.toggleAt(indexPath[0]);
                }
            }

            // ---- Vote button (disabled until something is selected) ----
            Container {
                horizontalAlignment: HorizontalAlignment.Fill
                topPadding: ui.du(1.2); bottomPadding: ui.du(1.2)
                leftPadding: ui.du(2);  rightPadding: ui.du(2)
                background: pollVoteSheet.isDark ? Color.create("#1a1a1a") : Color.White

                Button {
                    horizontalAlignment: HorizontalAlignment.Fill
                    text: "Vote"
                    enabled: pollVoteSheet.selectedCount > 0 && pollVoteSheet.selectionChanged
                    onClicked: { pollVoteSheet.submit() }
                }
            }
        }
    }
}
