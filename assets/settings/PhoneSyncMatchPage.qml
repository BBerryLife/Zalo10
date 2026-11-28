import bb.cascades 1.4

// Phone-sync conversations that could not be matched automatically (zService.phoneSyncUnmatched()).
// Tapping one opens PhoneSyncPickPage to choose the friend or group it belongs to.
Page {
    id: page
    property variant sheet
    property variant dataPage        // DataPage, to refresh its "Match Conversations (N)" button
    titleBar: SettingsTitleBar { heading: qsTr("Match Conversations") }

    function reload() {
        var items = zService.phoneSyncUnmatched();
        unmatchedModel.clear();
        if (items.length > 0) unmatchedModel.append(items);
        emptyLabel.visible = (items.length === 0);
        if (page.dataPage) page.dataPage.refreshUnmatched();
    }

    function openPicker(item) {
        var p = pickDef.createObject();
        if (!p) return;
        p.matchPage = page;
        p.item = item;
        page.sheet.pushPage(p);
    }

    onCreationCompleted: { page.reload(); }

    Container {
        horizontalAlignment: HorizontalAlignment.Fill
        verticalAlignment: VerticalAlignment.Fill

        Container {
            horizontalAlignment: HorizontalAlignment.Fill
            leftPadding: ui.du(2); rightPadding: ui.du(2); topPadding: ui.du(2); bottomPadding: ui.du(1)
            Label {
                text: qsTr("These conversations from your phone could not be matched automatically. Tap one and choose the friend or group it belongs to; its messages are then imported, and later syncs will match it by themselves.")
                multiline: true
                textStyle.color: Color.Gray
                textStyle.base: SystemDefaults.TextStyles.SmallText
            }
        }

        Label {
            id: emptyLabel
            visible: false
            horizontalAlignment: HorizontalAlignment.Center
            topMargin: ui.du(4)
            text: qsTr("Nothing left to match.")
            textStyle.color: Color.Gray
        }

        ListView {
            id: list
            horizontalAlignment: HorizontalAlignment.Fill
            dataModel: unmatchedModel
            listItemComponents: [
                ListItemComponent {
                    type: ""
                    Container {
                        horizontalAlignment: HorizontalAlignment.Fill
                        leftPadding: ui.du(2); rightPadding: ui.du(2); topPadding: ui.du(1.5)
                        Label {
                            text: (ListItemData.isGroup ? qsTr("Group chat") : qsTr("Chat")) + "  ·  " + ListItemData.count
                                  + (ListItemData.count === 1 ? " message" : " messages")
                            textStyle.base: SystemDefaults.TextStyles.TitleText
                        }
                        Label {
                            text: ListItemData.preview || ""
                            multiline: true
                            textStyle.color: Color.Gray
                            textStyle.base: SystemDefaults.TextStyles.SmallText
                        }
                        Divider { topMargin: ui.du(1) }
                    }
                }
            ]
            onTriggered: {
                var item = dataModel.data(indexPath);
                if (item) page.openPicker(item);
            }
        }
    }

    attachedObjects: [
        ArrayDataModel { id: unmatchedModel },
        ComponentDefinition { id: pickDef; source: "asset:///settings/PhoneSyncPickPage.qml" }
    ]
}
