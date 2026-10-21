#pragma once

#include <QSqlDatabase>
#include <QString>

// SQLite 连接管理：打开数据库、首次运行自动建表、用 PRAGMA user_version 记录 schema 版本
// （见 docs/TECH_DESIGN.md §8）。注意「驱动缺失/打不开」属于硬错误：启动即失败，不静默降级。
class Database {
public:
    Database() = default;
    ~Database();

    // 打开数据库并确保表结构就绪；失败返回 false，err 给出可读原因
    bool open(const QString& path, QString* err = nullptr);
    void close();

    // 供仓储层复用同一连接（QSqlDatabase 为值语义的引用计数句柄）
    QSqlDatabase handle() const { return m_db; }

private:
    bool ensureSchema(QString* err);
    bool createTables(QString* err);

    QSqlDatabase m_db;
    QString m_connectionName;
};
