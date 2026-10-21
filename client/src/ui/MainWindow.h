#pragma once

#include <QMainWindow>

#include "core/Session.h"

class QLabel;
class QPushButton;
class QTabWidget;
class ChatPanel;
class FriendListWidget;
class FriendModel;
class FriendRequestModel;
class ConversationModel;
class MessageModel;
class GameLibrary;
class GameProcessManager;
class GameLauncherPanel;

// 主窗口（M3/M4）：左侧栏 = 自身信息 + 连接状态 + 好友面板 + 登出；
// 右侧 = 页签（会话 / 游戏）。模型与 UI 的连接全部在这里完成，UI 之间不互相依赖。
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(Session* session, QWidget* parent = nullptr);
    ~MainWindow() override;

signals:
    void loggedOut(); // 用户已登出：由 main.cpp 决定是否回到登录页
    void closed();    // 窗口已关闭

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onLogoutClicked();
    void onSessionLoggedOut();
    void onStateChanged(Session::State state);
    void onErrorOccurred(const QString& message);
    void onReconnectScheduled(int attempt, int delayMs);
    void onConnectionRestored();
    void onReloginFailed(const QString& message);

    void onFriendListReceived(const QList<FriendInfo>& friends);
    void onPresenceChanged(qint64 userId, bool online);
    void onFriendRequestReceived(const FriendRequestInfo& request);
    void onFriendRequestsReceived(const QList<FriendRequestInfo>& requests);
    void onFriendAddResult(bool ok, const QString& message);
    void onFriendHandleResult(bool ok, const QString& message, qint64 fromUserId);
    void onFriendRemoveResult(bool ok, qint64 userId);
    void onChatReceived(const ChatMessageInfo& message);
    void onConversationHistory(qint64 peerUserId, bool hasMore,
                               const QList<ChatMessageInfo>& messages, qint64 peerReadUpTo);

    void onFriendSelected(qint64 userId);
    void onAddFriendRequested(const QString& username);
    void onRequestHandled(qint64 fromUserId, bool accept);
    void onRemoveFriendRequested(qint64 userId);

private:
    void buildUi();
    void updateStatus(Session::State state);
    void setStatus(const QString& text, const QString& key);

    Session* m_session = nullptr;

    FriendModel*        m_friendModel = nullptr;
    FriendRequestModel* m_requestModel = nullptr;
    ConversationModel*  m_conversationModel = nullptr;
    MessageModel*       m_messageModel = nullptr;
    GameLibrary*        m_gameLibrary = nullptr;
    GameProcessManager* m_gameProcesses = nullptr;

    QLabel*            m_nameLabel = nullptr;
    QLabel*            m_accountLabel = nullptr;
    QLabel*            m_statusLabel = nullptr;
    QPushButton*       m_logoutButton = nullptr;
    FriendListWidget*  m_friendList = nullptr;
    QTabWidget*        m_rightTabs = nullptr;
    ChatPanel*         m_chatPanel = nullptr;
    GameLauncherPanel* m_gamePanel = nullptr;
};
