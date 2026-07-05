import bb.cascades 1.4

Container {
    id: emojiBtnRoot
    property string emojiFile: ""
    property string emojiCategory: "people"

    signal emojiTapped(string file, string category)

    preferredHeight: ui.du(7)
    maxWidth: ui.du(7.5)
    layout: DockLayout {}
    topPadding:    ui.du(0.5)
    bottomPadding: ui.du(0.5)
    leftPadding:   ui.du(0.5)
    rightPadding:  ui.du(0.5)

    ImageButton {
        horizontalAlignment: HorizontalAlignment.Center
        verticalAlignment:   VerticalAlignment.Center
        preferredWidth:  ui.du(6.5)
        preferredHeight: ui.du(6.5)
        defaultImageSource: "asset:///images/emoji/" + emojiBtnRoot.emojiCategory + "/" + emojiBtnRoot.emojiFile
        pressedImageSource: "asset:///images/emoji/" + emojiBtnRoot.emojiCategory + "/" + emojiBtnRoot.emojiFile
        onClicked: {
            emojiBtnRoot.emojiTapped(emojiBtnRoot.emojiFile, emojiBtnRoot.emojiCategory);
        }
    }
}
