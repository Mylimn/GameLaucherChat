#include "store/UserRepository.h"

#include <QDateTime>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

std::optional<UserRecord> UserRepository::findByUsername(const QString& username) const
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT id, username, display_name, password_salt, password_hash, signature "
        "FROM users WHERE username = ?"));
    q.addBindValue(username);
    if (!q.exec()) {
        // 查询失败（如架构未就绪）视为「找不到账号」，由上层给出登录失败文案；错误另记日志
        qWarning("UserRepository::findByUsername 查询失败：%s", qPrintable(q.lastError().text()));
        return std::nullopt;
    }
    if (!q.next()) {
        return std::nullopt;
    }

    UserRecord r;
    r.id           = q.value(0).toLongLong();
    r.username     = q.value(1).toString();
    r.displayName  = q.value(2).toString();
    r.passwordSalt = q.value(3).toString();
    r.passwordHash = q.value(4).toString();
    r.signature    = q.value(5).toString();
    return r;
}

std::optional<UserRecord> UserRepository::findById(qint64 userId) const
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT id, username, display_name, password_salt, password_hash, signature "
        "FROM users WHERE id = ?"));
    q.addBindValue(userId);
    if (!q.exec()) {
        qWarning("UserRepository::findById 查询失败：%s", qPrintable(q.lastError().text()));
        return std::nullopt;
    }
    if (!q.next()) {
        return std::nullopt;
    }

    UserRecord r;
    r.id           = q.value(0).toLongLong();
    r.username     = q.value(1).toString();
    r.displayName  = q.value(2).toString();
    r.passwordSalt = q.value(3).toString();
    r.passwordHash = q.value(4).toString();
    r.signature    = q.value(5).toString();
    return r;
}

qint64 UserRepository::create(const QString& username, const QString& displayName,
                              const QString& passwordSalt, const QString& passwordHash,
                              QString* err) const
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO users(username, display_name, password_salt, password_hash, signature, created_at) "
        "VALUES(?, ?, ?, ?, '', ?)"));
    q.addBindValue(username);
    q.addBindValue(displayName);
    q.addBindValue(passwordSalt);
    q.addBindValue(passwordHash);
    q.addBindValue(QDateTime::currentMSecsSinceEpoch());

    if (!q.exec()) {
        if (err) {
            *err = q.lastError().text();
        }
        return -1;
    }
    return q.lastInsertId().toLongLong();
}
