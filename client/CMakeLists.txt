# 桌面客户端（Qt Widgets GUI）；WIN32 表示无控制台窗口
add_executable(client WIN32
    src/main.cpp
    src/net/NetController.cpp
    src/net/NetController.h
    src/net/NetWorker.cpp
    src/net/NetWorker.h
    src/core/Session.cpp
    src/core/Session.h
    src/core/Types.h
    src/core/FriendModel.cpp
    src/core/FriendModel.h
    src/core/FriendRequestModel.cpp
    src/core/FriendRequestModel.h
    src/core/ConversationModel.cpp
    src/core/ConversationModel.h
    src/core/MessageModel.cpp
    src/core/MessageModel.h
    src/launcher/GameEntry.h
    src/launcher/GameLibrary.cpp
    src/launcher/GameLibrary.h
    src/launcher/GameProcessManager.cpp
    src/launcher/GameProcessManager.h
    src/ui/LoginDialog.cpp
    src/ui/LoginDialog.h
    src/ui/MainWindow.cpp
    src/ui/MainWindow.h
    src/ui/FriendListWidget.cpp
    src/ui/FriendListWidget.h
    src/ui/ChatPanel.cpp
    src/ui/ChatPanel.h
    src/ui/GameLauncherPanel.cpp
    src/ui/GameLauncherPanel.h
    src/ui/ImportGameDialog.cpp
    src/ui/ImportGameDialog.h
    src/ui/Theme.h
    src/ui/WindowChrome.cpp
    src/ui/WindowChrome.h
    resources/resources.qrc
)

# 让源码可用 "ui/xxx.h" 风格包含
target_include_directories(client PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(client PRIVATE
    Qt6::Widgets
    Qt6::Network
    protocol
)
