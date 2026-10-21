#include "SessionRegistry.h"

void SessionRegistry::add(qint64 userId, ClientSession* session)
{
    if (userId == 0 || !session) {
        return;
    }
    m_sessions.insert(userId, session);
}

void SessionRegistry::remove(qint64 userId)
{
    m_sessions.remove(userId);
}

ClientSession* SessionRegistry::find(qint64 userId) const
{
    return m_sessions.value(userId, nullptr);
}
