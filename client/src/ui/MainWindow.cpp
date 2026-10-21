#include "ui/MainWindow.h"

#include <QCloseEvent>
#include <QApplication>
#include <QDebug>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QStyle>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QWidget>

#include "core/ConversationModel.h"
#include "core/FriendModel.h"
#include "core/FriendRequestModel.h"
#include "core/MessageModel.h"
#include "launcher/GameLibrary.h"
#include "launcher/GameProcessManager.h"
#include "ui/ChatPanel.h"
#include "ui/FriendListWidget.h"
#include "ui/GameLauncherPanel.h"
#include "ui/Theme.h"
#include "ui/WindowChrome.h"

namespace {

QString stateText(Session::State state)
{
    switch (state) {
    case Session::State::Disconnected: return QStringLiteral("未连接");
    case Session::State::Connecting:   return QStringLiteral("连接中…");
    case Session::State::Connected:    return QStringLiteral("已连接");
    case Session::State::LoggedIn:     return QStringLiteral("已连接");
    }
    return QString();
}

QString stateKey(Session::State state)
{
    switch (state) {
    case Session::State::Disconnected: return QStringLiteral("offline");
    case Session::State::Connecting:   return QStringLiteral("connecting");
    case Session::State::Connected:    return QStringLiteral("online");
    case Session::State::LoggedIn:     return QStringLiteral("online");
    }
    return QStringLiteral("offline");
}

} // namespace

MainWindow::MainWindow(Session* session, QWidget* parent)
    : QMainWindow(parent)
    , m_session(session)
    , m_friendModel(new FriendModel(this))
    , m_requestModel(new FriendRequestModel(this))
    , m_conversationModel(new ConversationModel(this))
    , m_messageModel(new MessageModel(this))
    , m_gameLibrary(new GameLibrary(session->userId(), this))
    , m_gameProcesses(new GameProcessManager(this))
{
    // 先加载游戏库，面板构造时才能拿到读取错误（见 GameLauncherPanel 构造）
    QString loadError;
    if (!m_gameLibrary->load(&loadError)) {
        qWarning().noquote() << "游戏库读取失败：" << loadError;
    }

    buildUi();

    connect(m_session, &Session::stateChanged,   this, &MainWindow::onStateChanged);
    connect(m_session, &Session::loggedOut,      this, &MainWindow::onSessionLoggedOut);
    connect(m_session, &Session::errorOccurred,  this, &MainWindow::onErrorOccurred);
    connect(m_session, &Session::reconnectScheduled, this, &MainWindow::onReconnectScheduled);
    connect(m_session, &Session::connectionRestored, this, &MainWindow::onConnectionRestored);
    connect(m_session, &Session::reloginFailed,  this, &MainWindow::onReloginFailed);

    // 好友/私聊入站 → 模型
    connect(m_session, &Session::friendListReceived,  this, &MainWindow::onFriendListReceived);
    connect(m_session, &Session::presenceChanged,     this, &MainWindow::onPresenceChanged);
    connect(m_session, &Session::friendRequestReceived, this, &MainWindow::onFriendRequestReceived);
    connect(m_session, &Session::friendRequestsReceived, this, &MainWindow::onFriendRequestsReceived);
    connect(m_session, &Session::friendAddResult,     this, &MainWindow::onFriendAddResult);
    connect(m_session, &Session::friendHandleResult,  this, &MainWindow::onFriendHandleResult);
    connect(m_session, &Session::friendRemoveResult,  this, &MainWindow::onFriendRemoveResult);
    connect(m_session, &Session::chatReceived,        this, &MainWindow::onChatReceived);
    connect(m_session, &Session::chatHistoryReceived, this, &MainWindow::onConversationHistory);
    connect(m_session, &Session::chatReadReceived, this, [this](qint64 peerUserId, qint64 upTo) {
        if (peerUserId == m_chatPanel->peerUserId()) {
            m_chatPanel->onPeerRead(upTo);
        }
    });

    // 面板 → 会话层
    connect(m_friendList, &FriendListWidget::friendSelected,       this, &MainWindow::onFriendSelected);
    connect(m_friendList, &FriendListWidget::conversationSelected, this, &MainWindow::onFriendSelected);
    connect(m_friendList, &FriendListWidget::addFriendRequested,   this, &MainWindow::onAddFriendRequested);
    connect(m_friendList, &FriendListWidget::requestHandled,       this, &MainWindow::onRequestHandled);
    connect(m_chatPanel, &ChatPanel::messageSent,                  this, [this](qint64 peerUserId, const QString& content, qint64 ts) {
        // 本地已发出即刷新会话预览；服务端回执只决定气泡状态，不影响「最后一条是谁发的」
        m_conversationModel->upsert(peerUserId, content, ts, false);
    });
    connect(m_chatPanel, &ChatPanel::removeFriendRequested,      this, &MainWindow::onRemoveFriendRequested);
    connect(m_logoutButton, &QPushButton::clicked, this, &MainWindow::onLogoutClicked);

    updateStatus(m_session->state());

    // 登录瞬间到达的推送：服务端在 LoginResp 之后立即补发，那时主窗口还不存在，
    // 由 Session 暂存，这里一次性补上（否则登录前发出的好友请求/离线消息会「凭空消失」）
    m_session->notifyUiReady();
    QList<FriendInfo> pendingFriends;
    if (m_session->takePendingFriends(&pendingFriends)) {
        onFriendListReceived(pendingFriends);
    }
    const QList<FriendRequestInfo> pendingRequests = m_session->takePendingRequests();
    for (const FriendRequestInfo& request : pendingRequests) {
        m_requestModel->upsert(request);
    }
    const QList<ChatMessageInfo> pendingChats = m_session->takePendingChats();
    for (const ChatMessageInfo& message : pendingChats) {
        onChatReceived(message);
    }

    // 推送是「尽力而为」：服务端的补推若因任何原因丢失，这里以数据库为准再拉一次全量请求，
    // 保证「对方已申请但界面看不到」不会成为持久状态
    m_session->requestFriendRequests();
}

MainWindow::~MainWindow()
{
    // FR-4.5：退出（含登出销毁窗口）时清理由自己拉起的游戏进程，避免留下孤儿进程
    if (m_gameProcesses) {
        m_gameProcesses->killAll();
    }
}

void MainWindow::buildUi()
{
    setWindowTitle(QStringLiteral("游戏启动器 + 聊天"));
    resize(1100, 720);
    setWindowOpacity(Theme::kWindowOpacity); // 窗口略透明，避免整块死板的不透明背景
    applyDarkTitleBar(this);                 // 系统标题栏跟随深色主题

    auto* central = new QWidget(this);
    auto* layout = new QHBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // ---- 左侧栏：自身信息 + 连接状态 + 好友面板 + 登出 ----
    auto* sidebar = new QFrame(central);
    sidebar->setObjectName(QStringLiteral("sidebar"));
    sidebar->setFixedWidth(260);

    auto* sideLayout = new QVBoxLayout(sidebar);
    sideLayout->setContentsMargins(16, 24, 16, 16);
    sideLayout->setSpacing(8);

    const QString displayName = m_session->displayName().isEmpty()
                                    ? m_session->username()
                                    : m_session->displayName();

    m_nameLabel = new QLabel(displayName, sidebar);
    m_nameLabel->setObjectName(QStringLiteral("userName"));
    m_accountLabel = new QLabel(QStringLiteral("@%1").arg(m_session->username()), sidebar);
    m_accountLabel->setObjectName(QStringLiteral("userAccount"));

    m_statusLabel = new QLabel(sidebar);
    m_statusLabel->setObjectName(QStringLiteral("statusLabel"));

    m_friendList = new FriendListWidget(m_friendModel, m_requestModel, m_conversationModel, sidebar);

    m_logoutButton = new QPushButton(QStringLiteral("登出"), sidebar);
    m_logoutButton->setObjectName(QStringLiteral("secondaryButton"));

    sideLayout->addWidget(m_nameLabel);
    sideLayout->addWidget(m_accountLabel);
    sideLayout->addSpacing(4);
    sideLayout->addWidget(m_statusLabel);
    sideLayout->addSpacing(12);
    sideLayout->addWidget(m_friendList, 1);
    sideLayout->addWidget(m_logoutButton);

    // ---- 右侧内容区：页签（会话 / 游戏） ----
    m_chatPanel = new ChatPanel(m_session, m_messageModel, central);
    m_gamePanel = new GameLauncherPanel(m_gameLibrary, m_gameProcesses, central);

    m_rightTabs = new QTabWidget(central);
    m_rightTabs->setObjectName(QStringLiteral("rightTabs"));
    m_rightTabs->addTab(m_chatPanel, QStringLiteral("会话"));
    m_rightTabs->addTab(m_gamePanel, QStringLiteral("游戏"));

    layout->addWidget(sidebar);
    layout->addWidget(m_rightTabs, 1);
    setCentralWidget(central);
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    emit closed();
    QMainWindow::closeEvent(event);
}

void MainWindow::updateStatus(Session::State state)
{
    setStatus(stateText(state), stateKey(state));
}

void MainWindow::setStatus(const QString& text, const QString& key)
{
    m_statusLabel->setText(QStringLiteral("● %1").arg(text));
    m_statusLabel->setProperty("state", key);
    m_statusLabel->style()->unpolish(m_statusLabel);
    m_statusLabel->style()->polish(m_statusLabel);
}

void MainWindow::onStateChanged(Session::State state)
{
    updateStatus(state);
}

void MainWindow::onLogoutClicked()
{
    m_session->logout();
}

void MainWindow::onSessionLoggedOut()
{
    m_friendModel->setFriends({});
    m_requestModel->clear();
    m_conversationModel->clear();
    m_chatPanel->openConversation(FriendInfo{});
    m_friendList->setActivePeer(0);
    emit loggedOut();
}

void MainWindow::onErrorOccurred(const QString& message)
{
    m_friendList->showMessage(message, false);
}

void MainWindow::onReconnectScheduled(int attempt, int delayMs)
{
    // 退避等待期间给明确反馈，避免用户以为客户端卡死（FR-1.3）
    setStatus(QStringLiteral("重连中…（第 %1 次，%2 秒后）")
                  .arg(attempt)
                  .arg(qMax(1, delayMs / 1000)),
              QStringLiteral("connecting"));
    m_friendList->showMessage(QStringLiteral("与服务端的连接已断开，正在自动重连…"), false);
}

void MainWindow::onConnectionRestored()
{
    m_friendList->showMessage(QStringLiteral("已重新连接"), true);
}

void MainWindow::onReloginFailed(const QString& message)
{
    // 服务端拒绝自动重登：回到登录页重新输入口令（原因由 Session::lastError 带到登录框）
    m_friendList->showMessage(message, false);
    emit loggedOut();
}

// ---------------------------------------------------------------- 好友

void MainWindow::onFriendListReceived(const QList<FriendInfo>& friends)
{
    m_friendModel->setFriends(friends);
    // 会话条目的名称/在线状态以好友表为准（会话本身只由消息产生）
    m_conversationModel->setFriends(friends);

    // 会话列表不单独拉取：登录后为「尚无最后一条消息」的好友各拉 1 条历史，
    // 把服务端已有的聊天记录补成会话条目（N 次轻量请求，好友量级下可接受）
    for (const FriendInfo& f : friends) {
        if (!m_conversationModel->contains(f.userId)) {
            m_session->requestHistory(f.userId, 0, 1);
        }
    }

    // 服务端每次补发整表都刷新一次；若当前会话对象已被删除，则回到空态
    const qint64 peer = m_chatPanel->peerUserId();
    if (peer != 0 && !m_friendModel->contains(peer)) {
        m_chatPanel->openConversation(FriendInfo{});
    } else if (peer != 0) {
        m_chatPanel->setPeerOnline(m_friendModel->friendAt(m_friendModel->indexOf(peer)).online);
    }
}

void MainWindow::onConversationHistory(qint64 peerUserId, bool hasMore,
                                       const QList<ChatMessageInfo>& messages, qint64 peerReadUpTo)
{
    Q_UNUSED(hasMore);
    Q_UNUSED(peerReadUpTo); // 只用于当前打开会话的已读重建，由 ChatPanel 处理
    // 只为「补齐会话条目」服务：打开中的会话由 ChatPanel 自行处理，避免重复入库
    if (messages.isEmpty() || peerUserId == m_chatPanel->peerUserId()) {
        return;
    }
    const ChatMessageInfo& last = messages.last();
    const bool fromSelf = last.fromUserId == m_session->userId();
    const int row = m_friendModel->indexOf(peerUserId);
    const int unread = (row >= 0 && !fromSelf)
                           ? m_friendModel->data(m_friendModel->index(row), FriendModel::UnreadRole).toInt()
                           : 0;
    m_conversationModel->upsert(peerUserId, last.content, last.ts, false);
    // 角标以好友列表的累计未读为准（那是由 ChatPush 累加出来的）
    if (unread > 0) {
        m_conversationModel->setUnread(peerUserId, unread);
    }
}

void MainWindow::onPresenceChanged(qint64 userId, bool online)
{
    m_friendModel->setOnline(userId, online);
    m_conversationModel->setOnline(userId, online);
    if (userId == m_chatPanel->peerUserId()) {
        m_chatPanel->setPeerOnline(online);
    }
}

void MainWindow::onFriendRequestReceived(const FriendRequestInfo& request)
{
    m_requestModel->upsert(request);
    m_friendList->showMessage(QStringLiteral("%1 请求加你为好友")
                                  .arg(request.displayName.isEmpty() ? request.username
                                                                     : request.displayName),
                              true);
}

void MainWindow::onFriendRequestsReceived(const QList<FriendRequestInfo>& requests)
{
    // 全量覆盖：主动拉取的结果就是权威快照（既补齐漏掉的，也清掉已处理的）
    m_requestModel->setAll(requests);
}

void MainWindow::onFriendAddResult(bool ok, const QString& message)
{
    m_friendList->showMessage(message, ok);
}

void MainWindow::onFriendHandleResult(bool ok, const QString& message, qint64 fromUserId)
{
    if (ok) {
        m_requestModel->removeByFromUserId(fromUserId);
    }
    m_friendList->showMessage(message.isEmpty()
                                  ? (ok ? QStringLiteral("已处理该好友请求")
                                        : QStringLiteral("处理好友请求失败"))
                                  : message,
                              ok);
    // 接受成功后服务端会主动补发双方的好友列表，无需客户端额外拉取
}

void MainWindow::onFriendRemoveResult(bool ok, qint64 userId)
{
    if (ok && userId == m_chatPanel->peerUserId()) {
        m_chatPanel->openConversation(FriendInfo{});
    }
    m_friendList->showMessage(ok ? QStringLiteral("已删除该好友")
                                 : QStringLiteral("删除好友失败"),
                              ok);
}

void MainWindow::onChatReceived(const ChatMessageInfo& message)
{
    const qint64 selfId = m_session->userId();
    if (message.fromUserId == selfId) {
        return; // 自己发出的消息由 ChatPanel 处理，不计未读
    }
    // 窗口不在前台时轻量提示（FR-3.3）：任务栏闪烁，不抢焦点
    if (!isActiveWindow()) {
        QApplication::alert(this);
    }
    const bool active = message.fromUserId == m_chatPanel->peerUserId();
    m_conversationModel->upsert(message.fromUserId, message.content, message.ts, !active);
    if (active) {
        m_friendModel->clearUnread(message.fromUserId); // 正打开着会话，气泡由 ChatPanel 上屏
        return;
    }
    // 非当前会话：累计未读（FR-3.3），打开会话时服务端历史会补齐内容
    m_friendModel->addUnread(message.fromUserId);
}

// ---------------------------------------------------------------- 面板交互

void MainWindow::onFriendSelected(qint64 userId)
{
    const int row = m_friendModel->indexOf(userId);
    if (row < 0) {
        return;
    }
    m_friendModel->clearUnread(userId);
    m_conversationModel->clearUnread(userId);
    m_friendList->setActivePeer(userId);
    m_chatPanel->openConversation(m_friendModel->friendAt(row));
    // 用户在「游戏」页时点好友，直接切回会话页
    if (m_rightTabs && m_rightTabs->currentWidget() != m_chatPanel) {
        m_rightTabs->setCurrentWidget(m_chatPanel);
    }
}

void MainWindow::onAddFriendRequested(const QString& username)
{
    m_session->addFriend(username, QString());
}

void MainWindow::onRequestHandled(qint64 fromUserId, bool accept)
{
    m_session->handleFriendRequest(fromUserId, accept);
}

void MainWindow::onRemoveFriendRequested(qint64 userId)
{
    const int row = m_friendModel->indexOf(userId);
    const QString name = row < 0 ? QString::number(userId)
                                 : m_friendModel->friendAt(row).displayName;
    const int answer = showDarkMessageBox(
        this, QMessageBox::Question, QStringLiteral("删除好友"),
        QStringLiteral("确定删除好友「%1」？删除后聊天记录仍保留在服务端。").arg(name),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer == QMessageBox::Yes) {
        m_session->removeFriend(userId);
    }
}
