#pragma once

#include <QList>
#include <QSqlDatabase>
#include <QString>

// messages 表的一行；serverMsgId 即自增主键 id（§8）
struct MessageRecord {
    qint64  serverMsgId = 0;
    qint64  fromUserId = 0;
    qint64  toUserId = 0;
    QString content;
    qint64  ts = 0;
};

// 消息仓储（见 docs/TECH_DESIGN.md §8）：落库、离线队列、历史分页。
class MessageRepository {
public:
    explicit MessageRepository(QSqlDatabase db) : m_db(std::move(db)) {}

    // 落库并返回 serverMsgId；失败返回 -1
    qint64 insert(qint64 fromUserId, qint64 toUserId, const QString& content, qint64 ts,
                  QString* err) const;

    // 未投递队列（to_user = userId 且 delivered = 0），按自增 id 升序（不用 ts 排序，避免同毫秒乱序）
    QList<MessageRecord> undelivered(qint64 userId) const;

    // 批量置 delivered = 1（补推完成后调用）
    bool markDelivered(const QList<qint64>& serverMsgIds, QString* err) const;

    // 已读上报（ChatReadReq，FR-3.7）：把 peer → self 且 id <= upToServerMsgId 的消息置 delivered = 1
    bool markReadFromPeer(qint64 selfUserId, qint64 peerUserId, qint64 upToServerMsgId,
                          QString* err) const;

    // 对端已读位置：self → peer 中 delivered = 1 的最大 id（没有则 0）。
    // 供历史响应回传给发送方，重建气泡的「已读」状态（delivered 对己方发出的行只由已读上报置位）
    qint64 readUpTo(qint64 selfUserId, qint64 peerUserId) const;

    // 双向会话历史：ts < beforeTs 倒序取 limit 条，返回时按时间升序（beforeTs <= 0 表示取最新一页）
    QList<MessageRecord> history(qint64 selfUserId, qint64 peerUserId, qint64 beforeTs, int limit) const;

private:
    QSqlDatabase m_db;
};
