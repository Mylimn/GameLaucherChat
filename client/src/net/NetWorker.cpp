#include "net/NetWorker.h"

#include <QDateTime>
#include <QDebug>
#include <QTcpSocket>
#include <QTimer>

namespace {
// 心跳每 5s 一次；连续 3 次（即 15s）未收到 HeartbeatResp 判定断线（见 §4.3）
constexpr int kHeartbeatIntervalMs = 5 * 1000;
constexpr int kMaxMissedHeartbeats = 3;
} // namespace

NetWorker::NetWorker(QObject* parent)
    : QObject(parent)
{
}

NetWorker::~NetWorker() = default;

void NetWorker::start()
{
    // 此处运行在网络线程内：创建的子对象自动归属本线程，信号槽为直接调用，无需跨线程
    m_socket = new QTcpSocket(this);
    connect(m_socket, &QTcpSocket::connected,    this, &NetWorker::onSocketConnected);
    connect(m_socket, &QTcpSocket::disconnected, this, &NetWorker::onSocketDisconnected);
    connect(m_socket, &QTcpSocket::readyRead,    this, &NetWorker::onReadyRead);
    connect(m_socket, &QAbstractSocket::errorOccurred, this, &NetWorker::onSocketError);

    m_heartbeatTimer = new QTimer(this);
    m_heartbeatTimer->setInterval(kHeartbeatIntervalMs);
    connect(m_heartbeatTimer, &QTimer::timeout, this, &NetWorker::onHeartbeatTick);
}

void NetWorker::connectToServer(const QString& host, quint16 port)
{
    if (!m_socket) {
        emit errorOccurred(QStringLiteral("网络层尚未就绪"));
        return;
    }

    m_decoder = FrameDecoder(); // 清空上一次的半包缓冲
    m_missedHeartbeats = 0;

    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->abort(); // 重连前先彻底断开，避免残留状态
    }
    m_socket->connectToHost(host, port);
}

void NetWorker::sendFrame(MsgType type, const QJsonObject& payload)
{
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState) {
        qWarning() << "未连接，丢弃待发送帧" << static_cast<int>(type);
        return;
    }
    m_socket->write(encodeFrame(type, payload));
}

void NetWorker::closeConnection()
{
    if (m_heartbeatTimer) {
        m_heartbeatTimer->stop();
    }
    if (m_socket) {
        m_socket->flush();
        m_socket->disconnectFromHost();
    }
}

void NetWorker::onSocketConnected()
{
    m_missedHeartbeats = 0;
    m_heartbeatTimer->start();
    emit connected();
}

void NetWorker::onSocketDisconnected()
{
    m_heartbeatTimer->stop();
    emit disconnected();
}

void NetWorker::onReadyRead()
{
    m_decoder.feed(m_socket->readAll());

    for (;;) {
        MsgType type{};
        QJsonObject payload;
        QString err;
        if (!m_decoder.next(type, payload, &err)) {
            if (err.isEmpty()) {
                break; // 半包
            }
            emit errorOccurred(QStringLiteral("收到坏帧：%1").arg(err));
            continue;
        }
        if (type == MsgType::HeartbeatResp) {
            m_missedHeartbeats = 0; // 收到服务端心跳应答 → 连接健康
        }
        emit frameReceived(type, payload);
    }

    if (m_decoder.fatal()) {
        emit errorOccurred(QStringLiteral("帧结构非法，断开连接"));
        m_socket->abort();
    }
}

void NetWorker::onSocketError(QAbstractSocket::SocketError error)
{
    // RemoteHostClosedError 属正常断开路径，由 disconnected 统一处理，避免重复噪音
    if (error == QAbstractSocket::RemoteHostClosedError) {
        return;
    }
    emit errorOccurred(m_socket->errorString());
}

void NetWorker::onHeartbeatTick()
{
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState) {
        m_heartbeatTimer->stop();
        return;
    }

    QJsonObject payload;
    payload[QStringLiteral("ts")] = QDateTime::currentMSecsSinceEpoch();
    sendFrame(MsgType::HeartbeatReq, payload);

    // 先在收到应答时清零；连续 kMaxMissedHeartbeats 次无应答即判定断线
    if (++m_missedHeartbeats >= kMaxMissedHeartbeats) {
        m_missedHeartbeats = 0;
        emit errorOccurred(QStringLiteral("心跳超时，连接已断开"));
        m_socket->abort();
    }
}
