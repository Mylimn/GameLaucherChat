#pragma once

#include <QMetaType>
#include <QtGlobal>

// 消息类型枚举 —— 客户端与服务端之间的「唯一真相源」（见 docs/TECH_DESIGN.md §4.1）。
// 段位约定：0x01xx 会话/连接、0x02xx 好友、0x03xx 私聊、0x04xx 启动器（预留）。
// 改动本枚举必须走 AGENTS.md §7 的协议变更流程（三处同步）。
enum class MsgType : quint16 {
    // 0x01xx 会话 / 连接
    LoginReq        = 0x0101,
    LoginResp       = 0x0102,
    LogoutReq       = 0x0103,
    LogoutResp      = 0x0104,
    HeartbeatReq    = 0x0105,
    HeartbeatResp   = 0x0106,
    ErrorResp       = 0x0107,
    RegisterReq     = 0x0108,
    RegisterResp    = 0x0109,

    // 0x02xx 好友
    FriendListReq   = 0x0201,
    FriendListResp  = 0x0202,
    FriendAddReq    = 0x0203,
    FriendAddResp   = 0x0204,
    FriendRequestPush = 0x0205,   // 服务端 → 客户端：有人加你
    FriendHandleReq = 0x0206,     // 客户端 → 服务端：接受/拒绝
    FriendHandleResp= 0x0207,
    FriendRemoveReq = 0x0208,
    FriendRemoveResp= 0x0209,
    PresencePush    = 0x020A,     // 服务端 → 客户端：好友上下线
    FriendRequestListReq  = 0x020B, // 客户端 → 服务端：主动拉取待处理请求
    FriendRequestListResp = 0x020C, // 服务端 → 客户端：待处理请求全量

    // 0x03xx 私聊
    ChatSendReq     = 0x0301,
    ChatSendResp    = 0x0302,
    ChatPush        = 0x0303,     // 服务端 → 客户端：收到新消息（含离线补推）
    ChatHistoryReq  = 0x0304,
    ChatHistoryResp = 0x0305,
    ChatReadReq     = 0x0306,
    ChatReadPush    = 0x0307,     // 服务端 → 客户端：对方已读你发的消息（FR-3.7）

    // 0x04xx 启动器（预留）
    GameListReq     = 0x0401,
    GameListResp    = 0x0402,
};

// 让 MsgType 成为 Qt 元类型：跨线程信号槽（QueuedConnection）需要。
// 注意：这只影响 Qt 类型系统，不改变线格式，枚举取值亦未变。
Q_DECLARE_METATYPE(MsgType)
