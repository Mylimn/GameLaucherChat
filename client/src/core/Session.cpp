#include "core/Session.h"

#include <QDateTime>
#include <QDebug>
#include <QJsonArray>
#include <QSettings>
#include <QTimer>
#include <QUuid>
#include <utility>

#include "net/NetController.h"
#include "protocol/ProtocolVersion.h"

namespace {
// 与 ClientSession 约定一致：code == 0 表示成功
constexpr int kCodeOk = 0;

// 断线重连退避：1s → 2s → 4s → 8s → 16s → 30s（此后固定 30s）
constexpr int kReconnectBaseDelayMs = 1000;
constexpr int kReconnectMaxDelayMs  = 30 * 1000;

// 未配置过服务端地址时的默认值（本机调试）
constexpr char kDefaultServerAddress[] = "127.0.0.1:9527";

// 与服务端/游戏库同一目录（%APPDATA%/GameLauncherChat/）
QSettings clientSettings()
{
    return QSettings(QSettings::IniFormat, QSettings::UserScope,
                     QStringLiteral("GameLauncherChat"), QStringLiteral("client"));
}

qint64 toInt64(const QJsonValue& v)
{
    return static_cast<qint64>(v.toDouble());
}

FriendInfo friendFromJson(const QJsonObject& o)
{
    FriendInfo f;
    f.userId      = toInt64(o.value(QStringLiteral("userId")));
    f.username    = o.value(QStringLiteral("username")).toString();
    f.displayName = o.value(QStringLiteral("displayName")).toString();
    f.online      = o.value(QStringLiteral("online")).toBool();
    f.signature   = o.value(QStringLiteral("signature")).toString();
    return f;
}

// 好友请求的两种来源（FriendRequestPush 单条补推 / FriendRequestListResp 全量）字段一致
FriendRequestInfo friendRequestFromJson(const QJsonObject& o)
{
    FriendRequestInfo r;
    r.fromUserId  = toInt64(o.value(QStringLiteral("fromUserId")));
    r.username    = o.value(QStringLiteral("username")).toString();
    r.displayName = o.value(QStringLiteral("displayName")).toString();
    r.remark      = o.value(QStringLiteral("remark")).toString();
    r.ts          = toInt64(o.value(QStringLiteral("ts")));
    return r;
}

ChatMessageInfo chatFromJson(const QJsonObject& o)
{
    ChatMessageInfo m;
    m.serverMsgId = toInt64(o.value(QStringLiteral("serverMsgId")));
    m.fromUserId  = toInt64(o.value(QStringLiteral("fromUserId")));
    m.toUserId    = toInt64(o.value(QStringLiteral("toUserId")));
    m.content     = o.value(QStringLiteral("content")).toString();
    m.ts          = toInt64(o.value(QStringLiteral("ts")));
    m.offline     = o.value(QStringLiteral("offline")).toBool();
    m.status      = MessageStatus::Delivered;
    return m;
}

} // namespace

Session::Session(NetController* net, QObject* parent)
    : QObject(parent)
    , m_net(net)
{
    connect(m_net, &NetController::connected,     this, &Session::onConnected);
    connect(m_net, &NetController::disconnected,  this, &Session::onDisconnected);
    connect(m_net, &NetController::frameReceived, this, &Session::onFrame);
    connect(m_net, &NetController::errorOccurred, this, &Session::onNetError);

    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout, this, &Session::onReconnectTimeout);
}

void Session::setState(State state)
{
    if (m_state == state) {
        return;
    }
    m_state = state;
    emit stateChanged(m_state);
}

void Session::clearUser()
{
    m_userId = 0;
    m_username.clear();
    m_displayName.clear();
    m_uiReady = false;
    m_hasPendingFriends = false;
    m_pendingFriends.clear();
    m_pendingRequests.clear();
    m_pendingChats.clear();
}

void Session::notifyUiReady()
{
    m_uiReady = true;
}

bool Session::takePendingFriends(QList<FriendInfo>* out)
{
    if (!m_hasPendingFriends) {
        return false;
    }
    m_hasPendingFriends = false;
    if (out) {
        *out = std::exchange(m_pendingFriends, {});
    } else {
        m_pendingFriends.clear();
    }
    return true;
}

QList<FriendRequestInfo> Session::takePendingRequests()
{
    return std::exchange(m_pendingRequests, {});
}

QList<ChatMessageInfo> Session::takePendingChats()
{
    return std::exchange(m_pendingChats, {});
}

void Session::connectToServer(const QString& host, quint16 port)
{
    m_host = host;
    m_port = port;
    m_connectInFlight = true;
    setState(State::Connecting);
    m_net->connectToServer(host, port);
}

QString Session::savedServerAddress()
{
    const QString saved = clientSettings().value(QStringLiteral("server/address")).toString().trimmed();
    return saved.isEmpty() ? QString::fromLatin1(kDefaultServerAddress) : saved;
}

void Session::saveServerAddress(const QString& address)
{
    clientSettings().setValue(QStringLiteral("server/address"), address.trimmed());
}

bool Session::parseServerAddress(const QString& text, QString* host, quint16* port)
{
    const QString trimmed = text.trimmed();
    // 只支持 IPv4/主机名；IPv6 字面量不是本期需求（局域网调试足够）
    const int sep = trimmed.lastIndexOf(QLatin1Char(':'));
    if (sep <= 0 || sep == trimmed.size() - 1) {
        return false;
    }
    const QString hostPart = trimmed.left(sep).trimmed();
    bool ok = false;
    const uint portPart = trimmed.mid(sep + 1).trimmed().toUInt(&ok);
    if (!ok || hostPart.isEmpty() || portPart == 0 || portPart > 65535) {
        return false;
    }
    if (host) {
        *host = hostPart;
    }
    if (port) {
        *port = static_cast<quint16>(portPart);
    }
    return true;
}

void Session::login(const QString& username, const QString& password)
{
    // 用户主动登录：取消可能在等待的重连，避免定时器稍后又把连接打断
    stopReconnect();
    m_pendingUsername = username;
    m_pendingPassword = password;
    m_pendingDisplayName.clear();
    m_intent = Intent::Login;
    // 记住本次凭据（仅进程内存），断线后自动重登用；登录失败或登出时清空
    m_savedUsername = username;
    m_savedPassword = password;

    if (m_state == State::Connected) {
        flushIntent(); // 已连接，直接发请求
    } else if (!m_connectInFlight) {
        // 未连接且没有在途连接（首次或上次失败后重试）：发起连接，连上由 onConnected 补发请求。
        // 若已有在途连接，则只留下意图 —— 这里再 connectToServer 会 abort 掉正在建立的连接，
        // 服务端日志会出现「已接入 → 立即断开」的抖动。
        connectToServer(m_host, m_port);
    }
}

void Session::registerAccount(const QString& username, const QString& password, const QString& displayName)
{
    stopReconnect();
    m_savedUsername.clear();
    m_savedPassword.clear();
    m_pendingUsername = username;
    m_pendingPassword = password;
    m_pendingDisplayName = displayName;
    m_intent = Intent::Register;

    if (m_state == State::Connected) {
        flushIntent();
    } else if (!m_connectInFlight) {
        connectToServer(m_host, m_port); // 同 login()：等待在途连接，避免重复 abort
    }
}

void Session::flushIntent()
{
    switch (m_intent) {
    case Intent::Login: {
        QJsonObject payload;
        payload[QStringLiteral("username")]        = m_pendingUsername;
        payload[QStringLiteral("password")]        = m_pendingPassword;
        payload[QStringLiteral("protocolVersion")] = static_cast<int>(Protocol::kVersion);
        m_net->send(MsgType::LoginReq, payload);
        break;
    }
    case Intent::Register: {
        QJsonObject payload;
        payload[QStringLiteral("username")]    = m_pendingUsername;
        payload[QStringLiteral("password")]    = m_pendingPassword;
        payload[QStringLiteral("displayName")] = m_pendingDisplayName;
        m_net->send(MsgType::RegisterReq, payload);
        break;
    }
    case Intent::Relogin: {
        // 断线重连后自动重登：复用已保存的凭据，不经 UI
        QJsonObject payload;
        payload[QStringLiteral("username")]        = m_savedUsername;
        payload[QStringLiteral("password")]        = m_savedPassword;
        payload[QStringLiteral("protocolVersion")] = static_cast<int>(Protocol::kVersion);
        m_net->send(MsgType::LoginReq, payload);
        m_reloginInFlight = true;
        break;
    }
    case Intent::None:
        break;
    }
    m_intent = Intent::None;
    m_pendingPassword.clear(); // 明文口令不在内存久留
}

void Session::logout()
{
    // 主动登出：不再自动重连，凭据与待发消息立即清除（下次登录必须重新输入口令）
    stopReconnect();
    m_savedUsername.clear();
    m_savedPassword.clear();
    m_reloginInFlight = false;
    m_outbox.clear();

    if (m_state != State::LoggedIn) {
        clearUser();
        setState(m_socketConnected ? State::Connected : State::Disconnected);
        emit loggedOut();
        return;
    }
    m_net->send(MsgType::LogoutReq, QJsonObject());
}

void Session::scheduleReconnect()
{
    if (m_savedPassword.isEmpty() || m_reconnectTimer->isActive()) {
        return;
    }
    int delay = kReconnectBaseDelayMs;
    for (int i = 0; i < m_reconnectAttempt && delay < kReconnectMaxDelayMs; ++i) {
        delay *= 2;
    }
    if (delay > kReconnectMaxDelayMs) {
        delay = kReconnectMaxDelayMs;
    }
    ++m_reconnectAttempt;
    m_reconnectTimer->start(delay);
    emit reconnectScheduled(m_reconnectAttempt, delay);
}

void Session::stopReconnect()
{
    m_reconnectTimer->stop();
    m_reconnectAttempt = 0;
}

void Session::onReconnectTimeout()
{
    m_intent = Intent::Relogin;
    setState(State::Connecting);
    m_net->connectToServer(m_host, m_port);
}

// ---------------------------------------------------------------- 好友请求

void Session::requestFriendList()
{
    if (m_state != State::LoggedIn) {
        return;
    }
    m_net->send(MsgType::FriendListReq, QJsonObject());
}

void Session::requestFriendRequests()
{
    if (m_state != State::LoggedIn) {
        return;
    }
    m_net->send(MsgType::FriendRequestListReq, QJsonObject());
}

void Session::addFriend(const QString& username, const QString& remark)
{
    if (m_state != State::LoggedIn) {
        emit friendAddResult(false, QStringLiteral("请先登录"));
        return;
    }
    QJsonObject payload;
    payload[QStringLiteral("username")] = username;
    payload[QStringLiteral("remark")]   = remark;
    m_net->send(MsgType::FriendAddReq, payload);
}

void Session::handleFriendRequest(qint64 fromUserId, bool accept)
{
    QJsonObject payload;
    payload[QStringLiteral("fromUserId")] = fromUserId;
    payload[QStringLiteral("action")]     = accept ? QStringLiteral("accept") : QStringLiteral("reject");
    m_net->send(MsgType::FriendHandleReq, payload);
}

void Session::removeFriend(qint64 userId)
{
    QJsonObject payload;
    payload[QStringLiteral("userId")] = userId;
    m_net->send(MsgType::FriendRemoveReq, payload);
}

// ---------------------------------------------------------------- 私聊请求

QString Session::sendChat(qint64 toUserId, const QString& content)
{
    const QString clientMsgId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (m_state != State::LoggedIn) {
        // 断线/重连中：先入本地待发队列，重登后按序补发。
        // 不能立刻回失败 —— ChatPanel 是「先调用、后上屏」，同步回执会在那一行
        // 插入之前到达，confirmOutgoing 找不到行，消息就永远停在「发送中」
        PendingChat p;
        p.toUserId    = toUserId;
        p.content     = content;
        p.clientMsgId = clientMsgId;
        p.ts          = QDateTime::currentMSecsSinceEpoch();
        m_outbox.append(p);
        return clientMsgId;
    }
    QJsonObject payload;
    payload[QStringLiteral("toUserId")]    = toUserId;
    payload[QStringLiteral("content")]     = content;
    payload[QStringLiteral("clientMsgId")] = clientMsgId;
    m_net->send(MsgType::ChatSendReq, payload);
    return clientMsgId;
}

void Session::flushOutbox()
{
    // 顺序发送：服务端按到达顺序落库，会话里的先后顺序才不会乱
    for (const PendingChat& p : m_outbox) {
        QJsonObject payload;
        payload[QStringLiteral("toUserId")]    = p.toUserId;
        payload[QStringLiteral("content")]     = p.content;
        payload[QStringLiteral("clientMsgId")] = p.clientMsgId;
        m_net->send(MsgType::ChatSendReq, payload);
    }
}

void Session::removeOutbox(const QString& clientMsgId)
{
    for (int i = 0; i < m_outbox.size(); ++i) {
        if (m_outbox.at(i).clientMsgId == clientMsgId) {
            m_outbox.removeAt(i);
            return;
        }
    }
}

QList<ChatMessageInfo> Session::pendingOutgoing(qint64 peerUserId) const
{
    QList<ChatMessageInfo> result;
    for (const PendingChat& p : m_outbox) {
        if (p.toUserId != peerUserId) {
            continue;
        }
        ChatMessageInfo m;
        m.fromUserId  = m_userId;
        m.toUserId    = p.toUserId;
        m.content     = p.content;
        m.ts          = p.ts;
        m.clientMsgId = p.clientMsgId;
        m.status      = MessageStatus::Sending;
        result.append(m);
    }
    return result;
}

void Session::requestHistory(qint64 peerUserId, qint64 beforeTs, int limit)
{
    if (m_state != State::LoggedIn) {
        return;
    }
    QJsonObject payload;
    payload[QStringLiteral("peerUserId")] = peerUserId;
    payload[QStringLiteral("beforeTs")]   = beforeTs;
    payload[QStringLiteral("limit")]      = limit;
    m_net->send(MsgType::ChatHistoryReq, payload);
}

void Session::reportRead(qint64 peerUserId, qint64 upToServerMsgId)
{
    if (m_state != State::LoggedIn || upToServerMsgId <= 0) {
        return;
    }
    QJsonObject payload;
    payload[QStringLiteral("peerUserId")]       = peerUserId;
    payload[QStringLiteral("upToServerMsgId")]  = upToServerMsgId;
    m_net->send(MsgType::ChatReadReq, payload);
}

// ---------------------------------------------------------------- 入站

void Session::onConnected()
{
    m_socketConnected = true;
    m_connectInFlight = false;
    setState(State::Connected);
    flushIntent();
}

void Session::onDisconnected()
{
    m_socketConnected = false;
    m_connectInFlight = false;
    m_reloginInFlight = false;

    // connectToServer() 会 abort 掉旧 socket，同样会走到这里。
    // 若此时正处于「连接中」，说明刚设置的登录/注册意图还没发出去（onConnected 才补发），
    // 绝不能在这里清掉——否则会停在「已连接 + 正在登录…」，服务端永远收不到 LoginReq。
    if (m_state == State::Connecting) {
        return;
    }

    m_intent = Intent::None;

    if (!m_savedPassword.isEmpty()) {
        // 用户仍希望保持登录（有凭据）：保留身份与界面数据，转为退避重连；
        // m_uiReady 不动 —— 重连后服务端会重发好友列表，窗口还在，照常直接分发
        setState(State::Disconnected);
        scheduleReconnect();
        return;
    }

    clearUser();
    setState(State::Disconnected);
    m_lastError = QStringLiteral("与服务端的连接已断开");
    emit errorOccurred(m_lastError);
}

void Session::onNetError(const QString& message)
{
    m_lastError = message;
    m_connectInFlight = false; // 本次连接尝试已失败，允许下次 login() 重新发起

    if (!m_socketConnected) {
        // 已登录成功过的会话（断线重连中）：静默退避重试，界面提示交给 MainWindow
        if (m_userId != 0 && !m_savedPassword.isEmpty()) {
            setState(State::Disconnected);
            scheduleReconnect();
            return;
        }
        // 手动登录/注册时连接就失败（地址或端口写错、服务端没起）：
        // 必须回到未连接并上报错误，否则界面会永远停在「连接中」/「正在登录…」
        m_reloginInFlight = false;
        stopReconnect();
        m_savedUsername.clear();
        m_savedPassword.clear();
        setState(State::Disconnected);
        emit errorOccurred(QStringLiteral("无法连接服务器：%1（请确认服务端已启动、地址与端口正确）")
                               .arg(message));
        return;
    }

    emit errorOccurred(message);
}

void Session::onFrame(MsgType type, const QJsonObject& payload)
{
    switch (type) {
    case MsgType::LoginResp: {
        const int code = payload.value(QStringLiteral("code")).toInt(-1);
        const QString message = payload.value(QStringLiteral("message")).toString();
        if (code == kCodeOk) {
            m_userId      = toInt64(payload.value(QStringLiteral("userId")));
            m_username    = payload.value(QStringLiteral("username")).toString();
            m_displayName = payload.value(QStringLiteral("displayName")).toString();
            const bool restored = m_reloginInFlight;
            m_reloginInFlight = false;
            stopReconnect();          // 已是登录态，计数与定时器归零
            m_lastError.clear();
            setState(State::LoggedIn);
            emit loginSucceeded();
            if (restored) {
                emit connectionRestored();
            }
            // §4.3：LoginResp 之后客户端主动拉好友列表；离线消息/请求由服务端随后补推
            requestFriendList();
            flushOutbox(); // 断线期间攒下的待发消息，此刻按原顺序补发
        } else if (m_reloginInFlight) {
            // 自动重登被拒（口令被改/账号被删）：凭据作废，退回登录页由用户重新输入
            m_reloginInFlight = false;
            stopReconnect();
            m_savedUsername.clear();
            m_savedPassword.clear();
            m_outbox.clear(); // 已无法投递，避免下次登录用同一 clientMsgId 重发
            m_lastError = message.isEmpty() ? QStringLiteral("自动重新登录失败，请重新登录") : message;
            clearUser();
            setState(m_socketConnected ? State::Connected : State::Disconnected);
            emit reloginFailed(m_lastError);
        } else {
            // 普通登录失败：不保留凭据，避免下次断线时用错误口令反复重登
            m_savedUsername.clear();
            m_savedPassword.clear();
            emit loginFailed(message.isEmpty() ? QStringLiteral("登录失败") : message);
        }
        break;
    }
    case MsgType::RegisterResp: {
        const int code = payload.value(QStringLiteral("code")).toInt(-1);
        const QString message = payload.value(QStringLiteral("message")).toString();
        if (code == kCodeOk) {
            // 注册成功不自动登录：交由 UI 切回登录页
            emit registerSucceeded(message.isEmpty() ? QStringLiteral("注册成功，请登录") : message);
        } else {
            emit registerFailed(message.isEmpty() ? QStringLiteral("注册失败") : message);
        }
        break;
    }
    case MsgType::LogoutResp: {
        stopReconnect();
        m_savedUsername.clear();
        m_savedPassword.clear();
        m_reloginInFlight = false;
        m_outbox.clear();
        clearUser();
        setState(State::Connected); // 连接未断，可再次登录
        emit loggedOut();
        break;
    }
    case MsgType::ErrorResp: {
        const QString message = payload.value(QStringLiteral("message")).toString();
        emit errorOccurred(message.isEmpty() ? QStringLiteral("服务端返回错误") : message);
        break;
    }

    // ---- 好友 0x02xx ----
    case MsgType::FriendListResp: {
        QList<FriendInfo> friends;
        const QJsonArray arr = payload.value(QStringLiteral("friends")).toArray();
        for (const QJsonValue& v : arr) {
            friends.append(friendFromJson(v.toObject()));
        }
        if (!m_uiReady) {
            m_pendingFriends = friends;
            m_hasPendingFriends = true;
        }
        emit friendListReceived(friends);
        break;
    }
    case MsgType::FriendAddResp: {
        const int code = payload.value(QStringLiteral("code")).toInt(-1);
        const QString message = payload.value(QStringLiteral("message")).toString();
        emit friendAddResult(code == kCodeOk,
                             message.isEmpty() ? QStringLiteral("好友请求发送失败") : message);
        break;
    }
    case MsgType::FriendRequestPush: {
        const FriendRequestInfo r = friendRequestFromJson(payload);
        if (!m_uiReady) {
            // 同一发起人只保留最新一条（服务端重启补推时可能与已有请求重复）
            bool replaced = false;
            for (FriendRequestInfo& pending : m_pendingRequests) {
                if (pending.fromUserId == r.fromUserId) {
                    pending = r;
                    replaced = true;
                    break;
                }
            }
            if (!replaced) {
                m_pendingRequests.append(r);
            }
        }
        emit friendRequestReceived(r);
        break;
    }
    case MsgType::FriendRequestListResp: {
        QList<FriendRequestInfo> requests;
        const QJsonArray arr = payload.value(QStringLiteral("requests")).toArray();
        for (const QJsonValue& v : arr) {
            requests.append(friendRequestFromJson(v.toObject()));
        }
        // 全量语义：以服务端结果覆盖本地缓存与界面
        m_pendingRequests = requests;
        emit friendRequestsReceived(requests);
        break;
    }
    case MsgType::FriendHandleResp: {
        const int code = payload.value(QStringLiteral("code")).toInt(-1);
        const QString message = payload.value(QStringLiteral("message")).toString();
        const qint64 fromUserId = toInt64(payload.value(QStringLiteral("fromUserId")));
        emit friendHandleResult(code == kCodeOk, message, fromUserId);
        break;
    }
    case MsgType::FriendRemoveResp: {
        const int code = payload.value(QStringLiteral("code")).toInt(-1);
        const qint64 userId = toInt64(payload.value(QStringLiteral("userId")));
        emit friendRemoveResult(code == kCodeOk, userId);
        break;
    }
    case MsgType::PresencePush: {
        const qint64 userId = toInt64(payload.value(QStringLiteral("userId")));
        const bool online = payload.value(QStringLiteral("online")).toBool();
        emit presenceChanged(userId, online);
        break;
    }

    // ---- 私聊 0x03xx ----
    case MsgType::ChatSendResp: {
        const int code = payload.value(QStringLiteral("code")).toInt(-1);
        const QString clientMsgId = payload.value(QStringLiteral("clientMsgId")).toString();
        removeOutbox(clientMsgId); // 已收到服务端裁决，无论成败都不再重发
        emit chatSendAck(clientMsgId, code == kCodeOk,
                         toInt64(payload.value(QStringLiteral("serverMsgId"))),
                         toInt64(payload.value(QStringLiteral("ts"))));
        break;
    }
    case MsgType::ChatPush: {
        const ChatMessageInfo message = chatFromJson(payload);
        if (!m_uiReady) {
            m_pendingChats.append(message);
        }
        emit chatReceived(message);
        break;
    }
    case MsgType::ChatReadPush: {
        emit chatReadReceived(toInt64(payload.value(QStringLiteral("peerUserId"))),
                              toInt64(payload.value(QStringLiteral("upToServerMsgId"))));
        break;
    }
    case MsgType::ChatHistoryResp: {
        const qint64 peerUserId = toInt64(payload.value(QStringLiteral("peerUserId")));
        const bool hasMore = payload.value(QStringLiteral("hasMore")).toBool();
        QList<ChatMessageInfo> messages;
        const QJsonArray arr = payload.value(QStringLiteral("messages")).toArray();
        for (const QJsonValue& v : arr) {
            messages.append(chatFromJson(v.toObject()));
        }
        emit chatHistoryReceived(peerUserId, hasMore, messages,
                                 toInt64(payload.value(QStringLiteral("peerReadUpTo"))));
        break;
    }
    default:
        break;
    }
}
