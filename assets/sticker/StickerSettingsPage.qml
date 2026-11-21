import bb.cascades 1.4
import QtQuick 1.0

// "Sticker settings": toggle packs on/off, and reorder them with the ic_menu.png drag handle.
// Toggles apply immediately; a new order is only stored when "Save" is pressed (leaving the page
// in reorder mode discards it).
Page {
    id: settingsPage

    property bool isDark: app.getDarkTheme()
    property bool reorderMode: false
    property int  packCount: 0
    property int  dragIndex: -1
    property real dragLastY: 0

    function reload() {
        packsModel.clear();
        var list = stickerManager.installedPacks();
        for (var i = 0; i < list.length; i++) packsModel.append(list[i]);
        settingsPage.packCount = packsModel.size();
        emptyLabel.visible = (settingsPage.packCount === 0);
    }
    onCreationCompleted: reload()

    function indexOfPack(id) {
        for (var i = 0; i < packsModel.size(); i++) {
            if (packsModel.value(i).id === id) return i;
        }
        return -1;
    }

    // Idempotent on purpose: rebinding a recycled ToggleButton fires onCheckedChanged too.
    function setPackEnabled(id, value) {
        var i = indexOfPack(id);
        if (i < 0) return;
        var it = packsModel.value(i);
        if (it.enabled === value) return;
        it.enabled = value;
        packsModel.replace(i, it);
        stickerManager.setPackEnabled(id, value);
    }

    function dragBegin(id, y) {
        settingsPage.dragIndex = indexOfPack(id);
        settingsPage.dragLastY = y;
    }

    // The finger moved a whole row: swap the pack with its neighbour. ArrayDataModel animates the
    // swap natively; the index is tracked here rather than per delegate because delegates are
    // recycled while the touch is still on the original handle.
    function dragMove(y) {
        if (settingsPage.dragIndex < 0) return;
        var rowH = ui.du(10);
        var d = y - settingsPage.dragLastY;
        while (d >= rowH && settingsPage.dragIndex < packsModel.size() - 1) {
            packsModel.swap(settingsPage.dragIndex, settingsPage.dragIndex + 1);
            settingsPage.dragIndex++;
            settingsPage.dragLastY += rowH;
            d -= rowH;
        }
        while (d <= -rowH && settingsPage.dragIndex > 0) {
            packsModel.swap(settingsPage.dragIndex, settingsPage.dragIndex - 1);
            settingsPage.dragIndex--;
            settingsPage.dragLastY -= rowH;
            d += rowH;
        }
    }

    function dragEnd() { settingsPage.dragIndex = -1; }

    function saveOrder() {
        var ids = [];
        for (var i = 0; i < packsModel.size(); i++) ids.push(packsModel.value(i).id);
        stickerManager.savePackOrder(ids);
        settingsPage.reorderMode = false;
    }

    titleBar: TitleBar {
        kind: TitleBarKind.FreeForm
        kindProperties: FreeFormTitleBarKindProperties {
            content: Container {
                background: Color.create("#2575fc")
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment: VerticalAlignment.Fill
                layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                leftPadding: ui.du(2)
                Label {
                    text: "Sticker settings"
                    layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                    horizontalAlignment: HorizontalAlignment.Fill
                    verticalAlignment: VerticalAlignment.Center
                    textStyle { color: Color.White; base: SystemDefaults.TextStyles.TitleText; fontWeight: FontWeight.Bold }
                }
            }
        }
    }

    // One action that switches role, so it never has to be hidden.
    actions: [
        ActionItem {
            title: settingsPage.reorderMode ? "Save" : "Reorder"
            imageSource: settingsPage.reorderMode
                ? "asset:///images/ChatView/ic_save.png"
                : "asset:///images/ChatView/ic_list.png"
            ActionBar.placement: ActionBarPlacement.OnBar
            enabled: settingsPage.reorderMode || settingsPage.packCount > 1
            onTriggered: {
                if (settingsPage.reorderMode) settingsPage.saveOrder();
                else settingsPage.reorderMode = true;
            }
        }
    ]

    attachedObjects: [
        Connections {
            target: app
            onDarkThemeChanged: { settingsPage.isDark = dark; }
        }
    ]

    Container {
        layout: DockLayout {}

        ListView {
            id: packsList
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment: VerticalAlignment.Fill
            dataModel: ArrayDataModel { id: packsModel }

            // ListItemComponent delegates only see the ListView, so they call back through these.
            property bool reorderMode: settingsPage.reorderMode
            function setPackEnabled(id, value) { settingsPage.setPackEnabled(id, value); }
            function dragBegin(id, y) { settingsPage.dragBegin(id, y); }
            function dragMove(y)      { settingsPage.dragMove(y); }
            function dragEnd()        { settingsPage.dragEnd(); }

            listItemComponents: [
                ListItemComponent {
                    type: ""
                    Container {
                        id: packRow
                        horizontalAlignment: HorizontalAlignment.Fill
                        layout: StackLayout { orientation: LayoutOrientation.TopToBottom }

                        Container {
                            horizontalAlignment: HorizontalAlignment.Fill
                            preferredHeight: ui.du(10)
                            leftPadding: ui.du(1.5); rightPadding: ui.du(1.5)
                            layout: StackLayout { orientation: LayoutOrientation.LeftToRight }

                            ImageView {
                                verticalAlignment: VerticalAlignment.Center
                                preferredWidth: ui.du(8); preferredHeight: ui.du(8)
                                scalingMethod: ScalingMethod.AspectFit
                                imageSource: ListItemData.thumb
                            }
                            Label {
                                layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                                verticalAlignment: VerticalAlignment.Center
                                leftMargin: ui.du(2)
                                text: ListItemData.name
                                textStyle.base: SystemDefaults.TextStyles.TitleText
                            }
                            ToggleButton {
                                verticalAlignment: VerticalAlignment.Center
                                visible: !packRow.ListItem.view.reorderMode
                                checked: ListItemData.enabled
                                onCheckedChanged: { packRow.ListItem.view.setPackEnabled(ListItemData.id, checked); }
                            }
                            ImageView {
                                verticalAlignment: VerticalAlignment.Center
                                visible: packRow.ListItem.view.reorderMode
                                preferredWidth: ui.du(7); preferredHeight: ui.du(7)
                                imageSource: "asset:///images/ChatView/ic_menu.png"
                                // Keep the drag from also scrolling the list.
                                touchPropagationMode: TouchPropagationMode.None
                                onTouch: {
                                    if (event.isDown())      packRow.ListItem.view.dragBegin(ListItemData.id, event.windowY);
                                    else if (event.isMove()) packRow.ListItem.view.dragMove(event.windowY);
                                    else if (event.isUp() || event.touchType === TouchType.Cancel) packRow.ListItem.view.dragEnd();
                                }
                            }
                        }
                        Divider {}
                    }
                }
            ]
        }

        Label {
            id: emptyLabel
            visible: false
            horizontalAlignment: HorizontalAlignment.Center
            verticalAlignment: VerticalAlignment.Center
            multiline: true
            textStyle { textAlign: TextAlign.Center }
            text: "No sticker packs yet.\nOpen the Shop to get some."
        }
    }
}
