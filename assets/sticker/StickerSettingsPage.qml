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

    // The rows are created once and then stay where they are: they are only handed other data (see
    // StickerPackRow). rowAt(i) is the i-th row from the top.
    function rowAt(i) { return rowsBox.at(i); }

    function reload() {
        rowsBox.removeAll();
        var list = stickerManager.installedPacks();
        for (var i = 0; i < list.length; i++) {
            var r = rowDef.createObject();
            if (!r) continue;
            r.host = settingsPage;
            r.index = i;
            r.packId = list[i].id;
            r.packName = list[i].name;
            r.thumb = list[i].thumb;
            r.packEnabled = list[i].enabled;
            r.reorderMode = settingsPage.reorderMode;
            rowsBox.add(r);
        }
        settingsPage.packCount = list.length;
        emptyLabel.visible = (settingsPage.packCount === 0);
    }
    onCreationCompleted: reload()

    onReorderModeChanged: {
        for (var i = 0; i < rowsBox.count(); i++) rowAt(i).reorderMode = settingsPage.reorderMode;
    }

    // Toggles apply immediately (the manager ignores a value it already has).
    function setPackEnabled(id, value) { stickerManager.setPackEnabled(id, value); }

    function dragBegin(index, y) {
        settingsPage.dragIndex = index;
        settingsPage.dragLastY = y;
        // No scrolling while a finger drags a row, or the ScrollView takes the touch over.
        scroller.scrollViewProperties.scrollMode = ScrollMode.None;
        rowAt(index).lifted = true;
    }

    // Row `a` (showing the dragged pack) and its neighbour `b` swap their data and both slide into place.
    function swapRows(a, b) {
        var ra = rowAt(a);
        var rb = rowAt(b);
        var id = ra.packId, name = ra.packName, th = ra.thumb, en = ra.packEnabled;
        ra.setData(rb.packId, rb.packName, rb.thumb, rb.packEnabled);
        rb.setData(id, name, th, en);
        ra.lifted = false;
        rb.lifted = true;
        var h = ui.du(10.5);
        ra.slideFrom(b > a ? h : -h);
        rb.slideFrom(b > a ? -h : h);
    }

    // The finger moved a whole row: the dragged pack trades places with its neighbour. dragIndex follows the data.
    function dragMove(y) {
        if (settingsPage.dragIndex < 0) return;
        var rowH = ui.du(10.5);
        var d = y - settingsPage.dragLastY;
        while (d >= rowH && settingsPage.dragIndex < rowsBox.count() - 1) {
            settingsPage.swapRows(settingsPage.dragIndex, settingsPage.dragIndex + 1);
            settingsPage.dragIndex++;
            settingsPage.dragLastY += rowH;
            d -= rowH;
        }
        while (d <= -rowH && settingsPage.dragIndex > 0) {
            settingsPage.swapRows(settingsPage.dragIndex, settingsPage.dragIndex - 1);
            settingsPage.dragIndex--;
            settingsPage.dragLastY -= rowH;
            d += rowH;
        }
    }

    function dragEnd() {
        if (settingsPage.dragIndex >= 0) rowAt(settingsPage.dragIndex).lifted = false;
        settingsPage.dragIndex = -1;
        scroller.scrollViewProperties.scrollMode = ScrollMode.Vertical;
    }

    function saveOrder() {
        var ids = [];
        for (var i = 0; i < rowsBox.count(); i++) ids.push(rowAt(i).packId);
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
                ? "asset:///images/ChatView/ic_folder_save.png"
                : "asset:///images/ChatView/ic_reorder.png"
            ActionBar.placement: ActionBarPlacement.OnBar
            enabled: settingsPage.reorderMode || settingsPage.packCount > 1
            onTriggered: {
                if (settingsPage.reorderMode) settingsPage.saveOrder();
                else settingsPage.reorderMode = true;
            }
        }
    ]

    attachedObjects: [
        ComponentDefinition {
            id: rowDef
            source: "asset:///sticker/StickerPackRow.qml"
        },
        Connections {
            target: app
            onDarkThemeChanged: { settingsPage.isDark = dark; }
        }
    ]

    Container {
        layout: DockLayout {}

        ScrollView {
            id: scroller
            scrollViewProperties { scrollMode: ScrollMode.Vertical }
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment: VerticalAlignment.Fill
            Container {
                id: rowsBox
                horizontalAlignment: HorizontalAlignment.Fill
                layout: StackLayout { orientation: LayoutOrientation.TopToBottom }
            }
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
