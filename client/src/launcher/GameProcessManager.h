#pragma once

#include <QHash>
#include <QObject>
#include <QString>

#include "launcher/GameEntry.h"

class QProcess;
class QTimer;

// 进程管理：每个游戏一个 QProcess（见 docs/TECH_DESIGN.md §7.1）。
// 启动/跟踪全程走信号，不做阻塞等待；只有退出清理时短暂等待，防止留下孤儿进程。
class GameProcessManager : public QObject {
    Q_OBJECT

public:
    enum class State {
        NotRunning, // 从未启动 / 启动失败
        Starting,   // 已调用 start()，等待 started 信号
        Running,    // 进程在跑
        Exited      // 已正常/异常退出
    };

    explicit GameProcessManager(QObject* parent = nullptr);
    ~GameProcessManager() override;

    bool start(const GameEntry& entry, QString* err = nullptr);
    void stop(const QString& gameId);
    void killAll();

    State state(const QString& gameId) const;
    bool isRunning(const QString& gameId) const; // Starting 或 Running
    QString lastError(const QString& gameId) const;
    qint64 elapsedMs(const QString& gameId) const;

signals:
    void stateChanged(const QString& gameId);
    void tick(); // 1s 心跳，供 UI 刷新运行时长

private slots:
    void onTick();

private:
    struct Proc {
        QProcess* process = nullptr;
        State     state = State::NotRunning;
        QString   error;
        qint64    startedAt = 0;
        int       exitCode = 0;
    };

    Proc* find(const QString& gameId) const;

    QHash<QString, Proc*> m_procs;
    QTimer*               m_tickTimer = nullptr;
};
