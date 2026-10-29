import bb.cascades 1.4
import bb.system 1.0
import QtQuick 1.0

CustomListItem {
    id: rowRoot
    highlightAppearance: HighlightAppearance.None
    dividerVisible: false

    // Helper functions for bindings: block-body bindings ({ var x; ...; return x; }) are a
    // parse-time error on this QtQuick1/Cascades engine ("Expected token `numeric literal'").
    // Every multi-statement binding in this delegate must go through a named function.
    function timestampLabel(latestTs, ts) {
        var t = latestTs || ts;
        if (!t) return "";
        var n = t * 1;
        if (n > 0 && n < 1e12) n *= 1000;
        var d   = new Date(n);
        var dow = ["Sun","Mon","Tue","Wed","Thu","Fri","Sat"][d.getDay()];
        var h   = d.getHours();
        var m2  = d.getMinutes();
        var ap  = h >= 12 ? "PM" : "AM";
        var h12 = h % 12; if (h12 === 0) h12 = 12;
        return dow + " " + h12 + ":" + (m2 < 10 ? "0" : "") + m2 + " " + ap;
    }
    function bubbleText(content, msgType, searchQuery, isCurrentSearchMatch) {
        var raw = (typeof content === "string" && content.length > 0)
              ? content
              : ((msgType === 2 || msgType === "2")
                 ? "[Photo]"
                 : ((msgType === 6 || msgType === "6")
                    ? "[Sticker]" : "[Photo]"));
        if (searchQuery && searchQuery.length > 0) {
            var hlColor = isCurrentSearchMatch ? "#ff9800" : "#ffeb3b";
            return "<html>" + rowRoot.ListItem.view.highlightMatchesProxy(raw, searchQuery, hlColor) + "</html>";
        }
        return raw;
    }
    function hasCaption(content) {
        var c = content || "";
        if (c.length === 0 || c.charCodeAt(0) !== 123) return false;
        return c.indexOf('"caption":"') >= 0;
    }
    function extractCaption(content) {
        var c = content || "";
        if (c.length === 0) return "";
        var key = '"caption":"';
        var si = c.indexOf(key);
        if (si < 0) return "";
        si += key.length;
        var ei = si;
        while (ei < c.length) {
            var code = c.charCodeAt(ei);
            if (code === 92) { ei += 2; continue; }
            if (code === 34) break;
            ei++;
        }
        return c.substring(si, ei);
    }
    function photoStatusText(mine, msgId, failed) {
        var mid = msgId || "";
        if (mine && mid.indexOf("local_img_") === 0) return failed ? "Not sent" : "Sending...";
        return mine ? "Picture sent" : "Photo";
    }

    // Do NOT set preferredHeight on rowRoot (even bound to a child's layoutFrame.height, or -1): it
    // is self-referential, locks the first measured height and clips later wrapped lines.
    // CustomListItem also has no padding or background property; the divider/highlight gap is
    // cancelled with a fixed negative bottomMargin on rowContentRoot.

    // ActionItem/DeleteActionItem has no "visible" property (it extends UIObject, not Control):
    // build two full ActionSets (member vs admin/owner) and pick one. A failed bubble gets its own
    // set with "Resend" under "Reaction".
    contextActions: [
        rowRoot.sendFailed ? bubbleActionsFailed
            : (rowRoot.isAdminOrOwner ? bubbleActionsAdmin : bubbleActionsMember)
    ]

    attachedObjects: [
        BubbleActionsMember { id: bubbleActionsMember },
        BubbleActionsFailed { id: bubbleActionsFailed },
        BubbleActionsAdmin { id: bubbleActionsAdmin }
    ]

    property bool isDark: ListItem.view.isDark
    property bool isAdminOrOwner: ListItem.view.isAdminOrOwner

    property string searchQuery: ListItem.view.searchQuery
    property bool   isCurrentSearchMatch: rowRoot.searchQuery.length > 0
                                           && ListItem.view.searchCurrentMsgIndex === ListItem.indexPath[0]

    property bool mine: (ListItemData.isMine === true
                         || ListItemData.isMine === "true"
                         || ListItemData.isMine === 1)
    property string kind: ListItemData.kind || "message"
    property bool grouped: ListItemData.grouped === true
    onGroupedChanged: {
    }
    property bool recalled: (ListItemData.msgType === 99 || ListItemData.msgType === "99")
    property bool showRecalledSetting: ListItem.view.showRecalledMessages
    property string recalledOriginal: ListItemData.recalledOriginalContent || ""
    property bool recalledIsPhoto: (rowRoot.recalledOriginal.length > 0
                                     && rowRoot.recalledOriginal.charAt(0) === "{"
                                     && (rowRoot.recalledOriginal.indexOf("normalUrl") >= 0
                                         || rowRoot.recalledOriginal.indexOf("thumbUrl") >= 0
                                         || rowRoot.recalledOriginal.indexOf("thumb") >= 0
                                         || rowRoot.recalledOriginal.indexOf("href") >= 0))
                                    || !!(ListItemData.localImage && ListItemData.localImage !== "")
    property bool recalledHasOriginalText: rowRoot.recalledOriginal.length > 0 && !rowRoot.recalledIsPhoto
    property bool recalledHidden: rowRoot.recalled
                                   && !(rowRoot.showRecalledSetting
                                        && (rowRoot.recalledHasOriginalText || rowRoot.recalledIsPhoto))

    property string bubblePos: ListItemData.bubblePos || "full"
    property bool sendFailed: ListItemData.sendFailed === true
    property int outState: rowRoot.mine
        ? (rowRoot.sendFailed ? 4
            : ((String(ListItemData.msgId || "").indexOf("local_") === 0)
                ? 0 : (ListItemData.msgState || 1)))
        : -1
    onBubblePosChanged: {
    }
    property real bubbleMaxW: rowLUH.layoutFrame.width > 94
                               ? (rowLUH.layoutFrame.width - 94)
                               : (ListItem.view.widthHint > 94
                                  ? (ListItem.view.widthHint - 94)
                                  : ui.du(40))

    property bool hasQuote: !!(ListItemData.quoteMsgId && ListItemData.quoteMsgId.length > 0)

    property bool quoteIsMine: !!(ListItemData.quoteOwnerId && ListItemData.quoteOwnerId === ListItem.view.selfUidProxy)
    property string quoteMemberName: ListItem.view.isGroupChat
        ? ListItem.view.memberDisplayNameProxy(ListItemData.quoteOwnerId || "")
        : ""
    property string quoteSenderResolved: rowRoot.quoteIsMine
        ? (ListItem.view.selfNameProxy || "Me")
        : ((!ListItem.view.isGroupChat && ListItem.view.threadNameProxy.length > 0)
           ? ListItem.view.threadNameProxy
           : ((ListItem.view.isGroupChat && rowRoot.quoteMemberName.length > 0)
              ? rowRoot.quoteMemberName
              : (ListItemData.quoteSenderName || "Unknown")))

    // Zalo's WS dName can carry OUR OWN name. 1-1: use chatViewPage.threadName; group:
    // memberDisplayNameProxy (m_memberNames from getmg-v2 currentMems). Falls back to the wire dName.
    property string otherMemberName: ListItem.view.isGroupChat
        ? ListItem.view.memberDisplayNameProxy(ListItemData.senderId || "")
        : ""
    property string otherDisplayName: (!ListItem.view.isGroupChat && ListItem.view.threadNameProxy.length > 0)
        ? ListItem.view.threadNameProxy
        : ((ListItem.view.isGroupChat && rowRoot.otherMemberName.length > 0)
           ? rowRoot.otherMemberName
           : (ListItemData.dName || "Unknown"))

    property bool isJumpHighlighted: ListItem.view.jumpHighlightMsgId.length > 0
                                      && ListItem.view.jumpHighlightMsgId === (ListItemData.msgId || "")
    property bool isHighlighted: rowRoot.isCurrentSearchMatch || rowRoot.isJumpHighlighted

    Container {
        id: rowContentRoot
        horizontalAlignment: HorizontalAlignment.Fill
        layout: StackLayout { orientation: LayoutOrientation.TopToBottom }
        // Constant offsets cancelling CustomListItem's invisible divider gap. They must be plain
        // constants, not bindings to a measured height (that clipped long messages). Tuned per
        // bubblePos ("bottom" and "middle" rows measure differently).
        property int gapCompensation:
            rowRoot.bubblePos === "middle" ? -6 : -12
        bottomMargin: rowRoot.kind === "message" ? gapCompensation : 0
        attachedObjects: [
            LayoutUpdateHandler {
                id: rowContentRootLUH
                onLayoutFrameChanged: {
                }
            }
        ]

    Container {
        horizontalAlignment: HorizontalAlignment.Fill
        visible: rowRoot.kind === "message"
        topPadding:    rowRoot.grouped ? 0 : 10
        bottomPadding: 0
        layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
        attachedObjects: [
            LayoutUpdateHandler {
                id: rowLUH
                onLayoutFrameChanged: {
                }
            }
        ]

    Container {
        preferredWidth: rowRoot.mine ? 18 : 60
        minWidth:       rowRoot.mine ? 18 : 60
        maxWidth:       rowRoot.mine ? 18 : 60
    }

    Container {
        id: bubbleWrap
        preferredWidth: rowRoot.bubbleMaxW
        maxWidth:       rowRoot.bubbleMaxW
        background: rowRoot.isHighlighted
            ? Color.create("#fff3b0")
            : (rowRoot.isDark
                ? Color.create("#2a2a2a")
                : Color.create("#FFFFFF"))

        Container {
            background: Color.Transparent
            horizontalAlignment: HorizontalAlignment.Fill
            topPadding:    rowRoot.grouped ? 6 : 10
            bottomPadding: 10
            leftPadding:   14
            rightPadding:  14

        Container {
            visible: !rowRoot.grouped
            layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
            horizontalAlignment: HorizontalAlignment.Fill
            bottomMargin: 2

            Label {
                layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                text: rowRoot.mine
                      ? (ListItemData.selfName || "Me")
                      : rowRoot.otherDisplayName
                textStyle {
                    fontSize:   FontSize.Small
                    fontWeight: FontWeight.Bold
                    color: rowRoot.mine
                        ? (rowRoot.isDark ? Color.create("#aaaaaa") : Color.create("#555555"))
                        : Color.create("#0073BC")
                }
                topMargin: 0; bottomMargin: 0
            }

            Label {
                text: rowRoot.timestampLabel(ListItemData.latestTs, ListItemData.ts)
                horizontalAlignment: HorizontalAlignment.Right
                textStyle { fontSize: FontSize.XSmall; color: rowRoot.isDark ? Color.create("#888888") : Color.create("#777777") }
                topMargin: 0; bottomMargin: 0
            }
        }

        Container {
            layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
            horizontalAlignment: HorizontalAlignment.Fill
            topMargin: 0; bottomMargin: 0

            // State icon: incoming = grey pellet; outgoing = sending -> sent -> delivered (D) -> read (R).
            // One ImageView per state toggled via visible: rebinding imageSource on a replaced row is
            // unreliable on this Cascades version (see applyImageUpdate).
            Container {
                visible: !rowRoot.recalled
                verticalAlignment: VerticalAlignment.Top
                topPadding:   7
                rightPadding: 8
            Container {
                layout: DockLayout {}
                preferredWidth:  38
                minWidth:        38
                maxWidth:        38
                preferredHeight: 38
                minHeight:       38
                ImageView {
                    visible: rowRoot.outState === -1
                    imageSource: "asset:///images/ChatView/bubble/message_states/grey_pellet.png"
                    horizontalAlignment: HorizontalAlignment.Fill
                    verticalAlignment: VerticalAlignment.Fill
                    scalingMethod: ScalingMethod.AspectFit
                }
                ImageView {
                    visible: rowRoot.outState === 0
                    imageSource: "asset:///images/ChatView/bubble/message_states/sending.png"
                    horizontalAlignment: HorizontalAlignment.Fill
                    verticalAlignment: VerticalAlignment.Fill
                    scalingMethod: ScalingMethod.AspectFit
                }
                ImageView {
                    visible: rowRoot.outState === 1
                    imageSource: "asset:///images/ChatView/bubble/message_states/sent.png"
                    horizontalAlignment: HorizontalAlignment.Fill
                    verticalAlignment: VerticalAlignment.Fill
                    scalingMethod: ScalingMethod.AspectFit
                }
                ImageView {
                    visible: rowRoot.outState === 2
                    imageSource: "asset:///images/ChatView/bubble/message_states/delivered.png"
                    horizontalAlignment: HorizontalAlignment.Fill
                    verticalAlignment: VerticalAlignment.Fill
                    scalingMethod: ScalingMethod.AspectFit
                }
                ImageView {
                    visible: rowRoot.outState === 3
                    imageSource: "asset:///images/ChatView/bubble/message_states/read.png"
                    horizontalAlignment: HorizontalAlignment.Fill
                    verticalAlignment: VerticalAlignment.Fill
                    scalingMethod: ScalingMethod.AspectFit
                }
                ImageView {
                    visible: rowRoot.outState === 4
                    imageSource: "asset:///images/ChatView/bubble/message_states/failed.png"
                    horizontalAlignment: HorizontalAlignment.Fill
                    verticalAlignment: VerticalAlignment.Fill
                    scalingMethod: ScalingMethod.AspectFit
                }
            }
            }

Container {
            id: msgContentRoot
            horizontalAlignment: HorizontalAlignment.Fill
            layoutProperties: StackLayoutProperties { spaceQuota: 1 }
            topMargin: 0; bottomMargin: 0

            Label {
                visible: rowRoot.recalledHidden
                text: rowRoot.mine ? "You recalled a message" : "This message was recalled"
                textStyle {
                    base:       SystemDefaults.TextStyles.BodyText
                    fontStyle:  FontStyle.Italic
                    color: rowRoot.isDark ? Color.create("#888888") : Color.create("#999999")
                }
                topMargin: 0; bottomMargin: 0
            }

            Container {
                visible: rowRoot.recalled && !rowRoot.recalledHidden && !rowRoot.recalledIsPhoto
                topMargin: 0; bottomMargin: 0

                Label {
                    text: rowRoot.searchQuery.length > 0
                          ? "<html>" + rowRoot.ListItem.view.highlightMatchesProxy(rowRoot.recalledOriginal, rowRoot.searchQuery, rowRoot.isCurrentSearchMatch ? "#ff9800" : "#ffeb3b") + "</html>"
                          : rowRoot.recalledOriginal
                    textStyle {
                        base:  SystemDefaults.TextStyles.BodyText
                        color: rowRoot.isDark ? Color.create("#eeeeee") : Color.create("#111111")
                    }
                    multiline: true
                    topMargin: 0; bottomMargin: 0
                }
                Label {
                    text: "(This message was recalled)"
                    textStyle {
                        base:       SystemDefaults.TextStyles.SmallText
                        fontStyle:  FontStyle.Italic
                        color: rowRoot.isDark ? Color.create("#888888") : Color.create("#999999")
                    }
                    topMargin: 2; bottomMargin: 0
                }
            }

            Container {
                visible: rowRoot.recalled && !rowRoot.recalledHidden && rowRoot.recalledIsPhoto
                topMargin: 0; bottomMargin: 0

                ImageView {
                    visible: !!(ListItemData.localImage && ListItemData.localImage !== "")
                    horizontalAlignment: HorizontalAlignment.Left
                    preferredWidth:  (ListItemData.imgWidth  && ListItemData.imgWidth  > 0)
                                     ? Math.min(rowRoot.bubbleMaxW, ListItemData.imgWidth) : Math.min(rowRoot.bubbleMaxW, ui.du(30))
                    preferredHeight: (ListItemData.imgWidth  && ListItemData.imgWidth  > 0
                                      && ListItemData.imgHeight && ListItemData.imgHeight > 0)
                                     ? (Math.min(rowRoot.bubbleMaxW, ListItemData.imgWidth) * (ListItemData.imgHeight / ListItemData.imgWidth))
                                     : ui.du(30)
                    scalingMethod: ScalingMethod.AspectFit
                    imageSource: ListItemData.localImage
                }
                Label {
                    text: "(This message was recalled)"
                    textStyle {
                        base:       SystemDefaults.TextStyles.SmallText
                        fontStyle:  FontStyle.Italic
                        color: rowRoot.isDark ? Color.create("#888888") : Color.create("#999999")
                    }
                    topMargin: 2; bottomMargin: 0
                }
            }

            Container {
                visible: !rowRoot.recalled && rowRoot.hasQuote
                horizontalAlignment: HorizontalAlignment.Fill
                background: rowRoot.isDark
                    ? (rowRoot.mine ? Color.create("#3a3a3a") : Color.create("#1c3450"))
                    : (rowRoot.mine ? Color.create("#d9d9d9") : Color.create("#cfe3fa"))
                topPadding: 6; bottomPadding: 6; leftPadding: 8; rightPadding: 8
                bottomMargin: 4

                gestureHandlers: [
                    TapHandler {
                        onTapped: {
                            rowRoot.ListItem.view.jumpToMessageProxy(ListItemData.quoteMsgId || "");
                        }
                    }
                ]

                Label {
                    text: rowRoot.quoteSenderResolved
                    multiline: false
                    textStyle {
                        fontSize:   FontSize.XSmall
                        fontWeight: FontWeight.Bold
                        color: rowRoot.mine
                            ? (rowRoot.isDark ? Color.create("#cccccc") : Color.create("#444444"))
                            : (rowRoot.isDark ? Color.create("#8ec2ff") : Color.create("#0073BC"))
                    }
                    topMargin: 0; bottomMargin: 0
                }
                Label {
                    text: (ListItemData.quoteMsgType === 2 || ListItemData.quoteMsgType === "2")
                          ? "[Photo]" : (ListItemData.quoteContent || "")
                    multiline: false
                    textStyle {
                        fontSize: FontSize.XSmall
                        color: rowRoot.isDark ? Color.create("#bbbbbb") : Color.create("#555555")
                    }
                    topMargin: 0; bottomMargin: 0
                }
            }

            Label {
                visible: !rowRoot.recalled
                         && (ListItemData.msgType !== 2 && ListItemData.msgType !== "2")
                         && (ListItemData.msgType !== 3 && ListItemData.msgType !== "3")
                         && (ListItemData.msgType !== 4 && ListItemData.msgType !== "4")
                         && (ListItemData.msgType !== 5 && ListItemData.msgType !== "5")
                         && (ListItemData.msgType !== 6 && ListItemData.msgType !== "6")
                         && !(typeof ListItemData.content === "string"
                              && ListItemData.content.length > 1
                              && ListItemData.content.charAt(0) === "{"
                              && (ListItemData.content.indexOf("normalUrl") >= 0
                                  || ListItemData.content.indexOf("thumbUrl") >= 0
                                  || ListItemData.content.indexOf("thumb") >= 0
                                  || ListItemData.content.indexOf("href") >= 0))
                maxWidth: Math.max(0, rowRoot.bubbleMaxW - 28)
                text: rowRoot.bubbleText(ListItemData.content, ListItemData.msgType,
                                          rowRoot.searchQuery, rowRoot.isCurrentSearchMatch)
                textStyle {
                    base:  SystemDefaults.TextStyles.BodyText
                    color: rowRoot.isDark ? Color.create("#eeeeee") : Color.create("#111111")
                }
                multiline: true
                topMargin: 0; bottomMargin: 0
            }

            BubblePhoto {}

            BubbleVideo {}

            BubbleCall {}

            BubbleLink {}

            BubbleSticker {}
        }
        }
    }

        // Accent strip on the bubble's bottom edge (gray = mine, blue = theirs), only on the last bubble
        // of a group or a standalone one. Height 0 instead of visible: false, which doesn't reliably
        // release the Container's layout space.
        Container {
            horizontalAlignment: HorizontalAlignment.Fill
            property bool showStrip: rowRoot.bubblePos === "bottom" || rowRoot.bubblePos === "full"
            preferredHeight: showStrip ? ui.du(0.8) : 0
            minHeight: showStrip ? ui.du(0.8) : 0
            maxHeight: showStrip ? ui.du(0.8) : 0
            visible: showStrip
            background: rowRoot.mine ? Color.create("#999999") : Color.create("#0073BC")
        }
    }

    Container {
        preferredWidth: rowRoot.mine ? 60 : 18
        minWidth:       rowRoot.mine ? 60 : 18
        maxWidth:       rowRoot.mine ? 60 : 18
    }
    }

    // Reaction pills (kind === "message" only): one chip per distinct icon, "+N" for the count
    // (msgList.summarizePills()). 6 fixed slots, no Repeater in this QtQuick version.
    RowReactions {}

    }

}
