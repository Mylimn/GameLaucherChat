#include "ui/FriendListWidget.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QStyle>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "core/ConversationModel.h"
#include "core/FriendModel.h"
#include "core/FriendRequestModel.h"

namespace {

// 列表项容器统一用 transparent，让 QListWidget::item 的 hover/选中底色透出来
const char* kItemObjectName = "listItem";

// 会话项高度与好友项一致（44px，见 TECH_DESIGN §7）；预览单行截断，避免侧栏过窄时撑破
constexpr int kItemHeight = 44;
constexpr int kPreviewMaxChars = 16;

QLabel* makeDot(bool online, QWidget* parent)
{
    auto* dot = new QLabel(parent);
    dot->setObjectName(QStringLiteral("presenceDot"));
    dot->setProperty("state", online ? "online" : "offline");
    return dot;
}

QString previewText(const FriendRequestInfo& request)
{
    if (!request.remark.isEmpty()) {
        return request.remark;
    }
    return QStringLiteral("@%1 请求加你为好友").arg(request.username);
}

QString previewText(const QString& content)
{
    const QString oneLine = content.simplified();
    if (oneLine.size() <= kPreviewMaxChars) {
        return oneLine;
    }
    return oneLine.left(kPreviewMaxChars) + QStringLiteral("…");
}

} // namespace

FriendListWidget::FriendListWidget(FriendModel* friends, FriendRequestModel* requests,
                                   ConversationModel* conversations, QWidget* parent)
    : QWidget(parent)
    , m_friends(friends)
    , m_requests(requests)
    , m_conversations(conversations)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    // ---- 最近会话（FR-3.4）：只显示有历史消息的会话，无内容时整段隐藏 ----
    m_conversationTitle = new QLabel(QStringLiteral("最近会话"), this);
    m_conversationTitle->setObjectName(QStringLiteral("sectionTitle"));
    m_conversationList = new QListWidget(this);
    m_conversationList->setObjectName(QStringLiteral("conversationList"));
    m_conversationList->setFrameShape(QFrame::NoFrame);
    m_conversationList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    // 最多展示 4 条，更多由滚动承接，避免把下方好友区挤没
    m_conversationList->setMaximumHeight(kItemHeight * 4 + 4);

    // ---- 添加好友（FR-2.2）----
    auto* addRow = new QHBoxLayout;
    addRow->setSpacing(8);
    m_usernameEdit = new QLineEdit(this);
    m_usernameEdit->setPlaceholderText(QStringLiteral("输入用户名"));
    m_addButton = new QPushButton(QStringLiteral("添加"), this);
    m_addButton->setObjectName(QStringLiteral("secondaryButton"));
    addRow->addWidget(m_usernameEdit, 1);
    addRow->addWidget(m_addButton);

    m_messageLabel = new QLabel(this);
    m_messageLabel->setObjectName(QStringLiteral("messageLabel"));
    m_messageLabel->setWordWrap(true);

    // ---- 页签：好友 / 好友请求 ----
    m_tabs = new QTabWidget(this);

    auto* friendsPage = new QWidget(m_tabs);
    auto* friendsLayout = new QVBoxLayout(friendsPage);
    friendsLayout->setContentsMargins(0, 8, 0, 0);
    friendsLayout->setSpacing(0);
    m_friendEmpty = new QLabel(QStringLiteral("还没有好友\n在上方输入用户名添加"), friendsPage);
    m_friendEmpty->setObjectName(QStringLiteral("emptyState"));
    m_friendEmpty->setAlignment(Qt::AlignCenter);
    m_friendList = new QListWidget(friendsPage);
    m_friendList->setObjectName(QStringLiteral("friendList"));
    m_friendList->setFrameShape(QFrame::NoFrame);
    m_friendList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    friendsLayout->addWidget(m_friendEmpty);
    friendsLayout->addWidget(m_friendList, 1);

    auto* requestsPage = new QWidget(m_tabs);
    auto* requestsLayout = new QVBoxLayout(requestsPage);
    requestsLayout->setContentsMargins(0, 8, 0, 0);
    requestsLayout->setSpacing(0);
    m_requestEmpty = new QLabel(QStringLiteral("暂无好友请求"), requestsPage);
    m_requestEmpty->setObjectName(QStringLiteral("emptyState"));
    m_requestEmpty->setAlignment(Qt::AlignCenter);
    m_requestList = new QListWidget(requestsPage);
    m_requestList->setObjectName(QStringLiteral("requestList"));
    m_requestList->setFrameShape(QFrame::NoFrame);
    m_requestList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    requestsLayout->addWidget(m_requestEmpty);
    requestsLayout->addWidget(m_requestList, 1);

    m_tabs->addTab(friendsPage, QStringLiteral("好友"));
    m_tabs->addTab(requestsPage, QStringLiteral("好友请求"));

    layout->addWidget(m_conversationTitle);
    layout->addWidget(m_conversationList);
    layout->addLayout(addRow);
    layout->addWidget(m_messageLabel);
    layout->addWidget(m_tabs, 1);

    m_messageTimer = new QTimer(this);
    m_messageTimer->setSingleShot(true);
    connect(m_messageTimer, &QTimer::timeout, m_messageLabel, [this]() { m_messageLabel->clear(); });

    // 模型变化 → 重建列表（好友数量很小，整表重建足够简单可靠）
    connect(m_friends, &QAbstractItemModel::modelReset,      this, &FriendListWidget::rebuildFriends);
    connect(m_friends, &QAbstractItemModel::rowsInserted,    this, &FriendListWidget::rebuildFriends);
    connect(m_friends, &QAbstractItemModel::rowsRemoved,     this, &FriendListWidget::rebuildFriends);
    connect(m_friends, &QAbstractItemModel::dataChanged,     this, &FriendListWidget::rebuildFriends);
    connect(m_requests, &QAbstractItemModel::modelReset,     this, &FriendListWidget::rebuildRequests);
    connect(m_requests, &QAbstractItemModel::rowsInserted,   this, &FriendListWidget::rebuildRequests);
    connect(m_requests, &QAbstractItemModel::rowsRemoved,    this, &FriendListWidget::rebuildRequests);
    connect(m_requests, &QAbstractItemModel::dataChanged,    this, &FriendListWidget::rebuildRequests);
    connect(m_conversations, &QAbstractItemModel::modelReset,   this, &FriendListWidget::rebuildConversations);
    connect(m_conversations, &QAbstractItemModel::rowsInserted, this, &FriendListWidget::rebuildConversations);
    connect(m_conversations, &QAbstractItemModel::rowsRemoved,  this, &FriendListWidget::rebuildConversations);
    connect(m_conversations, &QAbstractItemModel::dataChanged,  this, &FriendListWidget::rebuildConversations);

    connect(m_addButton, &QPushButton::clicked, this, &FriendListWidget::onAddClicked);
    connect(m_usernameEdit, &QLineEdit::returnPressed, this, &FriendListWidget::onAddClicked);
    connect(m_friendList, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        if (item) {
            emit friendSelected(item->data(Qt::UserRole).toLongLong());
        }
    });
    connect(m_conversationList, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        if (item) {
            emit conversationSelected(item->data(Qt::UserRole).toLongLong());
        }
    });

    rebuildFriends();
    rebuildRequests();
    rebuildConversations();
}

void FriendListWidget::setActivePeer(qint64 userId)
{
    m_activePeer = userId;
    applyHighlight();
}

void FriendListWidget::applyHighlight()
{
    // 会话列表与好友列表共用一个「当前会话」：只在包含该项的列表上显示选中态
    auto highlight = [this](QListWidget* list) {
        for (int row = 0; row < list->count(); ++row) {
            QListWidgetItem* item = list->item(row);
            if (item->data(Qt::UserRole).toLongLong() == m_activePeer && m_activePeer != 0) {
                list->setCurrentItem(item);
                return;
            }
        }
        list->clearSelection();
        list->setCurrentRow(-1);
    };
    highlight(m_conversationList);
    highlight(m_friendList);
}

void FriendListWidget::showMessage(const QString& text, bool ok)
{
    m_messageLabel->setText(text);
    m_messageLabel->setProperty("tone", ok ? "success" : "error");
    // 动态属性改变后需要重新求值样式，否则不会重绘
    m_messageLabel->style()->unpolish(m_messageLabel);
    m_messageLabel->style()->polish(m_messageLabel);
    m_messageTimer->start(4000);
}

void FriendListWidget::onAddClicked()
{
    const QString username = m_usernameEdit->text().trimmed();
    if (username.isEmpty()) {
        showMessage(QStringLiteral("请输入要添加的用户名"), false);
        return;
    }
    emit addFriendRequested(username);
}

void FriendListWidget::rebuildFriends()
{
    m_friendList->clear();
    for (int row = 0; row < m_friends->rowCount(); ++row) {
        const QModelIndex idx = m_friends->index(row);
        FriendInfo info = m_friends->friendAt(row);
        const int unread = m_friends->data(idx, FriendModel::UnreadRole).toInt();

        auto* item = new QListWidgetItem(m_friendList);
        item->setData(Qt::UserRole, info.userId);
        item->setSizeHint(QSize(0, kItemHeight));
        m_friendList->setItemWidget(item, makeFriendItem(info, unread));
    }
    applyHighlight();
    updateEmptyStates();
}

void FriendListWidget::rebuildConversations()
{
    m_conversationList->clear();
    for (int row = 0; row < m_conversations->rowCount(); ++row) {
        const ConversationInfo info = m_conversations->at(row);
        auto* item = new QListWidgetItem(m_conversationList);
        item->setData(Qt::UserRole, info.userId);
        item->setSizeHint(QSize(0, kItemHeight));
        m_conversationList->setItemWidget(item, makeConversationItem(info));
    }
    applyHighlight();
    updateEmptyStates();
}

void FriendListWidget::rebuildRequests()
{
    m_requestList->clear();
    for (int row = 0; row < m_requests->rowCount(); ++row) {
        const FriendRequestInfo request = m_requests->requestAt(row);
        auto* item = new QListWidgetItem(m_requestList);
        item->setData(Qt::UserRole, request.fromUserId);
        item->setSizeHint(QSize(0, 56));
        m_requestList->setItemWidget(item, makeRequestItem(request));
    }

    const int count = m_requests->rowCount();
    m_tabs->setTabText(1, count > 0 ? QStringLiteral("好友请求 (%1)").arg(count)
                                    : QStringLiteral("好友请求"));
    updateEmptyStates();
}

void FriendListWidget::updateEmptyStates()
{
    const bool hasConversations = m_conversationList->count() > 0;
    m_conversationList->setVisible(hasConversations);
    m_conversationTitle->setVisible(hasConversations);

    const bool hasFriends = m_friendList->count() > 0;
    m_friendList->setVisible(hasFriends);
    m_friendEmpty->setVisible(!hasFriends);

    const bool hasRequests = m_requestList->count() > 0;
    m_requestList->setVisible(hasRequests);
    m_requestEmpty->setVisible(!hasRequests);
}

QWidget* FriendListWidget::makeFriendItem(const FriendInfo& info, int unread)
{
    auto* row = new QWidget(m_friendList);
    row->setObjectName(QString::fromLatin1(kItemObjectName));

    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(12, 6, 12, 6);
    layout->setSpacing(8);

    auto* textColumn = new QVBoxLayout;
    textColumn->setSpacing(2);

    auto* nameRow = new QHBoxLayout;
    nameRow->setSpacing(6);
    auto* nameLabel = new QLabel(info.displayName, row);
    nameLabel->setObjectName(QStringLiteral("itemTitle"));
    nameLabel->setProperty("unread", unread > 0);
    nameRow->addWidget(nameLabel);
    nameRow->addStretch(1);
    if (unread > 0) {
        auto* badge = new QLabel(unread > 99 ? QStringLiteral("99+") : QString::number(unread), row);
        badge->setObjectName(QStringLiteral("unreadBadge"));
        badge->setAlignment(Qt::AlignCenter);
        nameRow->addWidget(badge);
    }

    auto* accountLabel = new QLabel(QStringLiteral("@%1").arg(info.username), row);
    accountLabel->setObjectName(QStringLiteral("itemSubtitle"));

    textColumn->addLayout(nameRow);
    textColumn->addWidget(accountLabel);

    layout->addLayout(textColumn, 1);
    layout->addWidget(makeDot(info.online, row));
    return row;
}

QWidget* FriendListWidget::makeConversationItem(const ConversationInfo& info)
{
    auto* row = new QWidget(m_conversationList);
    row->setObjectName(QString::fromLatin1(kItemObjectName));

    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(12, 6, 12, 6);
    layout->setSpacing(8);

    auto* textColumn = new QVBoxLayout;
    textColumn->setSpacing(2);

    auto* nameRow = new QHBoxLayout;
    nameRow->setSpacing(6);
    auto* nameLabel = new QLabel(info.displayName.isEmpty() ? info.username : info.displayName, row);
    nameLabel->setObjectName(QStringLiteral("itemTitle"));
    nameLabel->setProperty("unread", info.unread > 0);
    nameRow->addWidget(nameLabel);
    nameRow->addWidget(makeDot(info.online, row));
    nameRow->addStretch(1);

    auto* previewLabel = new QLabel(previewText(info.preview), row);
    previewLabel->setObjectName(QStringLiteral("itemSubtitle"));

    textColumn->addLayout(nameRow);
    textColumn->addWidget(previewLabel);

    layout->addLayout(textColumn, 1);
    if (info.unread > 0) {
        auto* badge = new QLabel(info.unread > 99 ? QStringLiteral("99+") : QString::number(info.unread), row);
        badge->setObjectName(QStringLiteral("unreadBadge"));
        badge->setAlignment(Qt::AlignCenter);
        layout->addWidget(badge, 0, Qt::AlignVCenter);
    }
    return row;
}

QWidget* FriendListWidget::makeRequestItem(const FriendRequestInfo& request)
{
    auto* row = new QWidget(m_requestList);
    row->setObjectName(QString::fromLatin1(kItemObjectName));

    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(12, 6, 12, 6);
    layout->setSpacing(8);

    auto* textColumn = new QVBoxLayout;
    textColumn->setSpacing(2);
    auto* nameLabel = new QLabel(request.displayName, row);
    nameLabel->setObjectName(QStringLiteral("itemTitle"));
    auto* remarkLabel = new QLabel(previewText(request), row);
    remarkLabel->setObjectName(QStringLiteral("itemSubtitle"));
    textColumn->addWidget(nameLabel);
    textColumn->addWidget(remarkLabel);

    auto* acceptButton = new QPushButton(QStringLiteral("接受"), row);
    acceptButton->setObjectName(QStringLiteral("miniButtonAccent"));
    auto* rejectButton = new QPushButton(QStringLiteral("拒绝"), row);
    rejectButton->setObjectName(QStringLiteral("miniButton"));

    const qint64 fromUserId = request.fromUserId;
    connect(acceptButton, &QPushButton::clicked, this, [this, fromUserId]() {
        emit requestHandled(fromUserId, true);
    });
    connect(rejectButton, &QPushButton::clicked, this, [this, fromUserId]() {
        emit requestHandled(fromUserId, false);
    });

    auto* buttons = new QHBoxLayout;
    buttons->setSpacing(6);
    buttons->addWidget(acceptButton);
    buttons->addWidget(rejectButton);

    layout->addLayout(textColumn, 1);
    layout->addLayout(buttons);
    return row;
}
