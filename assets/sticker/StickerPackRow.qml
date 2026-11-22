import bb.cascades 1.4
import QtQuick 1.0

// One row of Sticker settings: thumbnail, name, and either the on/off switch or, in reorder mode, the drag
// handle. The rows themselves never move: reordering swaps their DATA (setData) so the control that owns the
// finger stays where it is for the whole drag. `host` is the page, which receives every user action.
Container {
    id: row

    property variant host
    property int    index: 0
    property string packId: ""
    property string packName: ""
    property string thumb: ""
    property bool   packEnabled: true
    property bool   reorderMode: false
    property bool   lifted: false       // the row currently showing the pack being dragged
    property bool   syncing: false      // true while setData() writes the switch, so it is not taken for a tap

    horizontalAlignment: HorizontalAlignment.Fill
    layout: StackLayout { orientation: LayoutOrientation.TopToBottom }
    background: row.lifted ? Color.create("#552575fc") : Color.create("#00000000")

    function setData(id, name, thumbPath, enabled) {
        row.syncing = true;
        row.packId = id;
        row.packName = name;
        row.thumb = thumbPath;
        row.packEnabled = enabled;
        toggle.checked = enabled;
        row.syncing = false;
    }

    // Starts dy pixels away from the final spot and slides home.
    function slideFrom(dy) {
        slide.stop();
        slide.target = row;
        slide.fromY = dy;
        slide.toY = 0;
        slide.play();
    }

    onCreationCompleted: { toggle.checked = row.packEnabled; }

    attachedObjects: [
        TranslateTransition {
            id: slide
            duration: 170
            easingCurve: StockCurve.CubicOut
        }
    ]

    Container {
        horizontalAlignment: HorizontalAlignment.Fill
        preferredHeight: ui.du(10)
        leftPadding: ui.du(1.5); rightPadding: ui.du(1.5)
        layout: StackLayout { orientation: LayoutOrientation.LeftToRight }

        ImageView {
            verticalAlignment: VerticalAlignment.Center
            preferredWidth: ui.du(8); preferredHeight: ui.du(8)
            scalingMethod: ScalingMethod.AspectFit
            imageSource: row.thumb
        }
        Label {
            layoutProperties: StackLayoutProperties { spaceQuota: 1 }
            verticalAlignment: VerticalAlignment.Center
            leftMargin: ui.du(2)
            text: row.packName
            textStyle.base: SystemDefaults.TextStyles.TitleText
        }
        ToggleButton {
            id: toggle
            verticalAlignment: VerticalAlignment.Center
            visible: !row.reorderMode
            onCheckedChanged: {
                if (!row.syncing && row.host) row.host.setPackEnabled(row.packId, checked);
            }
        }
        ImageView {
            verticalAlignment: VerticalAlignment.Center
            visible: row.reorderMode
            preferredWidth: ui.du(7); preferredHeight: ui.du(7)
            imageSource: "asset:///images/ChatView/ic_menu.png"
            touchPropagationMode: TouchPropagationMode.Full
            onTouch: {
                if (!row.host) return;
                if (event.isDown())      row.host.dragBegin(row.index, event.windowY);
                else if (event.isMove()) row.host.dragMove(event.windowY);
                else if (event.isUp() || event.touchType === TouchType.Cancel) row.host.dragEnd();
            }
        }
    }
    Divider {}
}
