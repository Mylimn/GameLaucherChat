#include "launcher/GameProcessManager.h"

#include <QDateTime>
#include <QFileInfo>
#include <QProcess>
#include <QTimer>
#include <utility>

#include "launcher/GameLibrary.h"

GameProcessManager::GameProcessManager(QObject* parent)
    : QObject(parent)
{
    m_tickTimer = new QTimer(this);
    m_tickTimer->setInterval(1000);
    connect(m_tickTimer, &QTimer::timeout, this, &GameProcessManager::onTick);
}

GameProcessManager::~GameProcessManager()
{
    killAll();

    for (Proc* proc : std::as_const(m_procs)) {
        delete proc;
    }
    m_procs.clear();
}

GameProcessManager::Proc* GameProcessManager::find(const QString& gameId) const
{
    return m_procs.value(gameId);
}

bool GameProcessManager::start(const GameEntry& entry, QString* err)
{
    if (isRunning(entry.id)) {
        if (err) {
            *err = QStringLiteral("该游戏已在运行");
        }
        return false;
    }

    QString exeError;
    if (!GameLibrary::validateExe(entry.exePath, &exeError)) {
        if (err) {
            *err = exeError;
        }
        return false;
    }

    Proc* proc = find(entry.id);
    if (!proc) {
        proc = new Proc;
        proc->process = new QProcess(this);

        const QString gameId = entry.id;
        connect(proc->process, &QProcess::started, this, [this, gameId]() {
            Proc* current = find(gameId);
            if (!current || current->state != State::Starting) {
                return;
            }
            current->state = State::Running;
            emit stateChanged(gameId);
        });
        connect(proc->process, &QProcess::finished, this,
                [this, gameId](int exitCode, QProcess::ExitStatus status) {
                    Proc* current = find(gameId);
                    if (!current) {
                        return;
                    }
                    current->state = State::Exited;
                    current->exitCode = exitCode;
                    current->error = status == QProcess::CrashExit
                                         ? QStringLiteral("进程异常退出（退出码 %1）").arg(exitCode)
                                         : QString();
                    emit stateChanged(gameId);
                });
        connect(proc->process, &QProcess::errorOccurred, this,
                [this, gameId](QProcess::ProcessError error) {
                    Proc* current = find(gameId);
                    if (!current) {
                        return;
                    }
                    if (error == QProcess::FailedToStart) {
                        // 起不来时不会有 finished，状态必须在这里回退
                        current->state = State::NotRunning;
                        current->error =
                            QStringLiteral("启动失败：%1").arg(current->process->errorString());
                        emit stateChanged(gameId);
                    } else if (error == QProcess::Crashed) {
                        current->error = QStringLiteral("进程崩溃");
                    }
                });

        m_procs.insert(entry.id, proc);
    }

    proc->state = State::Starting;
    proc->error.clear();
    proc->exitCode = 0;
    proc->startedAt = QDateTime::currentMSecsSinceEpoch();

    proc->process->setProgram(entry.exePath);
    proc->process->setArguments(entry.args);
    // 工作目录必须设为 exe 所在目录：很多游戏按相对路径找自己的资源
    proc->process->setWorkingDirectory(QFileInfo(entry.exePath).absolutePath());
    proc->process->start();

    if (!m_tickTimer->isActive()) {
        m_tickTimer->start();
    }
    emit stateChanged(entry.id);
    return true;
}

void GameProcessManager::stop(const QString& gameId)
{
    Proc* proc = find(gameId);
    if (!proc || !proc->process || proc->process->state() == QProcess::NotRunning) {
        return;
    }
    // kill() 之后会收到 finished，状态在那里统一更新
    proc->process->kill();
}

void GameProcessManager::killAll()
{
    for (Proc* proc : std::as_const(m_procs)) {
        if (proc->process && proc->process->state() != QProcess::NotRunning) {
            proc->process->kill();
            proc->process->waitForFinished(2000); // 只在退出清理时阻塞，避免留下孤儿进程
        }
    }
}

GameProcessManager::State GameProcessManager::state(const QString& gameId) const
{
    const Proc* proc = find(gameId);
    return proc ? proc->state : State::NotRunning;
}

bool GameProcessManager::isRunning(const QString& gameId) const
{
    const State current = state(gameId);
    return current == State::Starting || current == State::Running;
}

QString GameProcessManager::lastError(const QString& gameId) const
{
    const Proc* proc = find(gameId);
    return proc ? proc->error : QString();
}

qint64 GameProcessManager::elapsedMs(const QString& gameId) const
{
    const Proc* proc = find(gameId);
    if (!proc || proc->startedAt <= 0) {
        return 0;
    }
    if (proc->state != State::Starting && proc->state != State::Running) {
        return 0;
    }
    return QDateTime::currentMSecsSinceEpoch() - proc->startedAt;
}

void GameProcessManager::onTick()
{
    bool anyRunning = false;
    for (const Proc* proc : std::as_const(m_procs)) {
        if (proc->state == State::Starting || proc->state == State::Running) {
            anyRunning = true;
            break;
        }
    }
    if (!anyRunning) {
        m_tickTimer->stop(); // 没有运行中的进程就不必每秒唤醒 UI
        return;
    }
    emit tick();
}
