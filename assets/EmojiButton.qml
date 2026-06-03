import bb.cascades 1.4

Container {
    id: emojiBtnRoot
    property string emojiFile: ""
    property string emojiCategory: "people"

    signal emojiTapped(string file, string category)

    preferredWidth:  ui.du(5.5)
    preferredHeight: ui.du(5.5)
    layout: DockLayout {}

    ImageButton {
        horizontalAlignment: HorizontalAlignment.Center
        verticalAlignment:   VerticalAlignment.Center
        preferredWidth:  ui.du(5)
        preferredHeight: ui.du(5)
        defaultImageSource: "asset:///images/emoji/" + emojiBtnRoot.emojiCategory + "/" + emojiBtnRoot.emojiFile
        pressedImageSource: "asset:///images/emoji/" + emojiBtnRoot.emojiCategory + "/" + emojiBtnRoot.emojiFile
        onClicked: {
            emojiBtnRoot.emojiTapped(emojiBtnRoot.emojiFile, emojiBtnRoot.emojiCategory);
        }
    }
}
