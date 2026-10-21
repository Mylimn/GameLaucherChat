#include "core/MessageModel.h"

MessageModel::MessageModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int MessageModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_messages.size();
}

QVariant MessageModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_messages.size()) {
        return {};
    }
    const ChatMessageInfo& m = m_messages.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case ContentRole:
        return m.content;
    case ServerMsgIdRole:
        return m.serverMsgId;
    case FromUserIdRole:
        return m.fromUserId;
    case ToUserIdRole:
        return m.toUserId;
    case TsRole:
        return m.ts;
    case OfflineRole:
        return m.offline;
    case StatusRole:
        return static_cast<int>(m.status);
    case ClientMsgIdRole:
        return m.clientMsgId;
    default:
        return {};
    }
}

void MessageModel::clear()
{
    if (m_messages.isEmpty()) {
        return;
    }
    beginResetModel();
    m_messages.clear();
    endResetModel();
}

void MessageModel::setHistory(const QList<ChatMessageInfo>& messages, bool prepend)
{
    if (!prepend) {
        beginResetModel();
        m_messages = messages;
        endResetModel();
        return;
    }
    if (messages.isEmpty()) {
        return;
    }
    beginInsertRows(QModelIndex(), 0, messages.size() - 1);
    for (int i = messages.size() - 1; i >= 0; --i) {
        m_messages.prepend(messages.at(i));
    }
    endInsertRows();
}

void MessageModel::appendIncoming(const ChatMessageInfo& message)
{
    // 同一 serverMsgId 已有（重复推送/历史与实时重叠）则忽略
    if (message.serverMsgId > 0 && indexOfServerMsgId(message.serverMsgId) >= 0) {
        return;
    }
    beginInsertRows(QModelIndex(), m_messages.size(), m_messages.size());
    m_messages.append(message);
    endInsertRows();
}

int MessageModel::addOutgoing(qint64 selfUserId, qint64 peerUserId, const QString& content,
                              const QString& clientMsgId, qint64 ts)
{
    ChatMessageInfo m;
    m.fromUserId  = selfUserId;
    m.toUserId    = peerUserId;
    m.content     = content;
    m.ts          = ts;
    m.status      = MessageStatus::Sending;
    m.clientMsgId = clientMsgId;

    const int row = m_messages.size();
    beginInsertRows(QModelIndex(), row, row);
    m_messages.append(m);
    endInsertRows();
    return row;
}

void MessageModel::confirmOutgoing(const QString& clientMsgId, bool ok, qint64 serverMsgId, qint64 ts)
{
    const int row = indexOfClientMsgId(clientMsgId);
    if (row < 0) {
        return;
    }
    ChatMessageInfo& m = m_messages[row];
    m.status = ok ? MessageStatus::Delivered : MessageStatus::Failed;
    if (ok) {
        m.serverMsgId = serverMsgId;
        m.ts = ts;
    }
    m.clientMsgId.clear(); // 已确认，不再需要本地标识

    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx, {StatusRole, ServerMsgIdRole, TsRole, ClientMsgIdRole});
}

void MessageModel::markPeerRead(qint64 upToServerMsgId)
{
    for (int row = 0; row < m_messages.size(); ++row) {
        ChatMessageInfo& m = m_messages[row];
        if (m.status != MessageStatus::Delivered) {
            continue; // 只升不降：已读不回退，发送中/失败不受影响
        }
        if (m.serverMsgId <= 0 || m.serverMsgId > upToServerMsgId) {
            continue;
        }
        m.status = MessageStatus::Read;
        const QModelIndex idx = index(row);
        emit dataChanged(idx, idx, {StatusRole});
    }
}

qint64 MessageModel::firstTs() const
{
    return m_messages.isEmpty() ? 0 : m_messages.first().ts;
}

bool MessageModel::hasClientMsgId(const QString& clientMsgId) const
{
    return indexOfClientMsgId(clientMsgId) >= 0;
}

ChatMessageInfo MessageModel::messageAt(int row) const
{
    if (row < 0 || row >= m_messages.size()) {
        return {};
    }
    return m_messages.at(row);
}

int MessageModel::indexOfServerMsgId(qint64 serverMsgId) const
{
    for (int i = 0; i < m_messages.size(); ++i) {
        if (m_messages.at(i).serverMsgId == serverMsgId) {
            return i;
        }
    }
    return -1;
}

int MessageModel::indexOfClientMsgId(const QString& clientMsgId) const
{
    if (clientMsgId.isEmpty()) {
        return -1;
    }
    for (int i = 0; i < m_messages.size(); ++i) {
        if (m_messages.at(i).clientMsgId == clientMsgId) {
            return i;
        }
    }
    return -1;
}
