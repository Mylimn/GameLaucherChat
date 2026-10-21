#include <QApplication>
#include <QDebug>
#include <QEventLoop>
#include <QLibraryInfo>
#include <QStyleFactory>
#include <QTranslator>

#include "core/Session.h"
#include "net/NetController.h"
#include "protocol/ProtocolVersion.h"
#include "ui/LoginDialog.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("GameLauncherChat"));

    // 用 Fusion 打底，避免残留原生风格的灰色控件；具体配色统一交给 app.qss（见 TECH_DESIGN §7）
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    app.setStyleSheet(Theme::styleSheet());

    // 文件选择框已改为非原生（见 ui/WindowChrome），其中的「打开/取消/文件名」等文案来自 Qt，
    // 不装中文包会在一水中文界面里露出英文；装不上就退回英文，不影响功能。
    // Qt 6.x 的翻译目录里是合集包 qt_zh_CN.qm，旧版本是 qtbase_zh_CN.qm，两个都试。
    QTranslator qtTranslator;
    const QString translationsDir = QLibraryInfo::path(QLibraryInfo::TranslationsPath);
    if (qtTranslator.load(QStringLiteral("qt_zh_CN"), translationsDir)
        || qtTranslator.load(QStringLiteral("qtbase_zh_CN"), translationsDir)) {
        app.installTranslator(&qtTranslator);
    }

    qInfo() << "[M2] 客户端启动，协议版本" << Protocol::kVersion;

    NetController net;
    Session session(&net);

    // 服务端地址可配置（登录页可改，记住上次输入）；这里先用保存值建立连接
    QString host;
    quint16 port = 0;
    if (!Session::parseServerAddress(Session::savedServerAddress(), &host, &port)) {
        host = QStringLiteral("127.0.0.1");
        port = 9527;
    }
    session.connectToServer(host, port);

    // 登录页 ↔ 主窗口：登出后回到登录页，直接关窗则退出
    bool relogin = false;
    while (true) {
        LoginDialog login(&session);
        if (login.exec() != QDialog::Accepted) {
            break;
        }

        MainWindow window(&session);
        relogin = false;
        QEventLoop loop;
        QObject::connect(&window, &MainWindow::closed, &loop, &QEventLoop::quit);
        // 登出后必须退出本窗口的事件循环，否则界面停在已登录状态、只有关窗才回登录页
        QObject::connect(&window, &MainWindow::loggedOut, &loop, &QEventLoop::quit);
        QObject::connect(&window, &MainWindow::loggedOut, &window, [&relogin]() { relogin = true; });
        window.show();
        loop.exec();

        if (!relogin) {
            break;
        }
    }

    // 退出前主动登出（§10）；shutdown 会等这一帧尽量写出后再关闭 socket
    if (session.state() == Session::State::LoggedIn) {
        session.logout();
    }
    net.shutdown();
    return 0;
}
