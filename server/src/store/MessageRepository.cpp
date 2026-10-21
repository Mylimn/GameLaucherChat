#include "store/MessageRepository.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace {

MessageRecord readRow(const QSqlQuery& q)
{
    MessageRecord r;
    r.serverMsgId = q.value(0).toLongLong();
    r.fromUserId  = q.value(1).toLongLong();
    r.toUserId    = q.value(2).toLongLong();
    r.content     = q.value(3).toString();
    r.ts          = q.value(4).toLongLong();
    return r;
}

} // namespace

qint64 MessageRepository::insert(qint64 fromUserId, qint64 toUserId, const QString& content, qint64 ts,
                                 QString* err) const
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO messages(from_user, to_user, content, ts, delivered) VALUES(?, ?, ?, ?, 0)"));
    q.addBindValue(fromUserId);
    q.addBindValue(toUserId);
    q.addBindValue(content);
    q.addBindValue(ts);
    if (!q.exec()) {
        if (err) {
            *err = q.lastError().text();
        }
        return -1;
    }
    return q.lastInsertId().toLongLong();
}

QList<MessageRecord> MessageRepository::undelivered(qint64 userId) const
{
    QList<MessageRecord> out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT id, from_user, to_user, content, ts FROM messages "
        "WHERE to_user = ? AND delivered = 0 ORDER BY id"));
    q.addBindValue(userId);
    if (!q.exec()) {
        qWarning("MessageRepository::undelivered 查询失败：%s", qPrintable(q.lastError().text()));
        return out;
    }
    while (q.next()) {
        out.append(readRow(q));
    }
    return out;
}

bool MessageRepository::markDelivered(const QList<qint64>& serverMsgIds, QString* err) const
{
    if (serverMsgIds.isEmpty()) {
        return true;
    }
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE messages SET delivered = 1 WHERE id = ?"));
    for (qint64 id : serverMsgIds) {
        q.bindValue(0, id); // 复用同一预编译语句，按位置覆盖绑定值
        if (!q.exec()) {
            if (err) {
                *err = q.lastError().text();
            }
            return false;
        }
    }
    return true;
}

bool MessageRepository::markReadFromPeer(qint64 selfUserId, qint64 peerUserId,
                                         qint64 upToServerMsgId, QString* err) const
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE messages SET delivered = 1 "
        "WHERE from_user = ? AND to_user = ? AND id <= ? AND delivered = 0"));
    q.addBindValue(peerUserId);
    q.addBindValue(selfUserId);
    q.addBindValue(upToServerMsgId);
    if (!q.exec()) {
        if (err) {
            *err = q.lastError().text();
        }
        return false;
    }
    return true;
}

qint64 MessageRepository::readUpTo(qint64 selfUserId, qint64 peerUserId) const
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT MAX(id) FROM messages WHERE from_user = ? AND to_user = ? AND delivered = 1"));
    q.addBindValue(selfUserId);
    q.addBindValue(peerUserId);
    if (!q.exec() || !q.next()) {
        return 0;
    }
    return q.value(0).toLongLong();
}

QList<MessageRecord> MessageRepository::history(qint64 selfUserId, qint64 peerUserId,
                                                qint64 beforeTs, int limit) const
{
    QList<MessageRecord> out;
    QSqlQuery q(m_db);
    // 双向条件覆盖「我发给他」与「他发给我」；ts 相同的情况由 id 兜底排序
    q.prepare(QStringLiteral(
        "SELECT id, from_user, to_user, content, ts FROM messages "
        "WHERE ((from_user = ? AND to_user = ?) OR (from_user = ? AND to_user = ?)) "
        "  AND (? <= 0 OR ts < ?) "
        "ORDER BY id DESC LIMIT ?"));
    q.addBindValue(selfUserId);
    q.addBindValue(peerUserId);
    q.addBindValue(peerUserId);
    q.addBindValue(selfUserId);
    q.addBindValue(beforeTs);
    q.addBindValue(beforeTs);
    q.addBindValue(limit);
    if (!q.exec()) {
        qWarning("MessageRepository::history 查询失败：%s", qPrintable(q.lastError().text()));
        return out;
    }
    while (q.next()) {
        out.prepend(readRow(q)); // 查询是倒序，前置插入还原为时间升序
    }
    return out;
}
