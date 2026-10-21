#include "ui/ChatPanel.h"

#include <QDateTime>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPainter>
#include <QPushButton>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

#include "core/MessageModel.h"
#include "core/Session.h"
#include "ui/Theme.h"

namespace {

// 气泡内边距 / 主体间距；与 TECH_DESIGN §7「间距只用 4/8/12/16/24」对齐
constexpr int kBubblePadH = 12;
constexpr int kBubblePadV = 8;
constexpr int kBubbleRadius = 10; // 圆角 10px（§7）
constexpr int kRowMarginH = 12;   // 列表左右留白
constexpr int kRowMarginV = 4;    // 上下留白
constexpr int kMaxBubbleRatio = 70; // 最大宽度约 70%
constexpr int kNameGap = 4;       // 来源标注与气泡的间距（4 的倍数，见 §7）

QString statusText(MessageStatus status)
{
    switch (status) {
    case MessageStatus::Sending:   return QStringLiteral("发送中…");
    case MessageStatus::Delivered: return QStringLiteral("已送达");
    case MessageStatus::Read:      return QStringLiteral("对方已读");
    case MessageStatus::Failed:    return QStringLiteral("发送失败");
    }
    return QString();
}

QString timeText(qint64 ts)
{
    if (ts <= 0) {
        return QString();
    }
    return QDateTime::fromMSecsSinceEpoch(ts).toString(QStringLiteral("HH:mm"));
}

// 单条消息的气泡绘制：自己靠右、主色底；对方靠左、卡片底（§7）。
// 气泡上方标注来源：对方显示昵称，自己显示「昵称 · me」。
class MessageDelegate : public QStyledItemDelegate {
public:
    MessageDelegate(QListView* view, qint64 selfUserId, const ChatPanel* panel)
        : m_view(view), m_selfUserId(selfUserId), m_panel(panel) {}

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        const Metrics m = measure(option, index);
        return QSize(width(option), m.nameH + m.nameGap + m.bubble.height() + 2 * kRowMarginV);
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        const Metrics m = measure(option, index);
        const bool mine = isMine(index);

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);

        if (!m.name.isEmpty()) {
            painter->setPen(QColor(Theme::kTextSecondary));
            painter->drawText(m.nameRect, Qt::AlignLeft | Qt::AlignVCenter, m.name);
        }

        painter->setPen(Qt::NoPen);
        painter->setBrush(mine ? QColor(Theme::kAccent) : QColor(Theme::kBgCard));
        painter->drawRoundedRect(m.bubble, kBubbleRadius, kBubbleRadius);

        painter->setPen(mine ? QColor(Theme::kTextOnAccent) : QColor(Theme::kTextPrimary));
        painter->drawText(m.textRect, Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignVCenter,
                          index.data(MessageModel::ContentRole).toString());

        if (!m.meta.isEmpty()) {
            const int status = index.data(MessageModel::StatusRole).toInt();
            painter->setPen(status == static_cast<int>(MessageStatus::Failed)
                                ? QColor(Theme::kDanger)
                                : QColor(Theme::kTextSecondary));
            painter->drawText(m.metaRect, Qt::AlignRight | Qt::AlignBottom, m.meta);
        }
        painter->restore();
    }

private:
    struct Metrics {
        QRect   nameRect;
        QRect   bubble;
        QRect   textRect;
        QRect   metaRect;
        QString name;
        QString meta;
        int     nameH = 0;
        int     nameGap = 0;
    };

    bool isMine(const QModelIndex& index) const
    {
        return index.data(MessageModel::FromUserIdRole).toLongLong() == m_selfUserId;
    }

    int width(const QStyleOptionViewItem& option) const
    {
        const int w = m_view && m_view->viewport() ? m_view->viewport()->width() : option.rect.width();
        return qMax(w, 200);
    }

    Metrics measure(const QStyleOptionViewItem& option, const QModelIndex& index) const
    {
        const bool mine = isMine(index);
        const QString content = index.data(MessageModel::ContentRole).toString();

        QString meta = timeText(index.data(MessageModel::TsRole).toLongLong());
        if (mine) {
            const QString status = statusText(static_cast<MessageStatus>(
                index.data(MessageModel::StatusRole).toInt()));
            meta = meta.isEmpty() ? status : QStringLiteral("%1 · %2").arg(meta, status);
        }

        const QFontMetrics fm(option.font);
        const int availW = qMax(200, width(option) - 2 * kRowMarginH);
        const int metaW = meta.isEmpty() ? 0 : fm.horizontalAdvance(meta) + 8;
        const int maxBubbleW = qMax(80, availW * kMaxBubbleRatio / 100 - metaW);

        const QRect textBound = fm.boundingRect(QRect(0, 0, maxBubbleW - 2 * kBubblePadH, 100000),
                                                Qt::TextWordWrap | Qt::AlignLeft, content);
        const int bubbleW = qBound(40, textBound.width() + 2 * kBubblePadH, maxBubbleW);
        const int bubbleH = textBound.height() + 2 * kBubblePadV;

        // 来源标注占掉气泡上方的一行；空名字（未选会话）时不留白
        const QString name = m_panel ? m_panel->senderLabel(mine) : QString();
        const int nameH = name.isEmpty() ? 0 : fm.height();
        const int nameGap = name.isEmpty() ? 0 : kNameGap;

        const int nameTop = option.rect.top() + kRowMarginV;
        const int top = nameTop + nameH + nameGap;
        Metrics m;
        if (mine) {
            const int metaRight = option.rect.left() + kRowMarginH + availW;
            m.bubble = QRect(metaRight - metaW - (metaW > 0 ? 8 : 0) - bubbleW, top, bubbleW, bubbleH);
            m.metaRect = QRect(m.bubble.right() + 8, top, qMax(0, metaW - 8), bubbleH);
        } else {
            m.bubble = QRect(option.rect.left() + kRowMarginH, top, bubbleW, bubbleH);
            m.metaRect = QRect(m.bubble.right() + 8, top, qMax(0, metaW - 8), bubbleH);
        }
        // 名字与气泡同侧对齐：自己的贴气泡右边缘，对方的贴气泡左边缘
        const int nameW = fm.horizontalAdvance(name);
        m.nameRect = QRect(mine ? m.bubble.right() - nameW + 1 : m.bubble.left(),
                           nameTop, nameW, nameH);
        m.textRect = m.bubble.adjusted(kBubblePadH, kBubblePadV, -kBubblePadH, -kBubblePadV);
        m.name = name;
        m.nameH = nameH;
        m.nameGap = nameGap;
        m.meta = meta;
        return m;
    }

    QListView*        m_view = nullptr;
    qint64            m_selfUserId = 0;
    const ChatPanel*  m_panel = nullptr;
};

} // namespace

ChatPanel::ChatPanel(Session* session, MessageModel* model, QWidget* parent)
    : QWidget(parent)
    , m_session(session)
    , m_model(model)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 16, 24, 16);
    layout->setSpacing(12);

    // ---- 顶部：对端信息 + 删除好友（FR-2.4）----
    auto* header = new QHBoxLayout;
    header->setSpacing(8);
    auto* headerColumn = new QVBoxLayout;
    headerColumn->setSpacing(2);
    m_peerNameLabel = new QLabel(this);
    m_peerNameLabel->setObjectName(QStringLiteral("panelTitle"));
    m_peerStatusLabel = new QLabel(this);
    m_peerStatusLabel->setObjectName(QStringLiteral("itemSubtitle"));
    headerColumn->addWidget(m_peerNameLabel);
    headerColumn->addWidget(m_peerStatusLabel);
    m_removeButton = new QPushButton(QStringLiteral("删除好友"), this);
    m_removeButton->setObjectName(QStringLiteral("miniButton"));
    header->addLayout(headerColumn, 1);
    header->addWidget(m_removeButton);

    // ---- 中部：加载更早 + 消息列表 ----
    m_loadMoreButton = new QPushButton(QStringLiteral("加载更早的消息"), this);
    m_loadMoreButton->setObjectName(QStringLiteral("miniButton"));

    m_emptyLabel = new QLabel(QStringLiteral("从左侧好友列表选择一个会话"), this);
    m_emptyLabel->setObjectName(QStringLiteral("emptyState"));
    m_emptyLabel->setAlignment(Qt::AlignCenter);

    m_messageList = new QListView(this);
    m_messageList->setObjectName(QStringLiteral("messageList"));
    m_messageList->setFrameShape(QFrame::NoFrame);
    m_messageList->setSelectionMode(QAbstractItemView::NoSelection);
    m_messageList->setFocusPolicy(Qt::NoFocus);
    m_messageList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_messageList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_messageList->setItemDelegate(new MessageDelegate(m_messageList, m_session->userId(), this));
    m_messageList->setModel(m_model);

    // ---- 底部：输入 + 发送（FR-3.2）----
    auto* inputRow = new QHBoxLayout;
    inputRow->setSpacing(8);
    m_input = new QLineEdit(this);
    m_input->setPlaceholderText(QStringLiteral("输入消息，回车发送"));
    m_sendButton = new QPushButton(QStringLiteral("发送"), this);
    m_sendButton->setObjectName(QStringLiteral("primaryButton"));
    inputRow->addWidget(m_input, 1);
    inputRow->addWidget(m_sendButton);

    layout->addLayout(header);
    layout->addWidget(m_loadMoreButton);
    layout->addWidget(m_emptyLabel, 1);
    layout->addWidget(m_messageList, 1);
    layout->addLayout(inputRow);

    connect(m_sendButton, &QPushButton::clicked, this, &ChatPanel::onSendClicked);
    connect(m_input, &QLineEdit::returnPressed, this, &ChatPanel::onSendClicked);
    connect(m_loadMoreButton, &QPushButton::clicked, this, &ChatPanel::onLoadMoreClicked);
    connect(m_removeButton, &QPushButton::clicked, this, [this]() {
        if (m_peerUserId != 0) {
            emit removeFriendRequested(m_peerUserId);
        }
    });

    connect(m_session, &Session::chatHistoryReceived, this, &ChatPanel::onHistoryReceived);
    connect(m_session, &Session::chatReceived, this, &ChatPanel::onChatReceived);
    connect(m_session, &Session::chatSendAck, this, &ChatPanel::onSendAck);

    m_loadingHistory = false;
    openConversation(FriendInfo{}); // 初始为空态
}

QString ChatPanel::senderLabel(bool mine) const
{
    if (!mine) {
        return m_peerDisplayName;
    }
    const QString name = m_session->displayName().isEmpty() ? m_session->username()
                                                            : m_session->displayName();
    return name.isEmpty() ? QStringLiteral("me") : QStringLiteral("%1 · me").arg(name);
}

void ChatPanel::openConversation(const FriendInfo& peer)
{
    m_peerUserId      = peer.userId;
    m_peerUsername    = peer.username;
    m_peerDisplayName = peer.displayName.isEmpty() ? peer.username : peer.displayName;
    m_peerOnline      = peer.online;
    m_hasMore         = false;

    m_model->clear();
    m_loadingHistory = false;
    if (m_peerUserId != 0) {
        // 断线期间发出的消息只在本机待发队列里，先补回再拉历史：
        // 服务端此刻不可达时历史请求会被直接丢弃，不补就整片空白了
        syncPendingOutgoing();
        m_session->requestHistory(m_peerUserId, 0, 30); // 进入会话即拉最新一页（FR-3.5）
    }
    refreshView();
}

void ChatPanel::setPeerOnline(bool online)
{
    m_peerOnline = online;
    updatePeerHeader();
}

void ChatPanel::updatePeerHeader()
{
    if (m_peerUserId == 0) {
        m_peerNameLabel->setText(QStringLiteral("未选择会话"));
        m_peerStatusLabel->setText(QStringLiteral("在左侧好友列表中选择一位好友开始聊天"));
        return;
    }
    m_peerNameLabel->setText(m_peerDisplayName);
    m_peerStatusLabel->setText(QStringLiteral("@%1 · %2")
                                   .arg(m_peerUsername,
                                        m_peerOnline ? QStringLiteral("在线") : QStringLiteral("离线")));
    m_peerStatusLabel->setProperty("state", m_peerOnline ? "online" : "offline");
    m_peerStatusLabel->style()->unpolish(m_peerStatusLabel);
    m_peerStatusLabel->style()->polish(m_peerStatusLabel);
}

void ChatPanel::refreshView()
{
    updatePeerHeader();

    const bool hasPeer = m_peerUserId != 0;
    const bool hasMessages = m_model->rowCount() > 0;

    m_messageList->setVisible(hasPeer && hasMessages);
    m_emptyLabel->setVisible(!hasPeer || !hasMessages);
    m_loadMoreButton->setVisible(hasPeer && hasMessages && m_hasMore);
    m_removeButton->setVisible(hasPeer);
    m_input->setEnabled(hasPeer);
    m_sendButton->setEnabled(hasPeer);

    if (!hasPeer) {
        m_emptyLabel->setText(QStringLiteral("从左侧好友列表选择一个会话"));
    } else if (!hasMessages) {
        m_emptyLabel->setText(QStringLiteral("还没有消息，打个招呼吧"));
    } else {
        m_emptyLabel->clear();
    }
}

void ChatPanel::onSendClicked()
{
    if (m_peerUserId == 0) {
        return;
    }
    const QString text = m_input->text().trimmed();
    if (text.isEmpty()) {
        return;
    }

    // 乐观上屏：先本地插入「发送中」，再由 ChatSendResp 回填状态（FR-3.2）
    const QString clientMsgId = m_session->sendChat(m_peerUserId, text);
    const qint64 ts = QDateTime::currentMSecsSinceEpoch();
    m_model->addOutgoing(m_session->userId(), m_peerUserId, text, clientMsgId, ts);
    m_input->clear();
    refreshView();
    scrollToBottom();
    emit messageSent(m_peerUserId, text, ts);
}

void ChatPanel::onLoadMoreClicked()
{
    if (m_peerUserId == 0 || !m_hasMore) {
        return;
    }
    m_loadingHistory = true;
    m_session->requestHistory(m_peerUserId, m_model->firstTs(), 30);
}

void ChatPanel::onHistoryReceived(qint64 peerUserId, bool hasMore,
                                  const QList<ChatMessageInfo>& messages, qint64 peerReadUpTo)
{
    if (peerUserId != m_peerUserId) {
        return; // 切换会话后到达的旧响应，丢弃
    }
    const bool prepend = m_loadingHistory;
    m_model->setHistory(messages, prepend);
    m_loadingHistory = false;
    m_hasMore = hasMore;
    if (!prepend) {
        // 历史整页替换会把「发送中」的本地消息冲掉（它们不在服务端历史里）
        syncPendingOutgoing();
        // 已读状态不在历史行里，用服务端回传的已读位置重建（否则切换会话会退回「已送达」）
        m_model->markPeerRead(peerReadUpTo);
    }
    refreshView();
    if (!prepend) {
        scrollToBottom();
        // FR-3.7：整页历史落地即视为已读，把最新一条对方消息的位置上报
        for (int row = m_model->rowCount() - 1; row >= 0; --row) {
            const ChatMessageInfo m = m_model->messageAt(row);
            if (m.serverMsgId > 0 && m.fromUserId == m_peerUserId) {
                m_session->reportRead(m_peerUserId, m.serverMsgId);
                break;
            }
        }
    }
}

void ChatPanel::syncPendingOutgoing()
{
    if (m_peerUserId == 0) {
        return;
    }
    for (const ChatMessageInfo& m : m_session->pendingOutgoing(m_peerUserId)) {
        if (m_model->hasClientMsgId(m.clientMsgId)) {
            continue;
        }
        m_model->addOutgoing(m.fromUserId, m.toUserId, m.content, m.clientMsgId, m.ts);
    }
}

void ChatPanel::onPeerRead(qint64 upToServerMsgId)
{
    m_model->markPeerRead(upToServerMsgId);
    refreshView();
}

void ChatPanel::onChatReceived(const ChatMessageInfo& message)
{
    if (message.fromUserId != m_peerUserId) {
        return; // 非当前会话的消息由 MainWindow 计入未读
    }
    m_model->appendIncoming(message);
    refreshView();
    scrollToBottom();
    // FR-3.7：打开着会话即视为已读，上报已读位置
    m_session->reportRead(m_peerUserId, message.serverMsgId);
}

void ChatPanel::onSendAck(const QString& clientMsgId, bool ok, qint64 serverMsgId, qint64 ts)
{
    m_model->confirmOutgoing(clientMsgId, ok, serverMsgId, ts);
    if (!ok) {
        // 失败时模型已置 Failed，无需额外动作（气泡旁显示「发送失败」）
        return;
    }
    scrollToBottom();
}

void ChatPanel::scrollToBottom()
{
    const int last = m_model->rowCount() - 1;
    if (last >= 0) {
        m_messageList->scrollToBottom();
    }
}
