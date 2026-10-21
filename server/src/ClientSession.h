#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>

#include "protocol/Frame.h"
#include "protocol/MessageType.h"

class QTimer;
class QTcpSocket;
class UserRepository;
class FriendRepository;
class MessageRepository;
class SessionRegistry;
struct MessageRecord;

// 单条客户端连接：拆帧、分发、回包（见 docs/TECH_DESIGN.md §8）。
// 运行在服务端主线程（单线程事件循环），因此不需要锁。
class ClientSession : public QObject {
    Q_OBJECT

public:
    ClientSession(QTcpSocket* socket,
                  UserRepository& users,
                  FriendRepository& friends,
                  MessageRepository& messages,
                  SessionRegistry& registry,
                  QObject* parent = nullptr);
    ~ClientSession() override;

    void sendFrame(MsgType type, const QJsonObject& payload);

    qint64  userId() const { return m_userId; }
    QString username() const { return m_username; }
    bool    isLoggedIn() const { return m_userId != 0; }

signals:
    void closed(ClientSession* session); // 连接断开，交由 MockServer 回收

private slots:
    void onReadyRead();
    void onSocketDisconnected();
    void onIdleTimeout();

private:
    // ---- 分发 ----
    void handleFrame(MsgType type, const QJsonObject& payload);

    // ---- 会话类 0x01xx ----
    void handleRegister(const QJsonObject& payload);
    void handleLogin(const QJsonObject& payload);
    void handleLogout();
    void handleHeartbeat(const QJsonObject& payload);

    // ---- 好友类 0x02xx ----
    void handleFriendList();
    void handleFriendRequestList();
    void handleFriendAdd(const QJsonObject& payload);
    void handleFriendHandle(const QJsonObject& payload);
    void handleFriendRemove(const QJsonObject& payload);

    // ---- 私聊类 0x03xx ----
    void handleChatSend(const QJsonObject& payload);
    void handleChatHistory(const QJsonObject& payload);
    void handleChatRead(const QJsonObject& payload);

    // ---- 登录/登出副作用（§4.3 时序 + §8 在线状态）----
    void afterLoginPush();
    void unregisterAndBroadcastOffline();
    void broadcastPresence(bool online);
    void deliverOfflineMessages();
    void deliverPendingRequests();

    // 把某人的最新好友列表主动下发（接受/删除好友后双方都要刷新）
    void pushFriendList(qint64 userId);
    QJsonObject buildFriendListPayload(qint64 userId) const;
    void pushChat(qint64 targetUserId, const MessageRecord& msg, bool offline);

    void sendError(int code, const QString& message, MsgType refType);

    QString peerName() const;
    void log(const QString& text) const;

    QTcpSocket*    m_socket = nullptr;
    UserRepository&    m_users;
    FriendRepository&  m_friends;
    MessageRepository& m_messages;
    SessionRegistry&   m_registry;
    FrameDecoder   m_decoder;
    QTimer*        m_idleTimer = nullptr;

    qint64  m_userId = 0;   // 0 = 未登录
    QString m_username;
    bool    m_announcedClosed = false;
};
