// ChatList.qml
import bb.cascades 1.4
import QtQuick 1.0

Page {
    id: chatListPage
    property bool fetchStarted: false
    
    // - HEADER NATIVE MÀU XANH #2575fc -
    titleBar: TitleBar {
        kind: TitleBarKind.FreeForm
        kindProperties: FreeFormTitleBarKindProperties {
            content: Container {
                background: Color.create("#2575fc")
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment: VerticalAlignment.Fill
                layout: DockLayout {}
                leftPadding: ui.du(2.5)
                rightPadding: ui.du(2.5)
                
                Label {
                    text: "Zalo10"
                    textStyle {
                        color: Color.White
                        base: SystemDefaults.TextStyles.TitleText
                        fontWeight: FontWeight.Bold
                    }
                    verticalAlignment: VerticalAlignment.Center
                    horizontalAlignment: HorizontalAlignment.Left
                }
            }
        }
    }
    
    onCreationCompleted: {
        if (!fetchStarted && zService.loggedIn) {
            fetchStarted = true;
            zService.fetchConversations();
            loadingBar.visible = true;
        }
    }
    
    // - ACTION BAR DƯỚI ĐÁY CHUẨN BB10 -
    actions: [
        ActionItem {
            title: "Refresh"
            imageSource: "asset:///images/ic_sync.png"
            ActionBar.placement: ActionBarPlacement.OnBar
            onTriggered: {
                chatListPage.fetchStarted = false;
                threadModel.clear();
                zService.fetchConversations();
                loadingBar.visible = true;
                emptyLabel.visible = false;
            }
        }
    ]
    
    content: Container {
        layout: DockLayout {}
        horizontalAlignment: HorizontalAlignment.Fill
        verticalAlignment: VerticalAlignment.Fill
        
        ListView {
            id: chatListView
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment: VerticalAlignment.Fill
            
            dataModel: ArrayDataModel { id: threadModel }
            
            // QUAN TRỌNG: BẮT BUỘC PHẢI CÓ HÀM NÀY ĐỂ RENDER ĐÚNG CUSTOM UI
            function itemType(data, indexPath) {
                return "chatItem";
            }
            
            listItemComponents: [
                ListItemComponent {
                    type: "chatItem" 
                    
                    CustomListItem {
                        id: chatItem
                        dividerVisible: true
                        
                        Container {
                            horizontalAlignment: HorizontalAlignment.Fill
                            verticalAlignment: VerticalAlignment.Fill
                            layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                            leftPadding: ui.du(2.5)
                            rightPadding: ui.du(2.5)
                            topPadding: ui.du(1.5)
                            bottomPadding: ui.du(1.5)
                            
                            // ================= BÊN TRÁI: AVATAR =================
                            Container {
                                verticalAlignment: VerticalAlignment.Center
                                layout: DockLayout {}
                                preferredWidth: ui.du(9.0)
                                preferredHeight: ui.du(9.0)
                                
                                ImageView {
                                    imageSource: {
                                        if (ListItemData.localAvatar && ListItemData.localAvatar.length > 0) {
                                            return "file://" + ListItemData.localAvatar
                                        } else if (ListItemData.avatar && ListItemData.avatar.length > 0) {
                                            return ListItemData.avatar
                                        } else {
                                            return "asset:///images/blank.png"
                                        }
                                    }
                                    horizontalAlignment: HorizontalAlignment.Fill
                                    verticalAlignment: VerticalAlignment.Fill
                                    scalingMethod: ScalingMethod.AspectFill
                                }
                                
                                // Spark badge khi có tin nhắn mới chưa đọc
                                ImageView {
                                    visible: ListItemData.hasUnread === true
                                    imageSource: "asset:///images/cs_spark_small.png"
                                    preferredWidth:  ui.du(3.5)
                                    preferredHeight: ui.du(3.5)
                                    horizontalAlignment: HorizontalAlignment.Right
                                    verticalAlignment:   VerticalAlignment.Top
                                }

                                // Fallback: chữ cái đầu khi không có avatar
                                Container {
                                    visible: {
                                        var hasAvatar = (ListItemData.localAvatar && ListItemData.localAvatar.length > 0) ||
                                        (ListItemData.avatar && ListItemData.avatar.length > 0);
                                        return !hasAvatar;
                                    }
                                    horizontalAlignment: HorizontalAlignment.Center
                                    verticalAlignment: VerticalAlignment.Center
                                    
                                    Label {
                                        text: {
                                            var name = ListItemData.name || ListItemData.displayName || "?";
                                            return name.substring(0, 1).toUpperCase();
                                        }
                                        textStyle {
                                            base: SystemDefaults.TextStyles.HeadingText
                                            color: Color.White
                                            fontWeight: FontWeight.Bold
                                            textAlign: TextAlign.Center
                                        }
                                    }
                                }
                            }
                            
                            // ================= BÊN PHẢI: NAME & MESSAGE =================
                            Container {
                                verticalAlignment: VerticalAlignment.Center
                                leftMargin: ui.du(2.2)
                                layout: StackLayout { orientation: LayoutOrientation.TopToBottom }
                                horizontalAlignment: HorizontalAlignment.Fill
                                
                                Label {
                                    text: ListItemData.name || ListItemData.displayName || "Unknown User"
                                    textStyle {
                                        base: SystemDefaults.TextStyles.TitleText
                                        fontWeight: FontWeight.Bold
                                    }
                                    bottomMargin: ui.du(0.2)
                                }
                                
                                Label {
                                    text: {
                                        var lm = ListItemData.lastMessage || ListItemData.lastMsg || "";
                                        if (lm.length === 0) return "No messages yet";
                                        var prefix = "";
                                        if (ListItemData.lastMsgIsMine === true || ListItemData.lastMsgIsMine === "true") {
                                            prefix = "Me: ";
                                        } else if (ListItemData.lastSenderName && ListItemData.lastSenderName.length > 0) {
                                            prefix = ListItemData.lastSenderName.split(" ")[0] + ": ";
                                        }
                                        return prefix + lm;
                                    }
                                    textStyle {
                                        base: SystemDefaults.TextStyles.SubtitleText
                                        color: (ListItemData.hasUnread === true) ? Color.create("#2575fc") : Color.DarkGray
                                        fontWeight: (ListItemData.hasUnread === true) ? FontWeight.Bold : FontWeight.Normal
                                    }
                                    multiline: false
                                }
                            }
                        }
                    }
                }
            ]
        }
        
        ActivityIndicator {
            id: loadingBar
            horizontalAlignment: HorizontalAlignment.Center
            verticalAlignment: VerticalAlignment.Center
            preferredWidth: ui.du(10)
            preferredHeight: ui.du(10)
            visible: false
        }
        
        Label {
            id: emptyLabel
            text: "No conversations found"
            visible: false
            horizontalAlignment: HorizontalAlignment.Center
            verticalAlignment: VerticalAlignment.Center
            textStyle { 
                base: SystemDefaults.TextStyles.BodyText 
            }
        }
    }
    
    attachedObjects: [
        Connections {
            target: zService
            
            onConversationsReady: {
                loadingBar.visible = false;
                if (threads && threads.length > 0) {
                    var firstIsGroup = threads[0].isGroup;
                    if (firstIsGroup) {
                        threadModel.clear();
                    }
                    for (var i = 0; i < threads.length; i++) {
                        var item = threads[i];
                        item.localAvatar = "";
                        item.hasUnread   = false;
                        item.lastMessage = item.lastMessage || item.lastMsg || "";
                        threadModel.append(item);
                    }
                    emptyLabel.visible = false;
                } else if (threadModel.size() === 0) {
                    emptyLabel.visible = true;
                }
            }

            onAvatarReady: {
                for (var i = 0; i < threadModel.size(); i++) {
                    var d = threadModel.value(i);
                    if (d.threadId === threadId) {
                        d.localAvatar = localPath;
                        threadModel.replace(i, d);
                        break;
                    }
                }
            }

            // Helper: update thread lastMessage + move to top
            onNewMessage: {
                // Find thread in model
                var tid = threadId;
                var snippet = "";
                if (message.msgType === 2 || message.msgType === "2") {
                    snippet = "[Photo]";
                } else {
                    snippet = (message.content || "").substring(0, 60);
                }
                var isMine = (message.isMine === true || message.isMine === "true" || message.isMine === 1);
                var senderName = message.dName || "";

                for (var i = 0; i < threadModel.size(); i++) {
                    var d = threadModel.value(i);
                    if (d.threadId === tid || d.uid === tid) {
                        d.lastMessage     = snippet;
                        d.lastMsgIsMine   = isMine;
                        d.lastSenderName  = senderName;
                        d.hasUnread       = !isMine; // unread chỉ khi người khác gửi
                        // Move to top: remove then insert at 0
                        threadModel.removeAt(i);
                        threadModel.insert(0, d);
                        return;
                    }
                }
            }

            onMessageSent: {
                // Tin tôi gửi thành công — update lastMessage (nếu có pendingMsg từ ChatView)
                // Chỉ mark isMine, không set hasUnread
                // threadId và content không available trực tiếp ở đây nên
                // ta dùng signal từ main.qml nếu cần; bỏ qua để tránh phức tạp
            }
        }
    ]
}