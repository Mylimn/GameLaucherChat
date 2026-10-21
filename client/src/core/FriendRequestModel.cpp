#include "core/FriendRequestModel.h"

FriendRequestModel::FriendRequestModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int FriendRequestModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_requests.size();
}

QVariant FriendRequestModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_requests.size()) {
        return {};
    }
    const FriendRequestInfo& r = m_requests.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case DisplayNameRole:
        return r.displayName;
    case FromUserIdRole:
        return r.fromUserId;
    case UsernameRole:
        return r.username;
    case RemarkRole:
        return r.remark;
    default:
        return {};
    }
}

void FriendRequestModel::upsert(const FriendRequestInfo& request)
{
    const int row = indexOf(request.fromUserId);
    if (row >= 0) {
        m_requests[row] = request;
        const QModelIndex idx = index(row);
        emit dataChanged(idx, idx);
        return;
    }
    beginInsertRows(QModelIndex(), m_requests.size(), m_requests.size());
    m_requests.append(request);
    endInsertRows();
}

void FriendRequestModel::setAll(const QList<FriendRequestInfo>& requests)
{
    beginResetModel();
    m_requests = requests;
    endResetModel();
}

void FriendRequestModel::removeByFromUserId(qint64 fromUserId)
{
    const int row = indexOf(fromUserId);
    if (row < 0) {
        return;
    }
    beginRemoveRows(QModelIndex(), row, row);
    m_requests.removeAt(row);
    endRemoveRows();
}

void FriendRequestModel::clear()
{
    if (m_requests.isEmpty()) {
        return;
    }
    beginResetModel();
    m_requests.clear();
    endResetModel();
}

FriendRequestInfo FriendRequestModel::requestAt(int row) const
{
    if (row < 0 || row >= m_requests.size()) {
        return {};
    }
    return m_requests.at(row);
}

int FriendRequestModel::indexOf(qint64 fromUserId) const
{
    for (int i = 0; i < m_requests.size(); ++i) {
        if (m_requests.at(i).fromUserId == fromUserId) {
            return i;
        }
    }
    return -1;
}
