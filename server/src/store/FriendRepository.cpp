#include "store/FriendRepository.h"

#include <QDateTime>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

QList<FriendRecord> FriendRepository::listFriends(qint64 userId) const
{
    QList<FriendRecord> out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT u.id, u.username, u.display_name, u.signature "
        "FROM friends f JOIN users u ON u.id = f.friend_id "
        "WHERE f.user_id = ? ORDER BY u.username"));
    q.addBindValue(userId);
    if (!q.exec()) {
        qWarning("FriendRepository::listFriends 查询失败：%s", qPrintable(q.lastError().text()));
        return out;
    }
    while (q.next()) {
        FriendRecord r;
        r.userId      = q.value(0).toLongLong();
        r.username    = q.value(1).toString();
        r.displayName = q.value(2).toString();
        r.signature   = q.value(3).toString();
        out.append(r);
    }
    return out;
}

QList<qint64> FriendRepository::friendIds(qint64 userId) const
{
    QList<qint64> out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT friend_id FROM friends WHERE user_id = ?"));
    q.addBindValue(userId);
    if (!q.exec()) {
        qWarning("FriendRepository::friendIds 查询失败：%s", qPrintable(q.lastError().text()));
        return out;
    }
    while (q.next()) {
        out.append(q.value(0).toLongLong());
    }
    return out;
}

bool FriendRepository::areFriends(qint64 a, qint64 b) const
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT 1 FROM friends WHERE user_id = ? AND friend_id = ? LIMIT 1"));
    q.addBindValue(a);
    q.addBindValue(b);
    if (!q.exec()) {
        qWarning("FriendRepository::areFriends 查询失败：%s", qPrintable(q.lastError().text()));
        return false;
    }
    return q.next();
}

FriendRepository::AddRequestOutcome FriendRepository::addRequest(qint64 fromUserId, qint64 toUserId,
                                                                const QString& remark,
                                                                qint64* requestId, QString* err) const
{
    // 双向都查一遍：同一对用户之间最多存在一条待处理请求，重复发起直接拒绝
    if (findPending(fromUserId, toUserId).has_value() || findPending(toUserId, fromUserId).has_value()) {
        return AddRequestOutcome::PendingExists;
    }

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO friend_requests(from_user, to_user, remark, state, created_at) "
        "VALUES(?, ?, ?, 0, ?)"));
    q.addBindValue(fromUserId);
    q.addBindValue(toUserId);
    q.addBindValue(remark);
    q.addBindValue(QDateTime::currentMSecsSinceEpoch());
    if (!q.exec()) {
        if (err) {
            *err = q.lastError().text();
        }
        return AddRequestOutcome::Error;
    }
    if (requestId) {
        *requestId = q.lastInsertId().toLongLong();
    }
    return AddRequestOutcome::Ok;
}

QList<FriendRequestRecord> FriendRepository::pendingRequests(qint64 toUserId) const
{
    QList<FriendRequestRecord> out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT r.id, r.from_user, r.to_user, u.username, u.display_name, r.remark, r.created_at "
        "FROM friend_requests r JOIN users u ON u.id = r.from_user "
        "WHERE r.to_user = ? AND r.state = 0 ORDER BY r.id"));
    q.addBindValue(toUserId);
    if (!q.exec()) {
        qWarning("FriendRepository::pendingRequests 查询失败：%s", qPrintable(q.lastError().text()));
        return out;
    }
    while (q.next()) {
        FriendRequestRecord r;
        r.id          = q.value(0).toLongLong();
        r.fromUserId  = q.value(1).toLongLong();
        r.toUserId    = q.value(2).toLongLong();
        r.username    = q.value(3).toString();
        r.displayName = q.value(4).toString();
        r.remark      = q.value(5).toString();
        r.ts          = q.value(6).toLongLong();
        out.append(r);
    }
    return out;
}

std::optional<FriendRequestRecord> FriendRepository::findPending(qint64 fromUserId, qint64 toUserId) const
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT r.id, r.from_user, r.to_user, u.username, u.display_name, r.remark, r.created_at "
        "FROM friend_requests r JOIN users u ON u.id = r.from_user "
        "WHERE r.from_user = ? AND r.to_user = ? AND r.state = 0 LIMIT 1"));
    q.addBindValue(fromUserId);
    q.addBindValue(toUserId);
    if (!q.exec()) {
        qWarning("FriendRepository::findPending 查询失败：%s", qPrintable(q.lastError().text()));
        return std::nullopt;
    }
    if (!q.next()) {
        return std::nullopt;
    }
    FriendRequestRecord r;
    r.id          = q.value(0).toLongLong();
    r.fromUserId  = q.value(1).toLongLong();
    r.toUserId    = q.value(2).toLongLong();
    r.username    = q.value(3).toString();
    r.displayName = q.value(4).toString();
    r.remark      = q.value(5).toString();
    r.ts          = q.value(6).toLongLong();
    return r;
}

bool FriendRepository::acceptRequest(const FriendRequestRecord& request, QString* err)
{
    // 置状态与写双向关系必须在同一事务里：否则可能出现「请求已接受但好友未建立」
    if (!m_db.transaction()) {
        if (err) {
            *err = QStringLiteral("开启事务失败");
        }
        return false;
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (!setRequestState(request.id, 1, err)
        || !insertFriendRow(request.fromUserId, request.toUserId, now, err)
        || !insertFriendRow(request.toUserId, request.fromUserId, now, err)) {
        m_db.rollback();
        return false;
    }

    if (!m_db.commit()) {
        if (err) {
            *err = QStringLiteral("提交事务失败");
        }
        m_db.rollback();
        return false;
    }
    return true;
}

bool FriendRepository::rejectRequest(const FriendRequestRecord& request, QString* err) const
{
    return setRequestState(request.id, 2, err);
}

bool FriendRepository::removeFriendPair(qint64 a, qint64 b, QString* err) const
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM friends WHERE (user_id = ? AND friend_id = ?) "
                             "OR (user_id = ? AND friend_id = ?)"));
    q.addBindValue(a);
    q.addBindValue(b);
    q.addBindValue(b);
    q.addBindValue(a);
    if (!q.exec()) {
        if (err) {
            *err = q.lastError().text();
        }
        return false;
    }
    return true;
}

bool FriendRepository::insertFriendRow(qint64 userId, qint64 friendId, qint64 nowMs, QString* err) const
{
    // 用 OR IGNORE：重复接受历史请求时不应因主键冲突而失败
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO friends(user_id, friend_id, created_at) VALUES(?, ?, ?)"));
    q.addBindValue(userId);
    q.addBindValue(friendId);
    q.addBindValue(nowMs);
    if (!q.exec()) {
        if (err) {
            *err = q.lastError().text();
        }
        return false;
    }
    return true;
}

bool FriendRepository::setRequestState(qint64 requestId, int state, QString* err) const
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE friend_requests SET state = ? WHERE id = ?"));
    q.addBindValue(state);
    q.addBindValue(requestId);
    if (!q.exec()) {
        if (err) {
            *err = q.lastError().text();
        }
        return false;
    }
    return true;
}
