#pragma once

#include <QAbstractSocket>
#include <QJsonObject>
#include <QObject>
#include <QString>

#include "protocol/Frame.h"
#include "protocol/MessageType.h"

class QTcpSocket;
class QTimer;

// 网络工作对象：跑在独立网络线程内，负责 Socket 生命周期、心跳、拆帧与组帧发送
// （见 docs/TECH_DESIGN.md §6/§7）。所有方法都应在网络线程内执行。
class NetWorker : public QObject {
    Q_OBJECT

public:
    explicit NetWorker(QObject* parent = nullptr);
    ~NetWorker() override;

public slots:
    // 在本对象所属线程（网络线程）内创建 socket 与心跳定时器；
    // 由 QThread::started 触发（见 §6：定时器必须在其所属线程创建，否则不触发）
    void start();

    void connectToServer(const QString& host, quint16 port);
    void sendFrame(MsgType type, const QJsonObject& payload);
    void closeConnection();

signals:
    void connected();
    void disconnected();
    void frameReceived(MsgType type, const QJsonObject& payload);
    void errorOccurred(const QString& message);

private slots:
    void onSocketConnected();
    void onSocketDisconnected();
    void onReadyRead();
    void onSocketError(QAbstractSocket::SocketError error);
    void onHeartbeatTick();

private:
    QTcpSocket*  m_socket = nullptr;
    QTimer*      m_heartbeatTimer = nullptr;
    FrameDecoder m_decoder;
    int          m_missedHeartbeats = 0;
};
