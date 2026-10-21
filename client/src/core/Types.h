#pragma once

#include <QList>
#include <QString>
#include <QtGlobal>

// 客户端内部的数据结构（与协议载荷一一对应，但独立于 JSON，便于 UI/模型消费）。
// 注意：这些结构只在主线程内通过信号槽传递，无需注册元类型。

struct FriendInfo {
    qint64  userId = 0;
    QString username;
    QString displayName;
    bool    online = false;
    QString signature;
};

struct FriendRequestInfo {
    qint64  fromUserId = 0;
    QString username;
    QString displayName;
    QString remark;
    qint64  ts = 0;
};

// 会话列表条目（FR-3.4）：只收录有历史消息的好友会话
struct ConversationInfo {
    qint64  userId = 0;
    QString username;
    QString displayName;
    QString preview;   // 最后一条消息的单行预览（已截断）
    qint64  ts = 0;
    int     unread = 0;
    bool    online = false;
};

// 消息发送状态（FR-3.2：发送中 / 已送达 / 失败；FR-3.7 追加已读）
enum class MessageStatus {
    Sending,
    Delivered,
    Read,
    Failed,
};

struct ChatMessageInfo {
    qint64        serverMsgId = 0; // 本地乐观消息为 0
    qint64        fromUserId = 0;
    qint64        toUserId = 0;
    QString       content;
    qint64        ts = 0;
    bool          offline = false;       // true = 登录后补推的离线消息
    MessageStatus status = MessageStatus::Delivered;
    QString       clientMsgId;           // 仅本地乐观消息使用
};
