#include "core/FriendModel.h"

FriendModel::FriendModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int FriendModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_entries.size();
}

QVariant FriendModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size()) {
        return {};
    }
    const Entry& e = m_entries.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case DisplayNameRole:
        return e.info.displayName;
    case UserIdRole:
        return e.info.userId;
    case UsernameRole:
        return e.info.username;
    case OnlineRole:
        return e.info.online;
    case SignatureRole:
        return e.info.signature;
    case UnreadRole:
        return e.unread;
    default:
        return {};
    }
}

void FriendModel::setFriends(const QList<FriendInfo>& friends)
{
    // 未读计数是本地状态，整表刷新时按 userId 保留（接受好友后服务端会补发整表）
    QHash<qint64, int> unread;
    for (const Entry& e : std::as_const(m_entries)) {
        if (e.unread > 0) {
            unread.insert(e.info.userId, e.unread);
        }
    }

    beginResetModel();
    m_entries.clear();
    for (const FriendInfo& f : friends) {
        Entry e;
        e.info = f;
        e.unread = unread.value(f.userId, 0);
        m_entries.append(e);
    }
    endResetModel();
}

bool FriendModel::setOnline(qint64 userId, bool online)
{
    const int row = indexOf(userId);
    if (row < 0 || m_entries.at(row).info.online == online) {
        return false;
    }
    m_entries[row].info.online = online;
    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx, {OnlineRole});
    return true;
}

bool FriendModel::addUnread(qint64 userId)
{
    const int row = indexOf(userId);
    if (row < 0) {
        return false;
    }
    ++m_entries[row].unread;
    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx, {UnreadRole, Qt::DisplayRole});
    return true;
}

void FriendModel::clearUnread(qint64 userId)
{
    const int row = indexOf(userId);
    if (row < 0 || m_entries.at(row).unread == 0) {
        return;
    }
    m_entries[row].unread = 0;
    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx, {UnreadRole, Qt::DisplayRole});
}

bool FriendModel::contains(qint64 userId) const
{
    return indexOf(userId) >= 0;
}

int FriendModel::indexOf(qint64 userId) const
{
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).info.userId == userId) {
            return i;
        }
    }
    return -1;
}

FriendInfo FriendModel::friendAt(int row) const
{
    if (row < 0 || row >= m_entries.size()) {
        return {};
    }
    return m_entries.at(row).info;
}
