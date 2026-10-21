#pragma once

#include <QList>
#include <QObject>
#include <QTcpServer>
#include <memory>

#include "SessionRegistry.h"
#include "store/Database.h"
#include "store/FriendRepository.h"
#include "store/MessageRepository.h"
#include "store/UserRepository.h"

class ClientSession;

// mock 服务端：监听 TCP、为每条连接创建 ClientSession（见 docs/TECH_DESIGN.md §8）。
// 业务在同一线程串行处理，因此在线注册表与仓储都无需加锁。
class MockServer : public QObject {
    Q_OBJECT

public:
    explicit MockServer(QObject* parent = nullptr);
    ~MockServer() override;

    // 打开数据库并开始监听；任一环节失败返回 false 并写 err
    bool start(quint16 port, const QString& dbPath, QString* err = nullptr);

private slots:
    void onNewConnection();
    void onSessionClosed(ClientSession* session);

private:
    QTcpServer m_server;
    Database   m_db;

    std::unique_ptr<UserRepository>    m_users;
    std::unique_ptr<FriendRepository>  m_friends;
    std::unique_ptr<MessageRepository> m_messages;
    SessionRegistry                    m_registry; // 仅内存：userId → 在线连接

    QList<ClientSession*> m_sessions;
};
