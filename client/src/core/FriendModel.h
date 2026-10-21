#pragma once

#include <QAbstractListModel>
#include <QList>

#include "core/Types.h"

// 好友列表模型（FR-2.1 / FR-2.5）：好友基础信息 + 在线状态 + 未读计数。
// 数据全部由 Session 驱动（拉取结果 / PresencePush / ChatPush 累加未读），UI 只读。
class FriendModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum Roles {
        UserIdRole = Qt::UserRole + 1,
        UsernameRole,
        DisplayNameRole,
        OnlineRole,
        SignatureRole,
        UnreadRole,
    };

    explicit FriendModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

    void setFriends(const QList<FriendInfo>& friends);
    bool setOnline(qint64 userId, bool online);
    bool addUnread(qint64 userId);
    void clearUnread(qint64 userId);

    bool contains(qint64 userId) const;
    int  indexOf(qint64 userId) const;
    FriendInfo friendAt(int row) const;

private:
    struct Entry {
        FriendInfo info;
        int unread = 0;
    };

    QList<Entry> m_entries;
};
