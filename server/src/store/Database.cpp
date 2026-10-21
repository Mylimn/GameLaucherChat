#include "store/Database.h"

#include <QDir>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace {
// schema 版本：M2 为 1（仅 users 表）；M3 升到 2（新增 friends / friend_requests / messages）
constexpr int kSchemaVersion = 2;
} // namespace

Database::~Database()
{
    close();
}

bool Database::open(const QString& path, QString* err)
{
    // 驱动缺失属于环境问题，明确报错而不是退回内存存储（§8/§10）
    if (!QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE"))) {
        if (err) {
            *err = QStringLiteral("未加载 QSQLITE 驱动；请确认 Qt 的 sqldrivers 插件可用（PATH 需含 Qt bin 目录）");
        }
        return false;
    }

    m_connectionName = QStringLiteral("mock_server_main");
    if (QSqlDatabase::contains(m_connectionName)) {
        m_db = QSqlDatabase::database(m_connectionName);
    } else {
        m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    }
    m_db.setDatabaseName(path);

    // 首次运行 server/data 目录可能不存在 → 先建目录，否则 SQLite 打开会失败
    const QFileInfo fi(path);
    if (!fi.absolutePath().isEmpty()) {
        QDir().mkpath(fi.absolutePath());
    }

    if (!m_db.open()) {
        if (err) {
            *err = QStringLiteral("无法打开数据库 %1：%2").arg(path, m_db.lastError().text());
        }
        return false;
    }

    // 打开外键约束（M3 的 friends/messages 会用到；M2 无影响，但一次性开启省事）
    QSqlQuery pragma(m_db);
    if (!pragma.exec(QStringLiteral("PRAGMA foreign_keys = ON"))) {
        if (err) {
            *err = QStringLiteral("设置 PRAGMA foreign_keys 失败：%1").arg(pragma.lastError().text());
        }
        return false;
    }

    return ensureSchema(err);
}

void Database::close()
{
    if (m_db.isOpen()) {
        m_db.close();
    }
    m_db = QSqlDatabase();
    if (!m_connectionName.isEmpty() && QSqlDatabase::contains(m_connectionName)) {
        QSqlDatabase::removeDatabase(m_connectionName); // 释放连接，避免名字占用
    }
    m_connectionName.clear();
}

bool Database::ensureSchema(QString* err)
{
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral("PRAGMA user_version")) || !q.next()) {
        if (err) {
            *err = QStringLiteral("读取 schema 版本失败：%1").arg(q.lastError().text());
        }
        return false;
    }
    const int version = q.value(0).toInt();
    if (version >= kSchemaVersion) {
        return true; // 已是当前版本
    }

    // 建表语句全部 CREATE TABLE/INDEX IF NOT EXISTS，因此「首建」与「v1→v2 迁移」共用同一条路径：
    // 老库（v1，已有 users）只会补建缺失的新表，不会破坏既有数据（§8）。
    if (!createTables(err)) {
        return false;
    }

    QSqlQuery up(m_db);
    if (!up.exec(QStringLiteral("PRAGMA user_version = %1").arg(kSchemaVersion))) {
        if (err) {
            *err = QStringLiteral("写入 schema 版本失败：%1").arg(up.lastError().text());
        }
        return false;
    }
    return true;
}

bool Database::createTables(QString* err)
{
    // 表结构见 §8：users（账号）、friends（双向关系，每对存两行）、
    // friend_requests（待处理请求）、messages（消息 + 离线投递标记）
    const QStringList statements = {
        QStringLiteral(R"(
            CREATE TABLE IF NOT EXISTS users (
              id            INTEGER PRIMARY KEY AUTOINCREMENT,
              username      TEXT    NOT NULL UNIQUE,
              display_name  TEXT    NOT NULL,
              password_salt TEXT    NOT NULL,
              password_hash TEXT    NOT NULL,
              signature     TEXT    NOT NULL DEFAULT '',
              created_at    INTEGER NOT NULL
            )
        )"),
        QStringLiteral(R"(
            CREATE TABLE IF NOT EXISTS friends (
              user_id    INTEGER NOT NULL,
              friend_id  INTEGER NOT NULL,
              created_at INTEGER NOT NULL,
              PRIMARY KEY (user_id, friend_id)
            )
        )"),
        QStringLiteral(R"(
            CREATE TABLE IF NOT EXISTS friend_requests (
              id         INTEGER PRIMARY KEY AUTOINCREMENT,
              from_user  INTEGER NOT NULL,
              to_user    INTEGER NOT NULL,
              remark     TEXT    NOT NULL DEFAULT '',
              state      INTEGER NOT NULL DEFAULT 0,
              created_at INTEGER NOT NULL
            )
        )"),
        QStringLiteral(R"(
            CREATE TABLE IF NOT EXISTS messages (
              id        INTEGER PRIMARY KEY AUTOINCREMENT,
              from_user INTEGER NOT NULL,
              to_user   INTEGER NOT NULL,
              content   TEXT    NOT NULL,
              ts        INTEGER NOT NULL,
              delivered INTEGER NOT NULL DEFAULT 0
            )
        )"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_msg_pair ON messages(from_user, to_user, id)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_msg_undelivered ON messages(to_user, delivered)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_freq_pending ON friend_requests(to_user, state)"),
    };

    for (const QString& sql : statements) {
        QSqlQuery q(m_db);
        if (!q.exec(sql)) {
            if (err) {
                *err = QStringLiteral("建表失败：%1").arg(q.lastError().text());
            }
            return false;
        }
    }
    return true;
}
