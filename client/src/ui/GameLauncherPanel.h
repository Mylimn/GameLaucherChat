#pragma once

#include <QHash>
#include <QWidget>

class QLabel;
class QListWidget;
class QPushButton;
struct GameEntry;
class GameLibrary;
class GameProcessManager;

// 游戏库面板（FR-4.1~FR-4.9）：
// 卡片列表展示游戏，支持添加（导入本地 exe）、启动/结束、移除与运行时长。
// 状态展示直接读 GameProcessManager，动作通过 GameLibrary 落盘。
class GameLauncherPanel : public QWidget {
    Q_OBJECT

public:
    GameLauncherPanel(GameLibrary* library, GameProcessManager* processes,
                      QWidget* parent = nullptr);

private slots:
    void rebuildCards();
    void onTick();
    void onAddClicked();

private:
    struct Card {
        QLabel*      status = nullptr;
        QLabel*      detail = nullptr;
        QPushButton* action = nullptr;
        QPushButton* remove = nullptr;
    };

    QWidget* createCard(const GameEntry& entry);
    void     updateCard(const QString& gameId);
    void     startGame(const QString& gameId);
    void     stopGame(const QString& gameId);
    void     removeGame(const QString& gameId);
    void     showMessage(const QString& text, bool ok);

    GameLibrary*        m_library = nullptr;
    GameProcessManager* m_processes = nullptr;

    QLabel*      m_messageLabel = nullptr;
    QLabel*      m_emptyLabel = nullptr;
    QListWidget* m_list = nullptr;
    QHash<QString, Card> m_cards;
};
