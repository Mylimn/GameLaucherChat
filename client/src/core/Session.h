#pragma once

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QtGlobal>

#include "core/Types.h"
#include "protocol/MessageType.h"

class QTimer;
class NetController;

// 会话层：持有登录态与自身信息，作为协议入站消息的分发中枢（见 docs/TECH_DESIGN.md §7）。
// 只依赖 NetController，不直接操作 Socket。所有信号都在主线程发出，UI 直接消费。
class Session : public QObject {
    Q_OBJECT

public:
    enum class State { Disconnected, Connecting, Connected, LoggedIn };
    Q_ENUM(State)

    explicit Session(NetController* net, QObject* parent = nullptr);

    // 建立连接（默认本机 9527）；login()/registerAccount() 会在未连接时自动先连接
    void connectToServer(const QString& host = QStringLiteral("127.0.0.1"), quint16 port = 9527);

    // ---- 服务端地址（登录页可改，持久化在 %APPDATA%\GameLauncherChat\client.ini）----
    QString serverHost() const { return m_host; }
    quint16 serverPort() const { return m_port; }
    // 上次保存的地址（"host:port"）；没保存过时返回默认的 127.0.0.1:9527
    static QString savedServerAddress();
    static void    saveServerAddress(const QString& address);
    // 解析 "host:port"；格式非法（缺端口/端口越界/主机为空）返回 false
    static bool    parseServerAddress(const QString& text, QString* host, quint16* port);

    // 登录 / 注册：二者共用一条连接意图队列，连接就绪后自动发出请求
    void login(const QString& username, const QString& password);
    void registerAccount(const QString& username, const QString& password, const QString& displayName);

    // 主动登出（不关闭连接，便于回到登录页再次登录）
    void logout();

    // ---- 好友（FR-2）----
    void requestFriendList();
    // 主动拉取待处理请求：登录后的补推可能丢失，UI 就绪时以数据库为准再拉一次全量
    void requestFriendRequests();
    void addFriend(const QString& username, const QString& remark);
    void handleFriendRequest(qint64 fromUserId, bool accept);
    void removeFriend(qint64 userId);

    // ---- 私聊（FR-3）----
    // 发送文本；返回本次的 clientMsgId，供 UI 关联乐观上屏的那条消息。
    // 未登录（断线重连中）时不直接失败，而是进本地待发队列，重登后按序补发
    QString sendChat(qint64 toUserId, const QString& content);
    void requestHistory(qint64 peerUserId, qint64 beforeTs = 0, int limit = 30);
    void reportRead(qint64 peerUserId, qint64 upToServerMsgId);
    // 本地待发队列中发往该会话的消息；ChatPanel 在会话重开/历史刷新后据此把
    // 「发送中」的消息补回列表，避免只存在于本地的消息被历史覆盖掉
    QList<ChatMessageInfo> pendingOutgoing(qint64 peerUserId) const;

    State   state() const { return m_state; }
    qint64  userId() const { return m_userId; }
    QString username() const { return m_username; }
    QString displayName() const { return m_displayName; }

    // 最近一次错误文案：自动重登失败退回登录页时，登录框据此提示原因
    QString lastError() const { return m_lastError; }

    // 登录成功后服务端会立刻补推好友列表/好友请求/离线消息，而主窗口此时尚未创建
    // （登录对话框要等 loginSucceeded 处理完才返回）。在此之前把推送缓存起来，
    // 由主窗口构造时调用 notifyUiReady() 取走，避免首批推送丢失。
    void notifyUiReady();
    // 返回 false 表示登录后还没收到过好友列表（此时不应覆盖空列表）
    bool                     takePendingFriends(QList<FriendInfo>* out);
    QList<FriendRequestInfo> takePendingRequests();
    QList<ChatMessageInfo>   takePendingChats();

signals:
    void stateChanged(Session::State state);
    void loginSucceeded();
    void loginFailed(const QString& message);
    void registerSucceeded(const QString& message);
    void registerFailed(const QString& message);
    void loggedOut();
    void errorOccurred(const QString& message);

    // ---- 自动重连（FR-1.3）----
    // 已调度下一次重连（第 attempt 次，delayMs 毫秒后）
    void reconnectScheduled(int attempt, int delayMs);
    // 断线后已重连并自动重新登录成功
    void connectionRestored();
    // 自动重登被服务端拒绝：凭据已清除，UI 应退回登录页
    void reloginFailed(const QString& message);

    // 好友
    void friendListReceived(const QList<FriendInfo>& friends);
    void presenceChanged(qint64 userId, bool online);
    void friendRequestReceived(const FriendRequestInfo& request);
    // 服务端返回的全量待处理请求（客户端整体覆盖，而不是增量合并）
    void friendRequestsReceived(const QList<FriendRequestInfo>& requests);
    void friendAddResult(bool ok, const QString& message);
    void friendHandleResult(bool ok, const QString& message, qint64 fromUserId);
    void friendRemoveResult(bool ok, qint64 userId);

    // 私聊
    void chatSendAck(const QString& clientMsgId, bool ok, qint64 serverMsgId, qint64 ts);
    void chatReceived(const ChatMessageInfo& message);
    // peerReadUpTo = 对端已读到自己的哪条（0 = 未读）；用于重建气泡的「已读」状态
    void chatHistoryReceived(qint64 peerUserId, bool hasMore,
                             const QList<ChatMessageInfo>& messages, qint64 peerReadUpTo);
    // FR-3.7：对端已读到该位置
    void chatReadReceived(qint64 peerUserId, qint64 upToServerMsgId);

private slots:
    void onConnected();
    void onDisconnected();
    void onFrame(MsgType type, const QJsonObject& payload);
    void onNetError(const QString& message);
    void onReconnectTimeout();

private:
    enum class Intent { None, Login, Register, Relogin };

    // 断线期间发出的消息：重登成功后按顺序补发，收到 ChatSendResp 才出队
    struct PendingChat {
        qint64  toUserId = 0;
        QString content;
        QString clientMsgId;
        qint64  ts = 0;
    };

    void setState(State state);
    void flushIntent();
    void clearUser();
    void scheduleReconnect();
    void stopReconnect();
    void flushOutbox();
    void removeOutbox(const QString& clientMsgId);

    NetController* m_net = nullptr;
    State  m_state = State::Disconnected;
    Intent m_intent = Intent::None;

    QString m_host = QStringLiteral("127.0.0.1");
    quint16 m_port = 9527;

    // 待发送意图的凭据
    QString m_pendingUsername;
    QString m_pendingPassword;
    QString m_pendingDisplayName;

    // 已登录后的自身信息
    qint64  m_userId = 0;
    QString m_username;
    QString m_displayName;

    // 自动重连：凭据只驻留进程内存（不落盘），登出/重登失败/退出即清空
    QTimer* m_reconnectTimer = nullptr;
    int     m_reconnectAttempt = 0;   // 已尝试次数，成功后归零
    bool    m_socketConnected = false; // 用于区分「连接尝试失败」与「连上后掉线」
    bool    m_connectInFlight = false; // 已有一次连接尝试在途（避免登录时重复 abort/重连）
    bool    m_reloginInFlight = false;
    QString m_savedUsername;
    QString m_savedPassword;
    QString m_lastError;

    QList<PendingChat> m_outbox;

    // UI 就绪前的首批推送缓存（登出时清空）
    bool                     m_uiReady = false;
    bool                     m_hasPendingFriends = false;
    QList<FriendInfo>        m_pendingFriends;
    QList<FriendRequestInfo> m_pendingRequests;
    QList<ChatMessageInfo>   m_pendingChats;
};
