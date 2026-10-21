#pragma once

#include <QList>
#include <QObject>
#include <QString>

#include "launcher/GameEntry.h"

// 游戏库：每个登录账号一份独立的 games-<userId>.json（见 docs/TECH_DESIGN.md §7.1）。
// 首次登录从资源模板复制；读取失败时只报错、不清空也不覆盖用户文件。
class GameLibrary : public QObject {
    Q_OBJECT

public:
    explicit GameLibrary(qint64 userId, QObject* parent = nullptr);

    // 读取游戏库；失败时 entries() 为空，原因见 loadError()
    bool load(QString* err = nullptr);

    const QList<GameEntry>& entries() const { return m_entries; }
    int indexOf(const QString& id) const;
    QString loadError() const { return m_loadError; }

    bool add(const GameEntry& entry, QString* err = nullptr);
    bool remove(const QString& id, QString* err = nullptr);

    // 同一 exe 去重（大小写/斜杠差异视为同一路径）
    bool containsExe(const QString& exePath, QString* existingName = nullptr) const;

    static QString storagePath(qint64 userId);
    static bool validateExe(const QString& path, QString* err = nullptr);

signals:
    void changed();

private:
    bool save(QString* err) const;
    bool copyTemplate(QString* err) const;
    static QString canonical(const QString& path);

    qint64           m_userId = 0;
    QList<GameEntry> m_entries;
    QString          m_loadError;
};
