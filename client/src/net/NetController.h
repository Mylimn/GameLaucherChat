#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QThread>

#include "protocol/MessageType.h"

class NetWorker;

// 网络门面（主线程侧）：把出站请求投递到网络线程，把入站信号转发给主线程消费者。
// 跨线程只走信号槽（QueuedConnection），不共享可变状态（见 docs/TECH_DESIGN.md §6）。
class NetController : public QObject {
    Q_OBJECT

public:
    explicit NetController(QObject* parent = nullptr);
    ~NetController() override;

    // 以下三个方法在主线程调用，内部转为 QueuedConnection 投递到网络线程
    void connectToServer(const QString& host, quint16 port);
    void send(MsgType type, const QJsonObject& payload);
    void disconnectFromServer();

    // 停止网络线程并回收 NetWorker（应在退出前调用，通常由析构代劳）
    void shutdown();

signals:
    void connected();
    void disconnected();
    void frameReceived(MsgType type, const QJsonObject& payload);
    void errorOccurred(const QString& message);

    // 内部桥接信号（仅本类使用）：由 QueuedConnection 送达 NetWorker 槽
    void connectRequested(const QString& host, quint16 port);
    void sendRequested(MsgType type, const QJsonObject& payload);
    void closeRequested();

private:
    QThread    m_netThread;
    NetWorker* m_worker = nullptr;
};
