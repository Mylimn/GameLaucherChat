#pragma once

#include <QWidget>

#include "core/Types.h"

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTabWidget;
class QTimer;
class FriendModel;
class ConversationModel;
class FriendRequestModel;

// 好友面板（FR-2.1 / FR-2.2 / FR-2.3 / FR-2.5）+ 会话列表（FR-3.4）：
// 顶部「最近会话」段（有历史消息的会话，右侧未读角标）；
// 下方「好友」页 = 添加输入框 + 好友列表，「好友请求」页 = 待处理请求 + 接受/拒绝。
class FriendListWidget : public QWidget {
    Q_OBJECT

public:
    FriendListWidget(FriendModel* friends, FriendRequestModel* requests,
                     ConversationModel* conversations, QWidget* parent = nullptr);

    void showMessage(const QString& text, bool ok);

    // 当前打开的会话：两个列表里只有一个会出现选中态（另一个清除选中）
    void setActivePeer(qint64 userId);

signals:
    void friendSelected(qint64 userId);
    void conversationSelected(qint64 userId);
    void addFriendRequested(const QString& username);
    void requestHandled(qint64 fromUserId, bool accept);

private slots:
    void rebuildFriends();
    void rebuildConversations();
    void rebuildRequests();
    void onAddClicked();
    void updateEmptyStates();

private:
    QWidget* makeFriendItem(const FriendInfo& info, int unread);
    QWidget* makeConversationItem(const ConversationInfo& info);
    QWidget* makeRequestItem(const FriendRequestInfo& request);
    void     applyHighlight();

    FriendModel*        m_friends = nullptr;
    FriendRequestModel* m_requests = nullptr;
    ConversationModel*  m_conversations = nullptr;

    QTabWidget*  m_tabs = nullptr;
    QLineEdit*   m_usernameEdit = nullptr;
    QPushButton* m_addButton = nullptr;
    QLabel*      m_messageLabel = nullptr;
    QLabel*      m_conversationTitle = nullptr;
    QListWidget* m_conversationList = nullptr;
    QListWidget* m_friendList = nullptr;
    QListWidget* m_requestList = nullptr;
    QLabel*      m_friendEmpty = nullptr;
    QLabel*      m_requestEmpty = nullptr;
    QTimer*      m_messageTimer = nullptr;
    qint64       m_activePeer = 0;
};
