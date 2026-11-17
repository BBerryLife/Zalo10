import bb.cascades 1.4
import bb.system 1.0

// One of the four navigation actions on the action bar (Chats, Feeds, Contacts, Groups).
// "feeds" has no tab yet: it only shows the "under development" dialog. The other targets emit
// picked(target) and main.qml switches the TabbedPane.
ActionItem {
    id: tabAction
    property string target: ""
    signal picked(string target)

    ActionBar.placement: ActionBarPlacement.OnBar
    title:       target === "chats" ? "Chats" : (target === "feeds" ? "Feeds" : (target === "contacts" ? "Contacts" : "Groups"))
    imageSource: target === "chats" ? "asset:///images/ic_bbm.png"
               : (target === "feeds" ? "asset:///images/updates.png"
               : (target === "contacts" ? "asset:///images/ic_contact.png" : "asset:///images/ic_groups_white.png"))

    onTriggered: {
        if (target === "feeds") feedsDevDialog.show();
        else tabAction.picked(target);
    }

    attachedObjects: [
        SystemDialog {
            id: feedsDevDialog
            title: "Feeds"
            body: "This feature is still under development."
            confirmButton.label: "OK"
            cancelButton.label: ""
            cancelButton.enabled: false
        }
    ]
}
