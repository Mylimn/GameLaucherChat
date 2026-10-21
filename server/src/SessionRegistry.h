#pragma once

#include <QHash>
#include <QtGlobal>

class ClientSession;

// 在线会话注册表：userId → 当前连接（仅内存，socket 无法持久化，见 docs/TECH_DESIGN.md §8）。
// 服务端为单线程事件循环，所有访问都在同一线程，因此不需要加锁。
class SessionRegistry {
public:
    void add(qint64 userId, ClientSession* session);
    void remove(qint64 userId);

    // 不存在返回 nullptr
    ClientSession* find(qint64 userId) const;
    bool           isOnline(qint64 userId) const { return m_sessions.contains(userId); }

private:
    QHash<qint64, ClientSession*> m_sessions;
};
