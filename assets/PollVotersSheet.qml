import bb.cascades 1.4

// PollVotersSheet — "who voted" detail for a single poll, opened from the
// inline poll card's "View voters" link and from boardEvent rows of kind
// "poll" (see ChatView.qml). Fetches fresh detail (including per-option
// voter uids — zca-js's PollOptions.voters[], ported by
// ZaloService::getPollDetail()) every time it opens rather than reusing
// whatever's cached in msgModel/boardItems, since neither of those carries
// voter names, only vote counts.
Sheet {
    id: votersSheet

    property string pollId: ""
    property bool   isDark: false
    property variant detail: ({})
    property bool   loading: false
    property string errorText: ""

    // Call this instead of just setting pollId — starts the fetch too.
    function openFor(id) {
        votersSheet.pollId = id;
        votersSheet.detail = {};
        votersSheet.errorText = "";
        votersSheet.loading = true;
        zService.getPollDetail(id);
        votersSheet.open();
    }

    function votersText(option) {
        var voters = option.voters || [];
        if (voters.length === 0) return "No votes yet";
        var names = [];
        for (var i = 0; i < voters.length; i++) {
            var n = zService.memberDisplayName(voters[i]);
            names.push(n && n.length > 0 ? n : "Someone");
        }
        return names.join(", ");
    }

    attachedObjects: [
        Connections {
            target: zService
            onPollDetailReady: {
                if (pollId !== votersSheet.pollId) return;
                votersSheet.loading = false;
                if (error && error.length > 0) { votersSheet.errorText = error; return; }
                votersSheet.detail = detail;
            }
        }
    ]

    content: Page {
        titleBar: TitleBar {
            title: qsTr("Poll votes")
            dismissAction: ActionItem {
                title: qsTr("Close")
                onTriggered: { votersSheet.close(); }
            }
        }

        Container {
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment: VerticalAlignment.Fill
            background: votersSheet.isDark ? Color.create("#1a1a1a") : Color.White

            ActivityIndicator {
                visible: votersSheet.loading
                running: votersSheet.loading
                preferredWidth: ui.du(8); preferredHeight: ui.du(8)
                horizontalAlignment: HorizontalAlignment.Center
                topMargin: ui.du(6)
            }

            Label {
                visible: !votersSheet.loading && votersSheet.errorText.length > 0
                text: qsTr("Couldn't load votes: ") + votersSheet.errorText
                multiline: true
                horizontalAlignment: HorizontalAlignment.Center
                topMargin: ui.du(6)
                textStyle { color: Color.create("#e53935") }
            }

            ScrollView {
                visible: !votersSheet.loading && votersSheet.errorText.length === 0
                scrollViewProperties.scrollMode: ScrollMode.Vertical
                layoutProperties: StackLayoutProperties { spaceQuota: 1 }

                Container {
                    topPadding: ui.du(2); bottomPadding: ui.du(2)
                    leftPadding: ui.du(2); rightPadding: ui.du(2)
                    layout: StackLayout { orientation: LayoutOrientation.TopToBottom }

                    Label {
                        text: votersSheet.detail.question || ""
                        multiline: true
                        textStyle { fontWeight: FontWeight.Bold; base: SystemDefaults.TextStyles.SubtitleText
                                    color: votersSheet.isDark ? Color.White : Color.Black }
                        bottomMargin: ui.du(2)
                    }

                    // Fixed 0..5 slots — same "no Repeater inside a Cascades
                    // Container" constraint GroupBoardSheet.qml's own poll
                    // rendering already works around (see that file/
                    // PinboardBar.qml for the same pattern), applied here too.

                    Container {
                        visible: (votersSheet.detail.options || []).length > 0
                        layout: StackLayout { orientation: LayoutOrientation.TopToBottom }
                        topPadding: ui.du(1); bottomPadding: ui.du(1)
                        Divider {}
                        Label {
                            text: ((votersSheet.detail.options || [])[0] ? (votersSheet.detail.options[0].content + "  (" + votersSheet.detail.options[0].votes + ")") : "")
                            textStyle { fontWeight: FontWeight.Bold; fontSize: FontSize.Medium; color: votersSheet.isDark ? Color.White : Color.Black }
                            topMargin: ui.du(1)
                        }
                        Label {
                            text: (votersSheet.detail.options || [])[0] ? votersSheet.votersText(votersSheet.detail.options[0]) : ""
                            multiline: true
                            textStyle { fontSize: FontSize.Small; color: Color.Gray }
                        }
                    }
                    Container {
                        visible: (votersSheet.detail.options || []).length > 1
                        layout: StackLayout { orientation: LayoutOrientation.TopToBottom }
                        topPadding: ui.du(1); bottomPadding: ui.du(1)
                        Divider {}
                        Label {
                            text: ((votersSheet.detail.options || [])[1] ? (votersSheet.detail.options[1].content + "  (" + votersSheet.detail.options[1].votes + ")") : "")
                            textStyle { fontWeight: FontWeight.Bold; fontSize: FontSize.Medium; color: votersSheet.isDark ? Color.White : Color.Black }
                            topMargin: ui.du(1)
                        }
                        Label {
                            text: (votersSheet.detail.options || [])[1] ? votersSheet.votersText(votersSheet.detail.options[1]) : ""
                            multiline: true
                            textStyle { fontSize: FontSize.Small; color: Color.Gray }
                        }
                    }
                    Container {
                        visible: (votersSheet.detail.options || []).length > 2
                        layout: StackLayout { orientation: LayoutOrientation.TopToBottom }
                        topPadding: ui.du(1); bottomPadding: ui.du(1)
                        Divider {}
                        Label {
                            text: ((votersSheet.detail.options || [])[2] ? (votersSheet.detail.options[2].content + "  (" + votersSheet.detail.options[2].votes + ")") : "")
                            textStyle { fontWeight: FontWeight.Bold; fontSize: FontSize.Medium; color: votersSheet.isDark ? Color.White : Color.Black }
                            topMargin: ui.du(1)
                        }
                        Label {
                            text: (votersSheet.detail.options || [])[2] ? votersSheet.votersText(votersSheet.detail.options[2]) : ""
                            multiline: true
                            textStyle { fontSize: FontSize.Small; color: Color.Gray }
                        }
                    }
                    Container {
                        visible: (votersSheet.detail.options || []).length > 3
                        layout: StackLayout { orientation: LayoutOrientation.TopToBottom }
                        topPadding: ui.du(1); bottomPadding: ui.du(1)
                        Divider {}
                        Label {
                            text: ((votersSheet.detail.options || [])[3] ? (votersSheet.detail.options[3].content + "  (" + votersSheet.detail.options[3].votes + ")") : "")
                            textStyle { fontWeight: FontWeight.Bold; fontSize: FontSize.Medium; color: votersSheet.isDark ? Color.White : Color.Black }
                            topMargin: ui.du(1)
                        }
                        Label {
                            text: (votersSheet.detail.options || [])[3] ? votersSheet.votersText(votersSheet.detail.options[3]) : ""
                            multiline: true
                            textStyle { fontSize: FontSize.Small; color: Color.Gray }
                        }
                    }
                    Container {
                        visible: (votersSheet.detail.options || []).length > 4
                        layout: StackLayout { orientation: LayoutOrientation.TopToBottom }
                        topPadding: ui.du(1); bottomPadding: ui.du(1)
                        Divider {}
                        Label {
                            text: ((votersSheet.detail.options || [])[4] ? (votersSheet.detail.options[4].content + "  (" + votersSheet.detail.options[4].votes + ")") : "")
                            textStyle { fontWeight: FontWeight.Bold; fontSize: FontSize.Medium; color: votersSheet.isDark ? Color.White : Color.Black }
                            topMargin: ui.du(1)
                        }
                        Label {
                            text: (votersSheet.detail.options || [])[4] ? votersSheet.votersText(votersSheet.detail.options[4]) : ""
                            multiline: true
                            textStyle { fontSize: FontSize.Small; color: Color.Gray }
                        }
                    }
                    Container {
                        visible: (votersSheet.detail.options || []).length > 5
                        layout: StackLayout { orientation: LayoutOrientation.TopToBottom }
                        topPadding: ui.du(1); bottomPadding: ui.du(1)
                        Divider {}
                        Label {
                            text: ((votersSheet.detail.options || [])[5] ? (votersSheet.detail.options[5].content + "  (" + votersSheet.detail.options[5].votes + ")") : "")
                            textStyle { fontWeight: FontWeight.Bold; fontSize: FontSize.Medium; color: votersSheet.isDark ? Color.White : Color.Black }
                            topMargin: ui.du(1)
                        }
                        Label {
                            text: (votersSheet.detail.options || [])[5] ? votersSheet.votersText(votersSheet.detail.options[5]) : ""
                            multiline: true
                            textStyle { fontSize: FontSize.Small; color: Color.Gray }
                        }
                    }
                }
            }
        }
    }
}
