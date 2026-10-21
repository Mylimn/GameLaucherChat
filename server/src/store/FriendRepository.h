#pragma once

#include <QList>
#include <QSqlDatabase>
#include <QString>
#include <optional>

// 好友关系表中的一条（已 join users 拿到展示信息）
struct FriendRecord {
    qint64  userId = 0;
    QString username;
    QString displayName;
    QString signature;
};

// friend_requests 表的一行（附带发起人展示信息，便于直接下发给客户端）
struct FriendRequestRecord {
    qint64  id = 0;
    qint64  fromUserId = 0;
    qint64  toUserId = 0;
    QString username;
    QString displayName;
    QString remark;
    qint64  ts = 0;
};

// 好友仓储（见 docs/TECH_DESIGN.md §8）：
// friends 表双向各存一行；friend_requests 用 state 标记 0 待处理 / 1 已接受 / 2 已拒绝。
class FriendRepository {
public:
    enum class AddRequestOutcome {
        Ok,
        AlreadyFriends,
        PendingExists,
        Error,
    };

    explicit FriendRepository(QSqlDatabase db) : m_db(std::move(db)) {}

    QList<FriendRecord> listFriends(qint64 userId) const;
    QList<qint64>       friendIds(qint64 userId) const;
    bool                areFriends(qint64 a, qint64 b) const;

    // 发起好友请求：同一对用户存在待处理请求时返回 PendingExists（不重复插入）
    AddRequestOutcome addRequest(qint64 fromUserId, qint64 toUserId, const QString& remark,
                                 qint64* requestId, QString* err) const;

    // 待处理请求（to_user = userId），按 id 升序
    QList<FriendRequestRecord> pendingRequests(qint64 toUserId) const;

    // 定位待处理请求（同一对用户之间最多一条待处理）
    std::optional<FriendRequestRecord> findPending(qint64 fromUserId, qint64 toUserId) const;

    // 接受：置 state=1 并写入双向好友行（同一事务，避免半成功）
    // 非 const：QSqlDatabase::transaction/commit 本身不是 const 接口
    bool acceptRequest(const FriendRequestRecord& request, QString* err);

    // 拒绝：置 state=2
    bool rejectRequest(const FriendRequestRecord& request, QString* err) const;

    // 删除双向好友关系
    bool removeFriendPair(qint64 a, qint64 b, QString* err) const;

private:
    bool insertFriendRow(qint64 userId, qint64 friendId, qint64 nowMs, QString* err) const;
    bool setRequestState(qint64 requestId, int state, QString* err) const;

    QSqlDatabase m_db;
};
