#include "core/ConversationModel.h"

#include <algorithm>

ConversationModel::ConversationModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int ConversationModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_entries.size();
}

QVariant ConversationModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size()) {
        return {};
    }
    const ConversationInfo& c = m_entries.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case DisplayNameRole:
        return c.displayName;
    case UserIdRole:
        return c.userId;
    case UsernameRole:
        return c.username;
    case PreviewRole:
        return c.preview;
    case TsRole:
        return c.ts;
    case UnreadRole:
        return c.unread;
    case OnlineRole:
        return c.online;
    default:
        return {};
    }
}

void ConversationModel::setFriends(const QList<FriendInfo>& friends)
{
    beginResetModel();
    m_friendIndex.clear();
    for (const FriendInfo& f : friends) {
        m_friendIndex.insert(f.userId, f);
    }
    // 会话只对仍是好友的人有意义：删除好友后同步移除（聊天记录仍在服务端）
    for (int i = m_entries.size() - 1; i >= 0; --i) {
        const auto it = m_friendIndex.constFind(m_entries.at(i).userId);
        if (it == m_friendIndex.cend()) {
            m_entries.removeAt(i);
            continue;
        }
        ConversationInfo& c = m_entries[i];
        c.username    = it->username;
        c.displayName = it->displayName;
        c.online      = it->online;
    }
    endResetModel();
}

bool ConversationModel::setOnline(qint64 userId, bool online)
{
    const int row = indexOf(userId);
    if (row < 0 || m_entries.at(row).online == online) {
        return false;
    }
    m_entries[row].online = online;
    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx, {OnlineRole});
    return true;
}

void ConversationModel::upsert(qint64 userId, const QString& preview, qint64 ts, bool incrementUnread)
{
    const int row = indexOf(userId);
    if (row < 0) {
        ConversationInfo c;
        c.userId    = userId;
        c.preview   = preview;
        c.ts        = ts;
        c.unread    = incrementUnread ? 1 : 0;
        // 会话条目通常晚于好友表建立：直接按快照补齐身份与在线状态，避免灰点/空名称
        const auto it = m_friendIndex.constFind(userId);
        if (it != m_friendIndex.cend()) {
            c.username    = it->username;
            c.displayName = it->displayName;
            c.online      = it->online;
        }
        beginResetModel();
        m_entries.append(c);
        reorder();
        endResetModel();
        return;
    }

    ConversationInfo& c = m_entries[row];
    c.preview = preview;
    c.ts      = ts;
    if (incrementUnread) {
        ++c.unread;
    }

    // 最新消息可能改变排序：有变化就整体重排，否则只刷新当前行
    const bool moved = row > 0 && m_entries.at(row - 1).ts < c.ts;
    if (moved) {
        beginResetModel();
        reorder();
        endResetModel();
        return;
    }
    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx, {PreviewRole, TsRole, UnreadRole, DisplayNameRole});
}

void ConversationModel::setUnread(qint64 userId, int unread)
{
    const int row = indexOf(userId);
    if (row < 0 || m_entries.at(row).unread == unread) {
        return;
    }
    m_entries[row].unread = unread;
    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx, {UnreadRole, DisplayNameRole});
}

void ConversationModel::clearUnread(qint64 userId)
{
    const int row = indexOf(userId);
    if (row < 0 || m_entries.at(row).unread == 0) {
        return;
    }
    m_entries[row].unread = 0;
    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx, {UnreadRole, DisplayNameRole});
}

void ConversationModel::clear()
{
    if (m_entries.isEmpty()) {
        return;
    }
    beginResetModel();
    m_entries.clear();
    endResetModel();
}

bool ConversationModel::contains(qint64 userId) const
{
    return indexOf(userId) >= 0;
}

int ConversationModel::indexOf(qint64 userId) const
{
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).userId == userId) {
            return i;
        }
    }
    return -1;
}

ConversationInfo ConversationModel::at(int row) const
{
    if (row < 0 || row >= m_entries.size()) {
        return {};
    }
    return m_entries.at(row);
}

void ConversationModel::reorder()
{
    std::stable_sort(m_entries.begin(), m_entries.end(),
                     [](const ConversationInfo& a, const ConversationInfo& b) {
                         return a.ts > b.ts;
                     });
}
