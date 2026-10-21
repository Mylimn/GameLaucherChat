#include "MockServer.h"

#include <QDebug>
#include <QHostAddress>
#include <QTcpSocket>

#include "ClientSession.h"

MockServer::MockServer(QObject* parent)
    : QObject(parent)
{
}

MockServer::~MockServer()
{
    // 会话随本对象析构（parent 为 this）；此处仅确保 socket 先关闭，避免半途事件回调
    for (ClientSession* s : std::as_const(m_sessions)) {
        s->disconnect(this);
    }
}

bool MockServer::start(quint16 port, const QString& dbPath, QString* err)
{
    if (!m_db.open(dbPath, err)) {
        return false;
    }
    m_users    = std::make_unique<UserRepository>(m_db.handle());
    m_friends  = std::make_unique<FriendRepository>(m_db.handle());
    m_messages = std::make_unique<MessageRepository>(m_db.handle());

    if (!m_server.listen(QHostAddress::Any, port)) {
        if (err) {
            *err = QStringLiteral("监听端口 %1 失败：%2").arg(port).arg(m_server.errorString());
        }
        return false;
    }

    connect(&m_server, &QTcpServer::newConnection, this, &MockServer::onNewConnection);
    qInfo().noquote() << QStringLiteral("mock_server 已启动：监听端口 %1，数据库 %2")
                             .arg(m_server.serverPort())
                             .arg(dbPath);
    return true;
}

void MockServer::onNewConnection()
{
    while (m_server.hasPendingConnections()) {
        QTcpSocket* socket = m_server.nextPendingConnection();

        // 每条连接一个 ClientSession，parent=this 保证随服务端一起销毁
        auto* session = new ClientSession(socket, *m_users, *m_friends, *m_messages, m_registry, this);
        connect(session, &ClientSession::closed, this, &MockServer::onSessionClosed);
        m_sessions.append(session);
    }
}

void MockServer::onSessionClosed(ClientSession* session)
{
    m_sessions.removeAll(session);
    session->deleteLater(); // 在事件循环空闲时销毁，避免在信号回调中直接 delete
}
