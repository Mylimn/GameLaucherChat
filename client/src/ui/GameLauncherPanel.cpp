#include "ui/GameLauncherPanel.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QPixmap>
#include <QStyle>
#include <QUuid>
#include <QVBoxLayout>

#include "launcher/GameLibrary.h"
#include "launcher/GameProcessManager.h"
#include "ui/ImportGameDialog.h"
#include "ui/WindowChrome.h"

namespace {

constexpr int kRowHeight   = 64;  // 卡片行高
constexpr int kCoverSize   = 40;
constexpr int kPathElidePx = 340; // 路径省略宽度（悬停可从 tooltip 看全路径）

QString formatDuration(qint64 ms)
{
    const qint64 totalSeconds = ms / 1000;
    const qint64 hours   = totalSeconds / 3600;
    const qint64 minutes = (totalSeconds % 3600) / 60;
    const qint64 seconds = totalSeconds % 60;
    if (hours > 0) {
        return QStringLiteral("%1:%2:%3")
            .arg(hours)
            .arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(seconds, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2")
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'));
}

// 动态属性变更后必须重新 polish，否则 QSS 里的 [state=...] 分支不生效
void setStateKey(QLabel* label, const QString& key)
{
    if (label->property("state").toString() == key) {
        return;
    }
    label->setProperty("state", key);
    label->style()->unpolish(label);
    label->style()->polish(label);
}

} // namespace

GameLauncherPanel::GameLauncherPanel(GameLibrary* library, GameProcessManager* processes,
                                     QWidget* parent)
    : QWidget(parent)
    , m_library(library)
    , m_processes(processes)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(12);

    // ---- 标题行：标题 + 添加游戏 ----
    auto* title = new QLabel(QStringLiteral("游戏库"), this);
    title->setObjectName(QStringLiteral("panelTitle"));

    auto* addButton = new QPushButton(QStringLiteral("＋ 添加游戏"), this);
    addButton->setObjectName(QStringLiteral("secondaryButton"));

    auto* headerRow = new QHBoxLayout;
    headerRow->setSpacing(8);
    headerRow->addWidget(title);
    headerRow->addStretch(1);
    headerRow->addWidget(addButton);

    m_messageLabel = new QLabel(this);
    m_messageLabel->setObjectName(QStringLiteral("messageLabel"));
    m_messageLabel->setWordWrap(true);

    // ---- 空态 + 卡片列表 ----
    m_emptyLabel = new QLabel(
        QStringLiteral("还没有游戏\n点右上角「添加游戏」导入本地 exe"), this);
    m_emptyLabel->setObjectName(QStringLiteral("emptyState"));
    m_emptyLabel->setAlignment(Qt::AlignCenter);

    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("gameList"));
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_list->setSelectionMode(QAbstractItemView::NoSelection);

    layout->addLayout(headerRow);
    layout->addWidget(m_messageLabel);
    layout->addWidget(m_emptyLabel);
    layout->addWidget(m_list, 1);

    connect(addButton, &QPushButton::clicked, this, &GameLauncherPanel::onAddClicked);
    // 列表结构变化（增删）才重建；排队执行，避免在按钮自己的 clicked() 里把按钮删掉
    connect(m_library, &GameLibrary::changed, this, &GameLauncherPanel::rebuildCards,
            Qt::QueuedConnection);
    // 进程状态变化就地刷新该行，不重建控件
    connect(m_processes, &GameProcessManager::stateChanged,
            this, &GameLauncherPanel::updateCard);
    connect(m_processes, &GameProcessManager::tick, this, &GameLauncherPanel::onTick);

    rebuildCards();

    if (!m_library->loadError().isEmpty()) {
        showMessage(QStringLiteral("游戏库读取失败：%1").arg(m_library->loadError()), false);
    }
}

QWidget* GameLauncherPanel::createCard(const GameEntry& entry)
{
    auto* row = new QWidget(this);
    row->setObjectName(QStringLiteral("listItem"));

    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(12, 8, 12, 8);
    layout->setSpacing(12);

    // ---- 封面：有可用图片就显示，否则用首字母占位 ----
    auto* cover = new QLabel(row);
    cover->setFixedSize(kCoverSize, kCoverSize);
    cover->setAlignment(Qt::AlignCenter);
    QPixmap pixmap;
    if (!entry.cover.isEmpty() && pixmap.load(entry.cover)) {
        cover->setObjectName(QStringLiteral("gameCover"));
        cover->setPixmap(pixmap.scaled(kCoverSize, kCoverSize, Qt::KeepAspectRatioByExpanding,
                                       Qt::SmoothTransformation)
                             .copy(0, 0, kCoverSize, kCoverSize));
    } else {
        cover->setObjectName(QStringLiteral("coverPlaceholder"));
        cover->setText(entry.name.left(1).toUpper());
    }

    // ---- 名称 + exe 路径 ----
    auto* name = new QLabel(entry.name, row);
    name->setObjectName(QStringLiteral("itemTitle"));

    const QString nativePath = QDir::toNativeSeparators(entry.exePath);
    auto* path = new QLabel(row);
    path->setObjectName(QStringLiteral("itemSubtitle"));
    path->setText(QFontMetrics(path->font()).elidedText(nativePath, Qt::ElideMiddle, kPathElidePx));
    path->setToolTip(nativePath);

    auto* textColumn = new QVBoxLayout;
    textColumn->setSpacing(2);
    textColumn->addWidget(name);
    textColumn->addWidget(path);

    // ---- 状态 + 详情（时长 / 失败原因） ----
    auto* status = new QLabel(row);
    status->setObjectName(QStringLiteral("gameStatus"));
    auto* detail = new QLabel(row);
    detail->setObjectName(QStringLiteral("itemSubtitle"));
    detail->setAlignment(Qt::AlignRight);

    auto* statusColumn = new QVBoxLayout;
    statusColumn->setSpacing(2);
    statusColumn->addWidget(status);
    statusColumn->addWidget(detail);

    // ---- 操作按钮 ----
    auto* action = new QPushButton(row);
    auto* remove = new QPushButton(QStringLiteral("移除"), row);
    remove->setObjectName(QStringLiteral("miniButton"));

    auto* buttonRow = new QHBoxLayout;
    buttonRow->setSpacing(8);
    buttonRow->addWidget(action);
    buttonRow->addWidget(remove);

    layout->addWidget(cover);
    layout->addLayout(textColumn, 1);
    layout->addLayout(statusColumn);
    layout->addLayout(buttonRow);

    const QString gameId = entry.id;
    connect(action, &QPushButton::clicked, this, [this, gameId]() {
        if (m_processes->isRunning(gameId)) {
            stopGame(gameId);
        } else {
            startGame(gameId);
        }
    });
    connect(remove, &QPushButton::clicked, this, [this, gameId]() { removeGame(gameId); });

    m_cards.insert(gameId, Card{status, detail, action, remove});
    return row;
}

void GameLauncherPanel::rebuildCards()
{
    m_cards.clear();
    m_list->clear();

    const auto& entries = m_library->entries();
    for (const GameEntry& entry : entries) {
        auto* item = new QListWidgetItem(m_list);
        item->setSizeHint(QSize(0, kRowHeight));
        m_list->setItemWidget(item, createCard(entry));
        updateCard(entry.id);
    }

    const bool empty = entries.isEmpty();
    m_emptyLabel->setVisible(empty);
    m_list->setVisible(!empty);
}

void GameLauncherPanel::updateCard(const QString& gameId)
{
    const int index = m_library->indexOf(gameId);
    const auto cardIt = m_cards.find(gameId);
    if (index < 0 || cardIt == m_cards.end()) {
        return;
    }

    const GameEntry& entry = m_library->entries().at(index);
    Card& card = *cardIt;

    QString statusText;
    QString statusKey;
    QString detailText;

    const bool pathValid = GameLibrary::validateExe(entry.exePath, nullptr);
    const GameProcessManager::State state = m_processes->state(gameId);
    const QString error = m_processes->lastError(gameId);

    if (!pathValid && (state == GameProcessManager::State::NotRunning
                       || state == GameProcessManager::State::Exited)) {
        // FR-4.6：外部删掉了文件，明确标出来并禁用启动
        statusText = QStringLiteral("● 路径失效");
        statusKey  = QStringLiteral("error");
        detailText = QStringLiteral("exe 已被移动或删除");
    } else {
        switch (state) {
        case GameProcessManager::State::NotRunning:
            statusText = QStringLiteral("● 未运行");
            statusKey  = QStringLiteral("idle");
            break;
        case GameProcessManager::State::Starting:
            statusText = QStringLiteral("● 启动中…");
            statusKey  = QStringLiteral("starting");
            detailText = QStringLiteral("已运行 %1").arg(formatDuration(m_processes->elapsedMs(gameId)));
            break;
        case GameProcessManager::State::Running:
            statusText = QStringLiteral("● 运行中");
            statusKey  = QStringLiteral("running");
            detailText = QStringLiteral("已运行 %1").arg(formatDuration(m_processes->elapsedMs(gameId)));
            break;
        case GameProcessManager::State::Exited:
            statusText = QStringLiteral("● 已退出");
            statusKey  = error.isEmpty() ? QStringLiteral("idle") : QStringLiteral("error");
            detailText = error.isEmpty() ? QStringLiteral("进程已结束") : error;
            break;
        }
    }

    card.status->setText(statusText);
    setStateKey(card.status, statusKey);
    card.detail->setText(detailText);
    card.detail->setToolTip(detailText);

    const bool running = m_processes->isRunning(gameId);
    card.action->setText(running ? QStringLiteral("结束") : QStringLiteral("启动"));
    card.action->setEnabled(running || pathValid);
    card.action->setObjectName(running ? QStringLiteral("miniButton")
                                       : QStringLiteral("miniButtonAccent"));
    card.action->style()->unpolish(card.action);
    card.action->style()->polish(card.action);

    card.remove->setEnabled(!running);
}

void GameLauncherPanel::onTick()
{
    // 只刷新运行中条目的时长，避免每秒重建整个列表
    for (const GameEntry& entry : m_library->entries()) {
        if (!m_processes->isRunning(entry.id)) {
            continue;
        }
        const auto it = m_cards.find(entry.id);
        if (it == m_cards.end()) {
            continue;
        }
        const QString text =
            QStringLiteral("已运行 %1").arg(formatDuration(m_processes->elapsedMs(entry.id)));
        it->detail->setText(text);
        it->detail->setToolTip(text);
    }
}

void GameLauncherPanel::onAddClicked()
{
    const QString exePath = getOpenFilePath(this, QStringLiteral("选择游戏程序"),
                                            QStringLiteral("可执行文件 (*.exe)"));
    if (exePath.isEmpty()) {
        return;
    }

    QString error;
    if (!GameLibrary::validateExe(exePath, &error)) {
        showMessage(error, false);
        return;
    }
    QString existingName;
    if (m_library->containsExe(exePath, &existingName)) {
        showMessage(QStringLiteral("该程序已在游戏库中（%1）").arg(existingName), false);
        return;
    }

    ImportGameDialog dialog(exePath, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    GameEntry entry;
    entry.id      = QUuid::createUuid().toString(QUuid::WithoutBraces);
    entry.name    = dialog.gameName();
    entry.exePath = exePath;
    entry.args    = dialog.args();
    entry.cover   = dialog.coverPath();
    entry.addedAt = QDateTime::currentMSecsSinceEpoch();

    if (!m_library->add(entry, &error)) {
        showMessage(error, false);
        return;
    }
    showMessage(QStringLiteral("已添加「%1」").arg(entry.name), true);
}

void GameLauncherPanel::startGame(const QString& gameId)
{
    const int index = m_library->indexOf(gameId);
    if (index < 0) {
        return;
    }
    QString error;
    if (!m_processes->start(m_library->entries().at(index), &error)) {
        showMessage(error, false);
    }
}

void GameLauncherPanel::stopGame(const QString& gameId)
{
    m_processes->stop(gameId);
}

void GameLauncherPanel::removeGame(const QString& gameId)
{
    if (m_processes->isRunning(gameId)) {
        showDarkMessageBox(this, QMessageBox::Information, QStringLiteral("移除游戏"),
                           QStringLiteral("请先结束该游戏的进程再移除。"));
        return;
    }

    const int index = m_library->indexOf(gameId);
    if (index < 0) {
        return;
    }
    const QString name = m_library->entries().at(index).name;
    const int answer = showDarkMessageBox(
        this, QMessageBox::Question, QStringLiteral("移除游戏"),
        QStringLiteral("从游戏库移除「%1」？只会删除这条记录，不会删除磁盘上的文件。").arg(name),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        return;
    }

    QString error;
    if (!m_library->remove(gameId, &error)) {
        showMessage(error, false);
    }
}

void GameLauncherPanel::showMessage(const QString& text, bool ok)
{
    m_messageLabel->setText(text);
    m_messageLabel->setProperty("tone", ok ? "success" : "error");
    m_messageLabel->style()->unpolish(m_messageLabel);
    m_messageLabel->style()->polish(m_messageLabel);
}
