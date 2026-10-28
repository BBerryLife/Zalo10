APP_NAME = Zalo10

CONFIG += qt warn_on cascades10

PKGNAME = com.berrylife.zalo10
# Version is read at runtime from bar-descriptor.xml (bb::ApplicationInfo().version());
# to release, only update <versionNumber> there.

QT += network script gui

LIBS += -lbbcascades -lbbsystem -lbb -lbbplatform -lbbdevice -lbbpim
# -lunifieddatasourcec is the real UDS library name ("uds" fails with "cannot find -luds").
LIBS += -lunifieddatasourcec
# -lbbcascadespickers: ContactPicker needs the real header and an explicit link.
LIBS += -lbbcascadespickers
LIBS += -lQtNetwork -lQtScript -lQtGui
LIBS += -lssl -lcrypto
LIBS += -lsocket   # POSIX sockets used directly by PhoneSyncCore::httpsGet
LIBS += -lsqlite3

# libwebpdecoder (vendored decode-only build for QNX armv7le/gcc 4.6.3, see
# src/third_party/webp/README.md): decodes sticker .webp since Cascades has no WebP codec.
LIBS += -L$$PWD/src/third_party/webp/lib -lwebpdecoder
INCLUDEPATH += $$PWD/src/third_party/webp/include

INCLUDEPATH += src

# src/third_party/xz: XZ Embedded (vendored, 0BSD) for phone sync. Built in qcc's default C mode;
# do NOT add -std=gnu99 (qcc: unknown option).
INCLUDEPATH += $$(QNX_TARGET)/usr/include/qt4/QtGui

HEADERS += \
    src/utils/ZaloCookieJar.hpp \
    src/core/ServiceHandoff.hpp \
    src/utils/LogSanitizer.hpp \
    src/utils/ZaloNetworkManager.hpp \
    src/core/PhoneSyncCore.hpp \
    src/applicationui.hpp \
    src/core/ZaloService.hpp \
    src/utils/ZaloServiceUtils.hpp \
    src/ui/ActiveFrameCover.hpp \
    src/core/HubIntegration.hpp

SOURCES += \
    src/main.cpp \
    src/app/ApplicationUI.cpp \
    src/app/ApplicationUI_Share.cpp \
    src/app/ApplicationUI_Update.cpp \
    src/core/auth/ZaloService_Auth_Cookie.cpp \
    src/core/auth/ZaloService_Auth_QRFinish.cpp \
    src/core/auth/ZaloService_Auth_QRSteps.cpp \
    src/core/contacts/ZaloService_Contacts_Actions.cpp \
    src/core/contacts/ZaloService_Contacts_Board.cpp \
    src/core/contacts/ZaloService_Contacts_Conversations.cpp \
    src/core/contacts/ZaloService_Contacts_Friends.cpp \
    src/core/contacts/ZaloService_Contacts_Media.cpp \
    src/core/contacts/ZaloService_Contacts_Polls.cpp \
    src/core/db/ZaloService_Db_Messages.cpp \
    src/core/db/ZaloService_Db_Store.cpp \
    src/core/messages/ZaloService_Messages_Actions.cpp \
    src/core/messages/ZaloService_Messages_Download.cpp \
    src/core/messages/ZaloService_Messages_Fetch.cpp \
    src/core/messages/ZaloService_Messages_FileVideo.cpp \
    src/core/messages/ZaloService_Messages_Notify.cpp \
    src/core/messages/ZaloService_Messages_Photo.cpp \
    src/core/messages/ZaloService_Messages_PhotoFetch.cpp \
    src/core/messages/ZaloService_Messages_Send.cpp \
    src/core/network/ZaloService_Network_Download.cpp \
    src/core/network/ZaloService_Network_Request.cpp \
    src/core/network/ZaloService_Network_SessionKey.cpp \
    src/core/phonesync/ZaloService_PhoneSync_Flow.cpp \
    src/core/phonesync/ZaloService_PhoneSync_Tick.cpp \
    src/core/websocket/ZaloService_WebSocket_Dispatch.cpp \
    src/core/websocket/ZaloService_WebSocket_Events.cpp \
    src/core/websocket/ZaloService_WebSocket_Frames.cpp \
    src/core/websocket/ZaloService_WebSocket_History.cpp \
    src/core/websocket/ZaloService_WebSocket_Push.cpp \
    src/core/websocket/ZaloService_WebSocket_Transport.cpp \
    src/core/ZaloService.cpp \
    src/core/ZaloService_ContactPicker.cpp \
    src/core/ZaloService_Crypto.cpp \
    src/core/ZaloService_Sync.cpp \
    src/core/PhoneSyncCore.cpp \
    src/third_party/xz/xz_crc32.c \
    src/third_party/xz/xz_crc64.c \
    src/third_party/xz/xz_dec_stream.c \
    src/third_party/xz/xz_dec_lzma2.c \
    src/ui/ActiveFrameCover.cpp \
    src/core/HubIntegration.cpp

OTHER_FILES += \
    bar-descriptor.xml \
    assets/main.qml \
    assets/ActiveFrameCover.qml \
    assets/images/ActiveFrame/activeframe_zl10_big.png \
    assets/images/ActiveFrame/activeframe_zl10_medium.png \
    assets/images/ActiveFrame/Activeframe_zl10_Small.png \
    assets/ChatList.qml \
    assets/ChatView.qml \
    assets/InfoDialog.qml \
    assets/ConfirmDialog.qml \
    assets/EmojiPanel.qml \
    assets/emoji/EmojiPeoplePage.qml \
    assets/emoji/EmojiNaturePage.qml \
    assets/emoji/EmojiObjectsPage.qml \
    assets/emoji/EmojiPlacesPage.qml \
    assets/emoji/EmojiSymbolsPage.qml \
    assets/emoji/EmojiButton.qml \
    assets/ContactsTab.qml \
    assets/LoginView.qml \
    assets/ProfileView.qml \
    assets/SettingsSheet.qml \
    assets/AboutSheet.qml \
    assets/PollVoteSheet.qml \
    assets/QuickMessagesSheet.qml \
    assets/QuickMessageEditSheet.qml \
    assets/AttachPickerSheet.qml \
    assets/VoiceNoteSheet.qml \
    assets/ChangelogPage.qml \
    assets/ChatsTab.qml \
    assets/CreateNoteSheet.qml \
    assets/CreatePollSheet.qml \
    assets/ForwardPickerSheet.qml \
    assets/GroupBoardSheet.qml \
    assets/GroupsTab.qml \
    assets/InvitesTab.qml \
    assets/NoteViewerSheet.qml \
    assets/PinboardBar.qml \
    assets/PollVotersSheet.qml \
    assets/ReactionPickerSheet.qml \
    assets/SharePickerSheet.qml \
    assets/board/BoardListItem.qml \
    assets/board/BoardTitleBar.qml \
    assets/chat/BubbleActionsAdmin.qml \
    assets/chat/BubbleActionsFailed.qml \
    assets/chat/BubbleActionsMember.qml \
    assets/chat/BubbleCall.qml \
    assets/chat/BubbleLink.qml \
    assets/chat/BubblePhoto.qml \
    assets/chat/BubbleSticker.qml \
    assets/chat/BubbleVideo.qml \
    assets/chat/ChatAttachPreviewBar.qml \
    assets/chat/ChatBubbleItem.qml \
    assets/chat/ChatMessageFlowConnections.qml \
    assets/chat/ChatQuickMessageBar.qml \
    assets/chat/ChatReplyPreviewBar.qml \
    assets/chat/ChatThreadEventConnections.qml \
    assets/chat/ChatTitleBar.qml \
    assets/chat/PollCard.qml \
    assets/chat/RowReactions.qml \
    assets/lists/ChatsListItem.qml \
    assets/lists/ChatsServiceConnections.qml \
    assets/lists/ChatsTitleBar.qml \
    assets/lists/GroupsListItem.qml \
    assets/lists/GroupsServiceConnections.qml \
    assets/lists/GroupsTitleBar.qml
