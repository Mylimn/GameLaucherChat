#pragma once

#include <QWidget>

#include "core/Types.h"

class QLabel;
class QLineEdit;
class QListView;
class QPushButton;
class Session;
class MessageModel;

// 会话面板（FR-3.1 / FR-3.2 / FR-3.3 / FR-3.5 / FR-3.6 / FR-3.7 / FR-2.4）：
// 顶部为对端信息与删除好友；中部为消息气泡列表（含分页加载更早消息）；底部为输入框。
// 直接持有 Session：发送、拉历史等动作都通过会话层发出，不经过 MainWindow 中转。
class ChatPanel : public QWidget {
    Q_OBJECT

public:
    ChatPanel(Session* session, MessageModel* model, QWidget* parent = nullptr);

    // 切换会话（peer.userId == 0 表示回到「未选择」空态）
    void openConversation(const FriendInfo& peer);
    void setPeerOnline(bool online);
    // FR-3.7：当前会话的对端读到该位置，把自己发出的消息标为已读
    void onPeerRead(qint64 upToServerMsgId);

    // 当前打开的会话对象；未被选中时为 0
    qint64 peerUserId() const { return m_peerUserId; }
    QString peerDisplayName() const { return m_peerDisplayName; }

    // 气泡上方的来源标注：对方显示其昵称，自己显示「昵称 · me」
    QString senderLabel(bool mine) const;

signals:
    void removeFriendRequested(qint64 userId);
    // 本地已把消息交给会话层（含断线入队），供会话列表即时更新最后一条预览
    void messageSent(qint64 peerUserId, const QString& content, qint64 ts);

private slots:
    void onSendClicked();
    void onLoadMoreClicked();
    void onHistoryReceived(qint64 peerUserId, bool hasMore,
                           const QList<ChatMessageInfo>& messages, qint64 peerReadUpTo);
    void onChatReceived(const ChatMessageInfo& message);
    void onSendAck(const QString& clientMsgId, bool ok, qint64 serverMsgId, qint64 ts);

private:
    void updatePeerHeader();
    void refreshView();
    void scrollToBottom();
    void syncPendingOutgoing();

    Session*      m_session = nullptr;
    MessageModel* m_model = nullptr;

    qint64  m_peerUserId = 0;
    QString m_peerDisplayName;
    QString m_peerUsername;
    bool    m_peerOnline = false;
    bool    m_hasMore = false;
    bool    m_loadingHistory = false;

    QLabel*      m_peerNameLabel = nullptr;
    QLabel*      m_peerStatusLabel = nullptr;
    QPushButton* m_removeButton = nullptr;
    QPushButton* m_loadMoreButton = nullptr;
    QLabel*      m_emptyLabel = nullptr;
    QListView*   m_messageList = nullptr;
    QLineEdit*   m_input = nullptr;
    QPushButton* m_sendButton = nullptr;
};
