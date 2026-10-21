#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

// 游戏条目：games.json 中的一条记录（见 docs/TECH_DESIGN.md §7.1）。
struct GameEntry {
    QString     id;          // UUID（无花括号），导入时生成
    QString     name;        // 显示名
    QString     exePath;     // 可执行文件绝对路径
    QStringList args;        // 启动参数
    QString     cover;       // 封面图片路径，可为空
    qint64      addedAt = 0; // 毫秒时间戳

    // 缺字段的条目视为脏数据：加载时直接跳过，不让它出现在界面上
    bool isValid() const
    {
        return !id.isEmpty() && !name.isEmpty() && !exePath.isEmpty();
    }

    QJsonObject toJson() const
    {
        QJsonObject obj;
        obj[QStringLiteral("id")]      = id;
        obj[QStringLiteral("name")]    = name;
        obj[QStringLiteral("exePath")] = exePath;
        obj[QStringLiteral("args")]    = QJsonArray::fromStringList(args);
        obj[QStringLiteral("cover")]   = cover;
        obj[QStringLiteral("addedAt")] = addedAt;
        return obj;
    }

    static GameEntry fromJson(const QJsonObject& obj)
    {
        GameEntry entry;
        entry.id      = obj.value(QStringLiteral("id")).toString();
        entry.name    = obj.value(QStringLiteral("name")).toString();
        entry.exePath = obj.value(QStringLiteral("exePath")).toString();
        entry.cover   = obj.value(QStringLiteral("cover")).toString();
        entry.addedAt = static_cast<qint64>(obj.value(QStringLiteral("addedAt")).toDouble());

        const QJsonArray args = obj.value(QStringLiteral("args")).toArray();
        for (const QJsonValue& value : args) {
            if (value.isString()) {
                entry.args.append(value.toString());
            }
        }
        return entry;
    }
};
