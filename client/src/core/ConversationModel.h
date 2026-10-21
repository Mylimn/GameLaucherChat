#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QList>

#include "core/Types.h"

// 会话列表模型（FR-3.4）：只收录「有历史消息」的好友会话，按最后一条消息时间倒序。
// 未读是本地状态（PRD 未要求跨设备同步），随 ChatPush 累加、打开会话即清零。
class ConversationModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum Roles {
        UserIdRole = Qt::UserRole + 1,
        UsernameRole,
        DisplayNameRole,
        PreviewRole,
        TsRole,
        UnreadRole,
        OnlineRole,
    };

    explicit ConversationModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

    // 好友表整表刷新：更新名称/在线状态，并剔除已不是好友的会话（会话只对好友有意义）
    void setFriends(const QList<FriendInfo>& friends);
    bool setOnline(qint64 userId, bool online);

    // 记录一条消息为该会话的最新消息；incrementUnread=true 表示当前未打开该会话
    void upsert(qint64 userId, const QString& preview, qint64 ts, bool incrementUnread);
    void setUnread(qint64 userId, int unread);
    void clearUnread(qint64 userId);
    void clear();

    bool contains(qint64 userId) const;
    int  indexOf(qint64 userId) const;
    ConversationInfo at(int row) const;

private:
    // 时间倒序；同一时间戳维持插入顺序（std::stable_sort 语义由手写插入保证）
    void reorder();

    // 新建条目时用它补齐名称与在线状态：会话往往在好友表到位之后才由消息创建，
    // 若只等 PresencePush，条目会一直停留在默认的「离线」
    QHash<qint64, FriendInfo> m_friendIndex;

    QList<ConversationInfo> m_entries;
};
