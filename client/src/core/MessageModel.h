#pragma once

#include <QAbstractListModel>
#include <QList>

#include "core/Types.h"

// 单个会话的消息列表模型（FR-3.2 / FR-3.3 / FR-3.5）。
// 生命周期跟随「当前打开的会话」：切换好友时整体清空并重新拉取历史。
class MessageModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum Roles {
        ServerMsgIdRole = Qt::UserRole + 1,
        FromUserIdRole,
        ToUserIdRole,
        ContentRole,
        TsRole,
        OfflineRole,
        StatusRole,
        ClientMsgIdRole,
    };

    explicit MessageModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

    void clear();

    // 历史页：prepend=true 表示向上翻页（插入到头部），否则整体替换
    void setHistory(const QList<ChatMessageInfo>& messages, bool prepend);

    // 收到的消息；按 serverMsgId 去重（§10）
    void appendIncoming(const ChatMessageInfo& message);

    // 本地乐观上屏，返回插入的行号；status = Sending
    int addOutgoing(qint64 selfUserId, qint64 peerUserId, const QString& content,
                    const QString& clientMsgId, qint64 ts);

    // 服务端确认：成功则补 serverMsgId/ts 并置 Delivered，失败置 Failed
    void confirmOutgoing(const QString& clientMsgId, bool ok, qint64 serverMsgId, qint64 ts);

    // FR-3.7：对端读到 upToServerMsgId 为止，把自己发过的、已送达的消息标为已读
    void markPeerRead(qint64 upToServerMsgId);

    // 最早一条的 ts，用于历史分页的 beforeTs；空列表返回 0
    qint64 firstTs() const;

    // 是否已存在该本地消息（用于补回待发消息时去重）
    bool hasClientMsgId(const QString& clientMsgId) const;

    ChatMessageInfo messageAt(int row) const;

private:
    int indexOfServerMsgId(qint64 serverMsgId) const;
    int indexOfClientMsgId(const QString& clientMsgId) const;

    QList<ChatMessageInfo> m_messages;
};
