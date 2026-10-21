#pragma once

#include <QAbstractListModel>
#include <QList>

#include "core/Types.h"

// 待处理好友请求模型（FR-2.3）：接收他人请求并支持接受/拒绝。
class FriendRequestModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum Roles {
        FromUserIdRole = Qt::UserRole + 1,
        UsernameRole,
        DisplayNameRole,
        RemarkRole,
    };

    explicit FriendRequestModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

    // 已存在同一起始人的请求时只更新备注，不重复插入
    void upsert(const FriendRequestInfo& request);
    // 用服务端全量结果覆盖（主动拉取的语义，顺带清掉已失效的请求）
    void setAll(const QList<FriendRequestInfo>& requests);
    void removeByFromUserId(qint64 fromUserId);
    void clear();

    FriendRequestInfo requestAt(int row) const;

private:
    int indexOf(qint64 fromUserId) const;

    QList<FriendRequestInfo> m_requests;
};
