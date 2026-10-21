#include "net/NetController.h"

#include <QMetaObject>

#include "net/NetWorker.h"

NetController::NetController(QObject* parent)
    : QObject(parent)
{
    // 跨线程队列连接需要在运行时识别 MsgType（编译期注册见 MessageType.h 的 Q_DECLARE_METATYPE）
    qRegisterMetaType<MsgType>("MsgType");

    // worker 不能带父对象，否则 moveToThread 无效
    m_worker = new NetWorker;
    m_worker->moveToThread(&m_netThread);

    // 线程启动后用 started 信号触达 worker，让 socket/timer 在网络线程内创建（§6）
    connect(&m_netThread, &QThread::started, m_worker, &NetWorker::start);

    // 入站：worker（网络线程）→ 本对象（主线程），自动排队
    connect(m_worker, &NetWorker::connected,     this, &NetController::connected);
    connect(m_worker, &NetWorker::disconnected,  this, &NetController::disconnected);
    connect(m_worker, &NetWorker::frameReceived, this, &NetController::frameReceived);
    connect(m_worker, &NetWorker::errorOccurred, this, &NetController::errorOccurred);

    // 出站：本对象（主线程）→ worker（网络线程），显式排队
    connect(this, &NetController::connectRequested, m_worker, &NetWorker::connectToServer, Qt::QueuedConnection);
    connect(this, &NetController::sendRequested,    m_worker, &NetWorker::sendFrame,       Qt::QueuedConnection);
    connect(this, &NetController::closeRequested,   m_worker, &NetWorker::closeConnection, Qt::QueuedConnection);

    m_netThread.start();
}

NetController::~NetController()
{
    shutdown();
}

void NetController::connectToServer(const QString& host, quint16 port)
{
    emit connectRequested(host, port);
}

void NetController::send(MsgType type, const QJsonObject& payload)
{
    emit sendRequested(type, payload);
}

void NetController::disconnectFromServer()
{
    emit closeRequested();
}

void NetController::shutdown()
{
    NetWorker* worker = m_worker;
    if (!worker) {
        return;
    }
    if (m_netThread.isRunning()) {
        // 在 worker 所属线程内收尾并销毁它（连同 socket/timer），避免跨线程销毁 socket 的告警
        QMetaObject::invokeMethod(
            worker,
            [worker]() {
                worker->closeConnection(); // 先把已排队的帧尽量写出
                delete worker;
            },
            Qt::BlockingQueuedConnection);
        m_netThread.quit();
        m_netThread.wait();
    } else {
        delete worker;
    }
    m_worker = nullptr;
}
