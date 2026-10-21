#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QStringList>

#include "MockServer.h"
#include "protocol/ProtocolVersion.h"

namespace {

// 默认端口（见 TECH_DESIGN §3，可用 --port 覆盖）
constexpr quint16 kDefaultPort = 9527;

// 数据文件路径解析：
//   开发期 从仓库根运行（AGENTS.md §5）→ ./server/data/mock.db
//   开发期 从 build/<Config> 运行（Qt Creator 默认）→ 仓库根/server/data/mock.db
//   部署期（发布包、exe 同级）→ exe 同级 data/mock.db
// 注意不能简单地「向上两级找 server/」：发布包放在仓库 dist/ 下时也会命中，
// 结果发布版把数据写进开发目录。只有 exe 的父目录名为 build 才认定为开发构建。
QString resolveDbPath()
{
    const QString cwd = QDir::currentPath();
    if (QDir(cwd + QStringLiteral("/server")).exists()) {
        return QDir(cwd).filePath(QStringLiteral("server/data/mock.db"));
    }

    const QString appDir = QCoreApplication::applicationDirPath();
    if (QFileInfo(appDir).dir().dirName() == QStringLiteral("build")) {
        return QDir(appDir).absoluteFilePath(QStringLiteral("../../server/data/mock.db"));
    }

    return QDir(appDir).absoluteFilePath(QStringLiteral("data/mock.db"));
}

quint16 parsePort(const QStringList& args)
{
    const int idx = args.indexOf(QStringLiteral("--port"));
    if (idx >= 0 && idx + 1 < args.size()) {
        bool ok = false;
        const uint value = args.at(idx + 1).toUInt(&ok);
        if (ok && value > 0 && value <= 65535) {
            return static_cast<quint16>(value);
        }
        qWarning() << "--port 参数非法，回退默认端口" << kDefaultPort;
    }
    return kDefaultPort;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("MockServer"));

    qInfo() << "mock_server 启动，协议版本" << Protocol::kVersion;

    // 数据库打不开 → 启动即失败，不静默降级为内存存储（TECH_DESIGN §10）
    MockServer server;
    QString err;
    if (!server.start(parsePort(QCoreApplication::arguments()), resolveDbPath(), &err)) {
        qCritical().noquote() << QStringLiteral("[启动失败] %1").arg(err);
        return 1;
    }

    return app.exec();
}
