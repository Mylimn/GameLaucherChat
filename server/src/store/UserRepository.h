#pragma once

#include <QSqlDatabase>
#include <QString>
#include <optional>

// users 表的一行（见 docs/TECH_DESIGN.md §8）
struct UserRecord {
    qint64  id = 0;
    QString username;
    QString displayName;
    QString passwordSalt;
    QString passwordHash;
    QString signature;
};

// 账号仓储：注册（唯一性）与按用户名查询（§8）。只做数据访问，不做格式校验（校验在 ClientSession）。
class UserRepository {
public:
    explicit UserRepository(QSqlDatabase db) : m_db(std::move(db)) {}

    // 按用户名精确查找；不存在返回 nullopt
    std::optional<UserRecord> findByUsername(const QString& username) const;

    // 按 userId 查找；不存在返回 nullopt（M3 好友列表/消息路由需要）
    std::optional<UserRecord> findById(qint64 userId) const;

    // 插入新账号：成功返回新 userId，失败返回 -1 并在 err 写入原因
    // （唯一性冲突由上层先查后插保证；此处仍可能因约束失败而返回错误）
    qint64 create(const QString& username, const QString& displayName,
                  const QString& passwordSalt, const QString& passwordHash,
                  QString* err) const;

private:
    QSqlDatabase m_db;
};
