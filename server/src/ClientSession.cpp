#include "ClientSession.h"

#include <QDateTime>
#include <QDebug>
#include <QJsonArray>
#include <QJsonValue>
#include <QHostAddress>
#include <QRegularExpression>
#include <QTcpSocket>
#include <QTimer>

#include "PasswordHash.h"
#include "SessionRegistry.h"
#include "protocol/ProtocolVersion.h"
#include "store/FriendRepository.h"
#include "store/MessageRepository.h"
#include "store/UserRepository.h"

namespace {

// 业务错误码：PRD 只约定「0 = 成功，非 0 = 失败」，具体码值两侧共同遵守
// （客户端当前只展示 message，码值留给以后做分支处理）
constexpr int kErrBadRequest    = 1; // 参数/格式非法
constexpr int kErrUsernameTaken = 2; // 用户名已被占用
constexpr int kErrBadCredential = 3; // 用户名或密码错误
constexpr int kErrBadVersion    = 4; // 协议版本不兼容
constexpr int kErrInternal      = 5; // 服务端内部错误
constexpr int kErrUnknownType   = 6; // 未知消息类型
constexpr int kErrBadPayload    = 7; // 载荷格式错误
constexpr int kErrAlreadyOnline = 8; // 该账号已在其他连接登录（保持原连接，拒绝新登录）
constexpr int kErrNotLoggedIn   = 9; // 好友/私聊类消息要求先登录

// 服务端 30s 收不到任何字节就主动断开（见 §4.3），客户端心跳每 5s 一次足以保活
constexpr int kIdleTimeoutMs = 30 * 1000;

// 文本消息长度上限：本期不支持文件/图片，超长文本一律拒绝（§10）
constexpr int kMaxChatContentLength = 2000;
// 历史分页默认值与上限
constexpr int kDefaultHistoryLimit = 30;
constexpr int kMaxHistoryLimit     = 100;

// 账号规则（见 §4.2）：以服务端校验为准，客户端仅做输入提示
bool isValidUsername(const QString& v)
{
    static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9_]{3,20}$"));
    return re.match(v).hasMatch();
}

bool isValidPassword(const QString& v)
{
    return v.size() >= 6 && v.size() <= 32;
}

bool isValidDisplayName(const QString& v)
{
    return v.size() >= 1 && v.size() <= 20;
}

QString msgName(MsgType type)
{
    switch (type) {
    case MsgType::LoginReq:           return QStringLiteral("LoginReq");
    case MsgType::LoginResp:          return QStringLiteral("LoginResp");
    case MsgType::LogoutReq:          return QStringLiteral("LogoutReq");
    case MsgType::LogoutResp:         return QStringLiteral("LogoutResp");
    case MsgType::HeartbeatReq:       return QStringLiteral("HeartbeatReq");
    case MsgType::HeartbeatResp:      return QStringLiteral("HeartbeatResp");
    case MsgType::ErrorResp:          return QStringLiteral("ErrorResp");
    case MsgType::RegisterReq:        return QStringLiteral("RegisterReq");
    case MsgType::RegisterResp:       return QStringLiteral("RegisterResp");
    case MsgType::FriendListReq:      return QStringLiteral("FriendListReq");
    case MsgType::FriendListResp:     return QStringLiteral("FriendListResp");
    case MsgType::FriendAddReq:       return QStringLiteral("FriendAddReq");
    case MsgType::FriendAddResp:      return QStringLiteral("FriendAddResp");
    case MsgType::FriendRequestPush:  return QStringLiteral("FriendRequestPush");
    case MsgType::FriendHandleReq:    return QStringLiteral("FriendHandleReq");
    case MsgType::FriendHandleResp:   return QStringLiteral("FriendHandleResp");
    case MsgType::FriendRemoveReq:    return QStringLiteral("FriendRemoveReq");
    case MsgType::FriendRemoveResp:   return QStringLiteral("FriendRemoveResp");
    case MsgType::PresencePush:       return QStringLiteral("PresencePush");
    case MsgType::FriendRequestListReq:  return QStringLiteral("FriendRequestListReq");
    case MsgType::FriendRequestListResp: return QStringLiteral("FriendRequestListResp");
    case MsgType::ChatSendReq:        return QStringLiteral("ChatSendReq");
    case MsgType::ChatSendResp:       return QStringLiteral("ChatSendResp");
    case MsgType::ChatPush:           return QStringLiteral("ChatPush");
    case MsgType::ChatHistoryReq:     return QStringLiteral("ChatHistoryReq");
    case MsgType::ChatHistoryResp:    return QStringLiteral("ChatHistoryResp");
    case MsgType::ChatReadReq:        return QStringLiteral("ChatReadReq");
    case MsgType::ChatReadPush:       return QStringLiteral("ChatReadPush");
    default:
        return QStringLiteral("0x%1").arg(static_cast<int>(type), 4, 16, QLatin1Char('0'));
    }
}

qint64 nowMs()
{
    return QDateTime::currentMSecsSinceEpoch();
}

// 好友请求的统一序列化：补推（FriendRequestPush）与主动拉取（FriendRequestListResp）
// 必须字段一致，抽出来避免两处各写一份而漂移
QJsonObject requestToJson(const FriendRequestRecord& r)
{
    QJsonObject o;
    o[QStringLiteral("fromUserId")]  = r.fromUserId;
    o[QStringLiteral("username")]    = r.username;
    o[QStringLiteral("displayName")] = r.displayName;
    o[QStringLiteral("remark")]      = r.remark;
    o[QStringLiteral("ts")]          = r.ts;
    return o;
}

} // namespace

ClientSession::ClientSession(QTcpSocket* socket,
                             UserRepository& users,
                             FriendRepository& friends,
                             MessageRepository& messages,
                             SessionRegistry& registry,
                             QObject* parent)
    : QObject(parent)
    , m_socket(socket)
    , m_users(users)
    , m_friends(friends)
    , m_messages(messages)
    , m_registry(registry)
{
    m_socket->setParent(this);

    connect(m_socket, &QTcpSocket::readyRead,    this, &ClientSession::onReadyRead);
    connect(m_socket, &QTcpSocket::disconnected, this, &ClientSession::onSocketDisconnected);
    connect(m_socket, &QTcpSocket::errorOccurred, this,
            [this](QAbstractSocket::SocketError) {
                log(QStringLiteral("socket 错误：%1").arg(m_socket->errorString()));
            });

    m_idleTimer = new QTimer(this);
    m_idleTimer->setSingleShot(true);
    m_idleTimer->setInterval(kIdleTimeoutMs);
    connect(m_idleTimer, &QTimer::timeout, this, &ClientSession::onIdleTimeout);
    m_idleTimer->start();

    log(QStringLiteral("已接入"));
}

ClientSession::~ClientSession() = default;

QString ClientSession::peerName() const
{
    const QString ip = m_socket->peerAddress().toString();
    const quint16 port = m_socket->peerPort();
    return QStringLiteral("%1:%2").arg(ip).arg(port);
}

void ClientSession::log(const QString& text) const
{
    qInfo().noquote() << QStringLiteral("[session %1] %2").arg(peerName(), text);
}

void ClientSession::sendFrame(MsgType type, const QJsonObject& payload)
{
    if (m_socket->state() != QAbstractSocket::ConnectedState) {
        return;
    }
    m_socket->write(encodeFrame(type, payload));
}

void ClientSession::onReadyRead()
{
    m_idleTimer->start(); // 任何字节都算活跃
    m_decoder.feed(m_socket->readAll());

    for (;;) {
        MsgType type{};
        QJsonObject payload;
        QString err;
        if (!m_decoder.next(type, payload, &err)) {
            if (err.isEmpty()) {
                break; // 半包，等更多字节
            }
            // 坏载荷：丢弃该帧 + 回 ErrorResp，不断连（§3/§10）
            log(QStringLiteral("坏帧：%1").arg(err));
            sendError(kErrBadPayload, err, type);
            continue;
        }
        handleFrame(type, payload);
    }

    if (m_decoder.fatal()) {
        // 帧结构非法（超长/长度不足）：直接断连，防止内存膨胀（§3）
        log(QStringLiteral("帧结构非法，断开连接"));
        m_socket->disconnectFromHost();
    }
}

void ClientSession::onSocketDisconnected()
{
    m_idleTimer->stop();
    log(isLoggedIn() ? QStringLiteral("断开（用户 %1）").arg(m_username) : QStringLiteral("断开"));
    unregisterAndBroadcastOffline(); // 断线也要让好友看到「离线」（§8）

    if (m_announcedClosed) {
        return;
    }
    m_announcedClosed = true;
    emit closed(this);
}

void ClientSession::onIdleTimeout()
{
    log(QStringLiteral("超过 %1 秒未收到任何帧，主动断开").arg(kIdleTimeoutMs / 1000));
    m_socket->disconnectFromHost();
}

void ClientSession::handleFrame(MsgType type, const QJsonObject& payload)
{
    log(QStringLiteral("收到 %1").arg(msgName(type)));

    switch (type) {
    case MsgType::RegisterReq:     handleRegister(payload); break;
    case MsgType::LoginReq:        handleLogin(payload);    break;
    case MsgType::LogoutReq:       handleLogout();          break;
    case MsgType::HeartbeatReq:    handleHeartbeat(payload);break;
    case MsgType::FriendListReq:   handleFriendList();      break;
    case MsgType::FriendRequestListReq: handleFriendRequestList(); break;
    case MsgType::FriendAddReq:    handleFriendAdd(payload);break;
    case MsgType::FriendHandleReq: handleFriendHandle(payload); break;
    case MsgType::FriendRemoveReq: handleFriendRemove(payload); break;
    case MsgType::ChatSendReq:     handleChatSend(payload);  break;
    case MsgType::ChatHistoryReq:  handleChatHistory(payload); break;
    case MsgType::ChatReadReq:     handleChatRead(payload);  break;
    default:
        log(QStringLiteral("未知消息类型，回 ErrorResp"));
        sendError(kErrUnknownType, QStringLiteral("服务端不支持该消息类型"), type);
        break;
    }
}

void ClientSession::handleRegister(const QJsonObject& payload)
{
    const QString username    = payload.value(QStringLiteral("username")).toString();
    const QString password    = payload.value(QStringLiteral("password")).toString();
    const QString displayName = payload.value(QStringLiteral("displayName")).toString();

    QJsonObject resp;
    auto fail = [this, &resp](int code, const QString& message) {
        resp[QStringLiteral("code")] = code;
        resp[QStringLiteral("message")] = message;
        sendFrame(MsgType::RegisterResp, resp);
        log(QStringLiteral("注册失败：%1").arg(message));
    };

    if (!isValidUsername(username)) {
        fail(kErrBadRequest, QStringLiteral("用户名需 3-20 个字符，仅限字母、数字、下划线"));
        return;
    }
    if (!isValidPassword(password)) {
        fail(kErrBadRequest, QStringLiteral("密码需 6-32 个字符"));
        return;
    }
    if (!isValidDisplayName(displayName)) {
        fail(kErrBadRequest, QStringLiteral("昵称需 1-20 个字符"));
        return;
    }
    if (m_users.findByUsername(username).has_value()) {
        fail(kErrUsernameTaken, QStringLiteral("用户名已被占用"));
        return;
    }

    // 注册成功不自动登录：登录与注册职责分离（§4.2）
    const QString salt = PasswordHash::makeSalt();
    const QString hash = PasswordHash::hash(salt, password);
    QString err;
    const qint64 userId = m_users.create(username, displayName, salt, hash, &err);
    if (userId <= 0) {
        log(QStringLiteral("注册写入失败：%1").arg(err));
        fail(kErrInternal, QStringLiteral("服务端保存账号失败，请稍后重试"));
        return;
    }

    resp[QStringLiteral("code")]        = 0;
    resp[QStringLiteral("message")]     = QStringLiteral("注册成功，请登录");
    resp[QStringLiteral("userId")]      = userId;
    resp[QStringLiteral("username")]    = username;
    resp[QStringLiteral("displayName")] = displayName;
    sendFrame(MsgType::RegisterResp, resp);
    log(QStringLiteral("注册成功：%1（userId=%2）").arg(username).arg(userId));
}

void ClientSession::handleLogin(const QJsonObject& payload)
{
    const QString username = payload.value(QStringLiteral("username")).toString();
    const QString password = payload.value(QStringLiteral("password")).toString();
    const int version = payload.value(QStringLiteral("protocolVersion")).toInt(-1);

    QJsonObject resp;
    auto fail = [this, &resp](int code, const QString& message) {
        resp[QStringLiteral("code")] = code;
        resp[QStringLiteral("message")] = message;
        sendFrame(MsgType::LoginResp, resp);
        log(QStringLiteral("登录失败：%1").arg(message));
    };

    if (isLoggedIn()) {
        fail(kErrBadRequest, QStringLiteral("当前连接已登录，请先登出"));
        return;
    }
    if (version != Protocol::kVersion) {
        fail(kErrBadVersion,
             QStringLiteral("协议版本不兼容（客户端 %1 / 服务端 %2）")
                 .arg(version).arg(Protocol::kVersion));
        return;
    }
    if (!isValidUsername(username) || !isValidPassword(password)) {
        fail(kErrBadCredential, QStringLiteral("用户名或密码错误"));
        return;
    }

    const auto user = m_users.findByUsername(username);
    if (!user || !PasswordHash::verify(user->passwordSalt, password, user->passwordHash)) {
        fail(kErrBadCredential, QStringLiteral("用户名或密码错误"));
        return;
    }

    // 同账号重复登录：保留已在线的连接，拒绝新连接（单线程事件循环，检查与注册之间不会被插入）
    if (ClientSession* existing = m_registry.find(user->id)) {
        if (existing != this) {
            fail(kErrAlreadyOnline, QStringLiteral("该账号已在其他设备登录，请先退出后重试"));
            return;
        }
    }

    m_userId   = user->id;
    m_username = user->username;

    resp[QStringLiteral("code")]        = 0;
    resp[QStringLiteral("message")]     = QStringLiteral("登录成功");
    resp[QStringLiteral("userId")]      = user->id;
    resp[QStringLiteral("username")]    = user->username;
    resp[QStringLiteral("displayName")] = user->displayName;
    sendFrame(MsgType::LoginResp, resp);
    log(QStringLiteral("登录成功：%1（userId=%2）").arg(m_username).arg(m_userId));

    // 先回 LoginResp 再补推：客户端按序处理，能保证「主窗口就绪 → 再收推送」（§4.3）
    afterLoginPush();
}

void ClientSession::handleLogout()
{
    // 幂等：未登录也回成功，客户端据此回到登录页
    const bool wasLoggedIn = isLoggedIn();
    const QString name = m_username;

    QJsonObject resp;
    resp[QStringLiteral("code")] = 0;
    sendFrame(MsgType::LogoutResp, resp);

    unregisterAndBroadcastOffline(); // 登出即视为离线，好友需看到状态变化
    log(wasLoggedIn ? QStringLiteral("登出：%1").arg(name) : QStringLiteral("登出（未登录）"));
}

void ClientSession::handleHeartbeat(const QJsonObject& payload)
{
    // 原样回传 ts，客户端据此测量 RTT（M2 仅用于保活）
    QJsonObject resp;
    resp[QStringLiteral("ts")] = payload.value(QStringLiteral("ts"));
    sendFrame(MsgType::HeartbeatResp, resp);
}

// ---------------------------------------------------------------- 好友 0x02xx

void ClientSession::handleFriendList()
{
    if (!isLoggedIn()) {
        sendError(kErrNotLoggedIn, QStringLiteral("请先登录"), MsgType::FriendListReq);
        return;
    }
    sendFrame(MsgType::FriendListResp, buildFriendListPayload(m_userId));
    log(QStringLiteral("下发好友列表（%1 人）").arg(m_friends.friendIds(m_userId).size()));
}

void ClientSession::handleFriendRequestList()
{
    if (!isLoggedIn()) {
        sendError(kErrNotLoggedIn, QStringLiteral("请先登录"), MsgType::FriendRequestListReq);
        return;
    }

    // 客户端此刻可能刚完成登录、尚未收到补推，也可能补推丢了；
    // 直接以数据库为准返回全量待处理请求，客户端整体覆盖即可
    QJsonArray arr;
    const QList<FriendRequestRecord> requests = m_friends.pendingRequests(m_userId);
    for (const FriendRequestRecord& r : requests) {
        arr.append(requestToJson(r));
    }

    QJsonObject payload;
    payload[QStringLiteral("requests")] = arr;
    sendFrame(MsgType::FriendRequestListResp, payload);
    log(QStringLiteral("下发待处理好友请求（%1 条）").arg(arr.size()));
}

QJsonObject ClientSession::buildFriendListPayload(qint64 userId) const
{
    QJsonArray arr;
    const QList<FriendRecord> friends = m_friends.listFriends(userId);
    for (const FriendRecord& f : friends) {
        QJsonObject o;
        o[QStringLiteral("userId")]      = f.userId;
        o[QStringLiteral("username")]    = f.username;
        o[QStringLiteral("displayName")] = f.displayName;
        // 在线状态实时取自注册表，而不是落库字段
        o[QStringLiteral("online")]      = m_registry.isOnline(f.userId);
        o[QStringLiteral("signature")]   = f.signature;
        arr.append(o);
    }

    QJsonObject payload;
    payload[QStringLiteral("friends")] = arr;
    return payload;
}

void ClientSession::handleFriendAdd(const QJsonObject& payload)
{
    QJsonObject resp;
    auto fail = [this, &resp](int code, const QString& message) {
        resp[QStringLiteral("code")] = code;
        resp[QStringLiteral("message")] = message;
        sendFrame(MsgType::FriendAddResp, resp);
    };

    if (!isLoggedIn()) {
        fail(kErrNotLoggedIn, QStringLiteral("请先登录"));
        return;
    }

    const QString username = payload.value(QStringLiteral("username")).toString().trimmed();
    const QString remark   = payload.value(QStringLiteral("remark")).toString();

    const auto target = m_users.findByUsername(username);
    if (!target) {
        fail(kErrBadRequest, QStringLiteral("用户不存在"));
        return;
    }
    if (target->id == m_userId) {
        fail(kErrBadRequest, QStringLiteral("不能添加自己为好友"));
        return;
    }
    if (m_friends.areFriends(m_userId, target->id)) {
        fail(kErrBadRequest, QStringLiteral("你们已经是好友"));
        return;
    }
    // 对方已向我发过请求：提示去「好友请求」里处理，而不是重复插入
    if (m_friends.findPending(target->id, m_userId).has_value()) {
        fail(kErrBadRequest, QStringLiteral("对方已向你发送好友请求，请在请求列表中处理"));
        return;
    }

    qint64 requestId = 0;
    QString err;
    const auto outcome = m_friends.addRequest(m_userId, target->id, remark, &requestId, &err);
    if (outcome == FriendRepository::AddRequestOutcome::PendingExists) {
        fail(kErrBadRequest, QStringLiteral("好友请求已发送，请等待对方处理"));
        return;
    }
    if (outcome != FriendRepository::AddRequestOutcome::Ok) {
        log(QStringLiteral("写好友请求失败：%1").arg(err));
        fail(kErrInternal, QStringLiteral("服务端处理失败，请稍后重试"));
        return;
    }

    // 对方在线则实时推送请求，离线则等其登录后补推（§4.3）
    if (ClientSession* targetSession = m_registry.find(target->id)) {
        const auto me = m_users.findById(m_userId);
        QJsonObject push;
        push[QStringLiteral("fromUserId")]  = m_userId;
        push[QStringLiteral("username")]    = m_username;
        push[QStringLiteral("displayName")] = me ? me->displayName : m_username;
        push[QStringLiteral("remark")]      = remark;
        push[QStringLiteral("ts")]          = nowMs();
        targetSession->sendFrame(MsgType::FriendRequestPush, push);
    }

    resp[QStringLiteral("code")]    = 0;
    resp[QStringLiteral("message")] = QStringLiteral("好友请求已发送");
    sendFrame(MsgType::FriendAddResp, resp);
    log(QStringLiteral("发出好友请求：#%1 → %2（requestId=%3）")
            .arg(m_username, target->username).arg(requestId));
}

void ClientSession::handleFriendHandle(const QJsonObject& payload)
{
    QJsonObject resp;
    const qint64 fromUserId = payload.value(QStringLiteral("fromUserId")).toInteger();
    const QString action = payload.value(QStringLiteral("action")).toString();

    auto fail = [this, &resp, fromUserId](int code, const QString& message) {
        resp[QStringLiteral("code")]      = code;
        resp[QStringLiteral("message")]   = message;
        resp[QStringLiteral("fromUserId")]= fromUserId;
        sendFrame(MsgType::FriendHandleResp, resp);
    };

    if (!isLoggedIn()) {
        fail(kErrNotLoggedIn, QStringLiteral("请先登录"));
        return;
    }

    const auto request = m_friends.findPending(fromUserId, m_userId);
    if (!request) {
        fail(kErrBadRequest, QStringLiteral("该请求不存在或已处理"));
        return;
    }

    QString err;
    if (action == QStringLiteral("accept")) {
        if (!m_friends.acceptRequest(*request, &err)) {
            log(QStringLiteral("接受好友请求失败：%1").arg(err));
            fail(kErrInternal, QStringLiteral("服务端处理失败，请稍后重试"));
            return;
        }
    } else if (action == QStringLiteral("reject")) {
        if (!m_friends.rejectRequest(*request, &err)) {
            log(QStringLiteral("拒绝好友请求失败：%1").arg(err));
            fail(kErrInternal, QStringLiteral("服务端处理失败，请稍后重试"));
            return;
        }
    } else {
        fail(kErrBadRequest, QStringLiteral("action 仅支持 accept / reject"));
        return;
    }

    resp[QStringLiteral("code")]       = 0;
    resp[QStringLiteral("message")]    = (action == QStringLiteral("accept"))
                                             ? QStringLiteral("已添加为好友")
                                             : QStringLiteral("已拒绝该请求");
    resp[QStringLiteral("fromUserId")] = fromUserId;
    sendFrame(MsgType::FriendHandleResp, resp);
    log(QStringLiteral("%1 好友请求（from=%2）")
            .arg(action == QStringLiteral("accept") ? QStringLiteral("接受") : QStringLiteral("拒绝"))
            .arg(fromUserId));

    // 关系变化后双方列表都要刷新：协议没有专门的「列表已变更」推送，
    // 因此主动补发 FriendListResp（接受方与发起方，在线即发）——见 DEVLOG 决策
    if (action == QStringLiteral("accept")) {
        pushFriendList(m_userId);
        pushFriendList(fromUserId);
    }
}

void ClientSession::handleFriendRemove(const QJsonObject& payload)
{
    QJsonObject resp;
    const qint64 targetUserId = payload.value(QStringLiteral("userId")).toInteger();

    auto finish = [this, &resp, targetUserId](int code) {
        resp[QStringLiteral("code")]   = code;
        resp[QStringLiteral("userId")] = targetUserId;
        sendFrame(MsgType::FriendRemoveResp, resp);
    };

    if (!isLoggedIn()) {
        finish(kErrNotLoggedIn);
        return;
    }
    if (!m_friends.areFriends(m_userId, targetUserId)) {
        finish(kErrBadRequest);
        return;
    }

    QString err;
    if (!m_friends.removeFriendPair(m_userId, targetUserId, &err)) {
        log(QStringLiteral("删除好友失败：%1").arg(err));
        finish(kErrInternal);
        return;
    }

    finish(0);
    log(QStringLiteral("删除好友：%1 与 %2").arg(m_userId).arg(targetUserId));

    // 双方列表都刷新（被删方不再看到自己）
    pushFriendList(m_userId);
    pushFriendList(targetUserId);
}

// ---------------------------------------------------------------- 私聊 0x03xx

void ClientSession::handleChatSend(const QJsonObject& payload)
{
    const qint64 toUserId   = payload.value(QStringLiteral("toUserId")).toInteger();
    const QString content   = payload.value(QStringLiteral("content")).toString();
    const QString clientMsgId = payload.value(QStringLiteral("clientMsgId")).toString();

    // clientMsgId 无论成败都回带，客户端据此定位乐观上屏的那条消息
    QJsonObject resp;
    resp[QStringLiteral("clientMsgId")] = clientMsgId;
    auto fail = [this, &resp](int code) {
        resp[QStringLiteral("code")] = code;
        sendFrame(MsgType::ChatSendResp, resp);
    };

    if (!isLoggedIn()) {
        fail(kErrNotLoggedIn);
        return;
    }
    if (toUserId <= 0 || toUserId == m_userId
        || content.isEmpty() || content.size() > kMaxChatContentLength) {
        fail(kErrBadRequest);
        return;
    }
    if (!m_friends.areFriends(m_userId, toUserId)) {
        fail(kErrBadRequest); // 只允许好友之间私聊
        return;
    }

    const qint64 ts = nowMs();
    QString err;
    const qint64 serverMsgId = m_messages.insert(m_userId, toUserId, content, ts, &err);
    if (serverMsgId <= 0) {
        log(QStringLiteral("消息落库失败：%1").arg(err));
        fail(kErrInternal);
        return;
    }

    resp[QStringLiteral("code")]        = 0;
    resp[QStringLiteral("serverMsgId")] = serverMsgId;
    resp[QStringLiteral("ts")]          = ts;
    sendFrame(MsgType::ChatSendResp, resp);

    const MessageRecord record{serverMsgId, m_userId, toUserId, content, ts};
    if (ClientSession* receiver = m_registry.find(toUserId)) {
        receiver->pushChat(toUserId, record, false);
        m_messages.markDelivered({serverMsgId}, nullptr); // 已实时投递，出离线队列（§8）
        log(QStringLiteral("私聊 %1 → %2（实时，id=%3）").arg(m_userId).arg(toUserId).arg(serverMsgId));
    } else {
        log(QStringLiteral("私聊 %1 → %2（离线入队，id=%3）").arg(m_userId).arg(toUserId).arg(serverMsgId));
    }
}

void ClientSession::handleChatHistory(const QJsonObject& payload)
{
    QJsonObject resp;
    const qint64 peerUserId = payload.value(QStringLiteral("peerUserId")).toInteger();
    const qint64 beforeTs   = payload.value(QStringLiteral("beforeTs")).toInteger();
    int limit = payload.value(QStringLiteral("limit")).toInt(kDefaultHistoryLimit);
    limit = qBound(1, limit, kMaxHistoryLimit);

    if (!isLoggedIn()) {
        sendError(kErrNotLoggedIn, QStringLiteral("请先登录"), MsgType::ChatHistoryReq);
        return;
    }

    const QList<MessageRecord> rows = m_messages.history(m_userId, peerUserId, beforeTs, limit);
    QJsonArray messages;
    for (const MessageRecord& m : rows) {
        QJsonObject o;
        o[QStringLiteral("serverMsgId")] = m.serverMsgId;
        o[QStringLiteral("fromUserId")]  = m.fromUserId;
        o[QStringLiteral("toUserId")]    = m.toUserId;
        o[QStringLiteral("content")]     = m.content;
        o[QStringLiteral("ts")]          = m.ts;
        messages.append(o);
    }

    resp[QStringLiteral("peerUserId")] = peerUserId;
    // 取满一页即认为可能还有更早的（客户端用本页最早一条的 ts 作为下次 beforeTs）
    resp[QStringLiteral("hasMore")]    = (rows.size() == limit);
    resp[QStringLiteral("messages")]   = messages;
    // 对端已读位置：客户端据此重建「已读」气泡状态（否则切换会话重新拉历史会退回「已送达」）
    resp[QStringLiteral("peerReadUpTo")] = m_messages.readUpTo(m_userId, peerUserId);
    sendFrame(MsgType::ChatHistoryResp, resp);
    log(QStringLiteral("历史 %1 ↔ %2：%3 条（hasMore=%4）")
            .arg(m_userId).arg(peerUserId).arg(rows.size())
            .arg(rows.size() == limit ? QStringLiteral("true") : QStringLiteral("false")));
}

void ClientSession::handleChatRead(const QJsonObject& payload)
{
    if (!isLoggedIn()) {
        return; // 无回包类型（ChatReadReq 无对应 Resp），静默忽略
    }
    const qint64 peerUserId = payload.value(QStringLiteral("peerUserId")).toInteger();
    const qint64 upToId     = payload.value(QStringLiteral("upToServerMsgId")).toInteger();
    if (peerUserId <= 0 || upToId <= 0) {
        return;
    }

    QString err;
    if (!m_messages.markReadFromPeer(m_userId, peerUserId, upToId, &err)) {
        log(QStringLiteral("已读上报失败：%1").arg(err));
        return;
    }
    log(QStringLiteral("已读上报：%1 ← %2（≤ %3）").arg(m_userId).arg(peerUserId).arg(upToId));

    // FR-3.7：把已读位置回推给发送方，让其气泡从「已送达」变「已读」。
    // 对方不在线时直接丢弃 —— 已读只是即时状态，不落库、不做离线补推。
    if (ClientSession* peer = m_registry.find(peerUserId)) {
        QJsonObject push;
        push[QStringLiteral("peerUserId")]      = m_userId; // 读消息的人
        push[QStringLiteral("upToServerMsgId")] = upToId;
        peer->sendFrame(MsgType::ChatReadPush, push);
    }
}

// ---------------------------------------------------------------- 登录/登出副作用

void ClientSession::afterLoginPush()
{
    // 1) 登记在线（同一账号重复登录时以新连接为准，见 unregisterAndBroadcastOffline 的保护）
    m_registry.add(m_userId, this);
    // 2) 向好友广播上线
    broadcastPresence(true);
    // 3) 补推离线消息（按 id 升序）
    deliverOfflineMessages();
    // 4) 补推未处理的好友请求
    deliverPendingRequests();
}

void ClientSession::unregisterAndBroadcastOffline()
{
    if (m_userId == 0) {
        return;
    }
    // 若已被同一账号的新连接顶替，则不动注册表、也不广播离线（否则会把新连接误标为离线）
    if (m_registry.find(m_userId) == this) {
        broadcastPresence(false);
        m_registry.remove(m_userId);
    }
    m_userId = 0;
    m_username.clear();
}

void ClientSession::broadcastPresence(bool online)
{
    if (m_userId == 0) {
        return;
    }
    const QList<qint64> friends = m_friends.friendIds(m_userId);
    const qint64 ts = nowMs();
    for (qint64 friendId : friends) {
        ClientSession* s = m_registry.find(friendId);
        if (!s) {
            continue;
        }
        QJsonObject push;
        push[QStringLiteral("userId")] = m_userId;
        push[QStringLiteral("online")] = online;
        push[QStringLiteral("ts")]     = ts;
        s->sendFrame(MsgType::PresencePush, push);
    }
    log(QStringLiteral("广播%1：%2 → %3 位好友")
            .arg(online ? QStringLiteral("上线") : QStringLiteral("离线"))
            .arg(m_username)
            .arg(friends.size()));
}

void ClientSession::deliverOfflineMessages()
{
    const QList<MessageRecord> pending = m_messages.undelivered(m_userId);
    if (pending.isEmpty()) {
        return;
    }
    QList<qint64> ids;
    for (const MessageRecord& m : pending) {
        pushChat(m_userId, m, true);
        ids.append(m.serverMsgId);
    }
    QString err;
    if (!m_messages.markDelivered(ids, &err)) {
        log(QStringLiteral("标记离线消息已投递失败：%1").arg(err));
    }
    log(QStringLiteral("补推离线消息 %1 条").arg(pending.size()));
}

void ClientSession::deliverPendingRequests()
{
    const QList<FriendRequestRecord> requests = m_friends.pendingRequests(m_userId);
    for (const FriendRequestRecord& r : requests) {
        sendFrame(MsgType::FriendRequestPush, requestToJson(r));
    }
    if (!requests.isEmpty()) {
        log(QStringLiteral("补推未处理好友请求 %1 条").arg(requests.size()));
    }
}

void ClientSession::pushFriendList(qint64 userId)
{
    ClientSession* target = m_registry.find(userId);
    if (!target) {
        return;
    }
    target->sendFrame(MsgType::FriendListResp, buildFriendListPayload(userId));
}

void ClientSession::pushChat(qint64 targetUserId, const MessageRecord& msg, bool offline)
{
    ClientSession* target = m_registry.find(targetUserId);
    if (!target) {
        return;
    }
    QJsonObject push;
    push[QStringLiteral("serverMsgId")] = msg.serverMsgId;
    push[QStringLiteral("fromUserId")]  = msg.fromUserId;
    push[QStringLiteral("toUserId")]    = msg.toUserId;
    push[QStringLiteral("content")]     = msg.content;
    push[QStringLiteral("ts")]          = msg.ts;
    push[QStringLiteral("offline")]     = offline;
    target->sendFrame(MsgType::ChatPush, push);
}

void ClientSession::sendError(int code, const QString& message, MsgType refType)
{
    QJsonObject resp;
    resp[QStringLiteral("code")]    = code;
    resp[QStringLiteral("message")] = message;
    resp[QStringLiteral("refType")] = static_cast<int>(refType);
    sendFrame(MsgType::ErrorResp, resp);
}
