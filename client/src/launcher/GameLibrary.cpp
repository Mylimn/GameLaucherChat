#include "launcher/GameLibrary.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>
#include <QStandardPaths>

namespace {
// 资源里的内置模板：首次运行复制到用户目录后不再使用
const char* kTemplateResource = ":/games.json";
} // namespace

GameLibrary::GameLibrary(qint64 userId, QObject* parent)
    : QObject(parent)
    , m_userId(userId)
{
}

QString GameLibrary::storagePath(qint64 userId)
{
    // 每个账号一份：同一台机器上不同账号互不影响
    // Windows 下为 %APPDATA%\GameLauncherChat\games-<userId>.json
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return dir + QStringLiteral("/games-%1.json").arg(userId);
}

bool GameLibrary::load(QString* err)
{
    m_entries.clear();
    m_loadError.clear();

    const QString path = storagePath(m_userId);
    QDir().mkpath(QFileInfo(path).absolutePath());

    if (!QFileInfo::exists(path)) {
        QString copyErr;
        if (!copyTemplate(&copyErr)) {
            m_loadError = copyErr;
            if (err) {
                *err = copyErr;
            }
            return false;
        }
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_loadError = QStringLiteral("无法读取游戏库文件：%1").arg(file.errorString());
        if (err) {
            *err = m_loadError;
        }
        return false;
    }
    const QByteArray raw = file.readAll();
    file.close();

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        // 不覆盖用户文件：语法错误可能只是手改错了一处，直接重写会把内容全丢掉
        m_loadError = QStringLiteral("游戏库文件格式错误：%1（行 %2）")
                          .arg(parseError.errorString())
                          .arg(parseError.offset);
        if (err) {
            *err = m_loadError;
        }
        return false;
    }
    if (!doc.isArray()) {
        m_loadError = QStringLiteral("游戏库文件格式错误：根节点应为数组");
        if (err) {
            *err = m_loadError;
        }
        return false;
    }

    for (const QJsonValue& value : doc.array()) {
        if (!value.isObject()) {
            continue;
        }
        const GameEntry entry = GameEntry::fromJson(value.toObject());
        if (!entry.isValid()) {
            qWarning() << "游戏库：跳过字段不全的条目" << value.toObject();
            continue;
        }
        m_entries.append(entry);
    }
    return true;
}

bool GameLibrary::copyTemplate(QString* err) const
{
    QByteArray content = QByteArrayLiteral("[]");

    QFile res(QString::fromLatin1(kTemplateResource));
    if (res.open(QIODevice::ReadOnly)) {
        content = res.readAll();
        res.close();
    } else {
        qWarning() << "游戏库：内置模板缺失，退化为空列表";
    }

    // 原子写入：避免复制过程中崩溃留下半个文件
    QSaveFile out(storagePath(m_userId));
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (err) {
            *err = QStringLiteral("无法创建游戏库文件：%1").arg(out.errorString());
        }
        return false;
    }
    out.write(content);
    if (!out.commit()) {
        if (err) {
            *err = QStringLiteral("写入游戏库文件失败：%1").arg(out.errorString());
        }
        return false;
    }
    return true;
}

bool GameLibrary::save(QString* err) const
{
    QJsonArray array;
    for (const GameEntry& entry : m_entries) {
        array.append(entry.toJson());
    }

    QSaveFile file(storagePath(m_userId));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (err) {
            *err = QStringLiteral("无法写入游戏库文件：%1").arg(file.errorString());
        }
        return false;
    }
    file.write(QJsonDocument(array).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (err) {
            *err = QStringLiteral("保存游戏库失败：%1").arg(file.errorString());
        }
        return false;
    }
    return true;
}

int GameLibrary::indexOf(const QString& id) const
{
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).id == id) {
            return i;
        }
    }
    return -1;
}

QString GameLibrary::canonical(const QString& path)
{
    const QFileInfo info(path);
    const QString real = info.canonicalFilePath();
    return real.isEmpty() ? QDir::cleanPath(info.absoluteFilePath()) : real;
}

bool GameLibrary::containsExe(const QString& exePath, QString* existingName) const
{
    const QString key = canonical(exePath);
    for (const GameEntry& entry : m_entries) {
        if (QString::compare(canonical(entry.exePath), key, Qt::CaseInsensitive) == 0) {
            if (existingName) {
                *existingName = entry.name;
            }
            return true;
        }
    }
    return false;
}

bool GameLibrary::validateExe(const QString& path, QString* err)
{
    auto fail = [err](const QString& message) {
        if (err) {
            *err = message;
        }
        return false;
    };

    if (path.trimmed().isEmpty()) {
        return fail(QStringLiteral("请选择游戏可执行文件"));
    }
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile()) {
        return fail(QStringLiteral("文件不存在：%1").arg(QDir::toNativeSeparators(path)));
    }
    if (info.suffix().compare(QStringLiteral("exe"), Qt::CaseInsensitive) != 0) {
        return fail(QStringLiteral("仅支持 .exe 程序"));
    }
    return true;
}

bool GameLibrary::add(const GameEntry& entry, QString* err)
{
    QString duplicateName;
    if (containsExe(entry.exePath, &duplicateName)) {
        if (err) {
            *err = QStringLiteral("该程序已在游戏库中（%1）").arg(duplicateName);
        }
        return false;
    }

    m_entries.append(entry);
    if (!save(err)) {
        m_entries.removeLast(); // 落盘失败则回滚内存，避免与文件不一致
        return false;
    }
    emit changed();
    return true;
}

bool GameLibrary::remove(const QString& id, QString* err)
{
    const int index = indexOf(id);
    if (index < 0) {
        if (err) {
            *err = QStringLiteral("游戏条目不存在");
        }
        return false;
    }

    const GameEntry removed = m_entries.takeAt(index);
    if (!save(err)) {
        m_entries.insert(index, removed);
        return false;
    }
    emit changed();
    return true;
}
