#include "ui/LoginDialog.h"

#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStyle>
#include <QTabWidget>
#include <QVBoxLayout>

#include "ui/Theme.h"
#include "ui/WindowChrome.h"

namespace {

// 连接状态文案（小圆点色由 app.qss 依据动态属性 state 着色）
QString stateText(Session::State state)
{
    switch (state) {
    case Session::State::Disconnected: return QStringLiteral("未连接");
    case Session::State::Connecting:   return QStringLiteral("连接中…");
    case Session::State::Connected:    return QStringLiteral("已连接");
    case Session::State::LoggedIn:     return QStringLiteral("已登录");
    }
    return QString();
}

QString stateKey(Session::State state)
{
    switch (state) {
    case Session::State::Disconnected: return QStringLiteral("offline");
    case Session::State::Connecting:   return QStringLiteral("connecting");
    case Session::State::Connected:    return QStringLiteral("online");
    case Session::State::LoggedIn:     return QStringLiteral("online");
    }
    return QStringLiteral("offline");
}

// 动态属性变更后需重新 polish，QSS 选择器才会重新求值
void repolish(QWidget* widget)
{
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
}

} // namespace

LoginDialog::LoginDialog(Session* session, QWidget* parent)
    : QDialog(parent)
    , m_session(session)
{
    buildUi();

    connect(m_session, &Session::loginSucceeded,    this, &LoginDialog::onLoginSucceeded);
    connect(m_session, &Session::loginFailed,       this, &LoginDialog::onLoginFailed);
    connect(m_session, &Session::registerSucceeded, this, &LoginDialog::onRegisterSucceeded);
    connect(m_session, &Session::registerFailed,    this, &LoginDialog::onRegisterFailed);
    connect(m_session, &Session::stateChanged,      this, &LoginDialog::onStateChanged);
    connect(m_session, &Session::errorOccurred,     this, &LoginDialog::onErrorOccurred);
}

void LoginDialog::buildUi()
{
    setWindowTitle(QStringLiteral("登录"));
    setObjectName(QStringLiteral("loginDialog"));
    setFixedWidth(400);
    setWindowOpacity(Theme::kWindowOpacity); // 与主窗口一致的轻微透明
    applyDarkTitleBar(this);                 // 系统标题栏跟随深色主题

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(24, 24, 24, 24);
    root->setSpacing(16);

    auto* title = new QLabel(QStringLiteral("游戏启动器 + 聊天"), this);
    title->setObjectName(QStringLiteral("titleLabel"));
    root->addWidget(title);

    auto* subtitle = new QLabel(QStringLiteral("登录后可管理游戏并与好友聊天"), this);
    subtitle->setObjectName(QStringLiteral("subtitleLabel"));
    root->addWidget(subtitle);

    m_tabs = new QTabWidget(this);
    m_tabs->addTab(buildLoginPage(), QStringLiteral("登录"));
    m_tabs->addTab(buildRegisterPage(), QStringLiteral("注册"));
    root->addWidget(m_tabs);

    // 服务端地址：默认带出上次保存值，改了会在下次登录/注册时生效并记住
    m_serverEdit = new QLineEdit(this);
    m_serverEdit->setText(QStringLiteral("%1:%2")
                              .arg(m_session->serverHost())
                              .arg(m_session->serverPort()));
    m_serverEdit->setPlaceholderText(QStringLiteral("127.0.0.1:9527"));
    m_serverEdit->setToolTip(QStringLiteral("服务端地址，格式为「主机:端口」"));
    auto* serverForm = new QFormLayout();
    serverForm->setContentsMargins(0, 0, 0, 0);
    serverForm->addRow(QStringLiteral("服务器"), m_serverEdit);
    root->addLayout(serverForm);

    m_messageLabel = new QLabel(this);
    m_messageLabel->setObjectName(QStringLiteral("messageLabel"));
    m_messageLabel->setWordWrap(true);
    m_messageLabel->setProperty("tone", QStringLiteral("neutral"));
    root->addWidget(m_messageLabel);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName(QStringLiteral("statusLabel"));
    onStateChanged(m_session->state());
    root->addWidget(m_statusLabel);

    // 自动重登失败会退回登录页；原因直接从 Session 取，不让用户莫名其妙被踢回
    if (!m_session->lastError().isEmpty()) {
        showMessage(m_session->lastError(), Tone::Error);
    }
}

QWidget* LoginDialog::buildLoginPage()
{
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);
    form->setContentsMargins(0, 16, 0, 0);
    form->setSpacing(12);

    m_loginUsername = new QLineEdit(page);
    m_loginUsername->setPlaceholderText(QStringLiteral("用户名"));
    m_loginPassword = new QLineEdit(page);
    m_loginPassword->setPlaceholderText(QStringLiteral("密码"));
    m_loginPassword->setEchoMode(QLineEdit::Password);

    form->addRow(QStringLiteral("用户名"), m_loginUsername);
    form->addRow(QStringLiteral("密码"), m_loginPassword);

    m_loginButton = new QPushButton(QStringLiteral("登录"), page);
    m_loginButton->setObjectName(QStringLiteral("primaryButton"));
    m_loginButton->setDefault(true);
    connect(m_loginButton, &QPushButton::clicked, this, &LoginDialog::onLoginClicked);
    connect(m_loginPassword, &QLineEdit::returnPressed, this, &LoginDialog::onLoginClicked);
    form->addRow(m_loginButton); // 跨两列的整行按钮

    return page;
}

QWidget* LoginDialog::buildRegisterPage()
{
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);
    form->setContentsMargins(0, 16, 0, 0);
    form->setSpacing(12);

    m_regUsername = new QLineEdit(page);
    m_regUsername->setPlaceholderText(QStringLiteral("3-20 位字母/数字/下划线"));
    m_regDisplayName = new QLineEdit(page);
    m_regDisplayName->setPlaceholderText(QStringLiteral("1-20 个字符"));
    m_regPassword = new QLineEdit(page);
    m_regPassword->setPlaceholderText(QStringLiteral("6-32 个字符"));
    m_regPassword->setEchoMode(QLineEdit::Password);

    form->addRow(QStringLiteral("用户名"), m_regUsername);
    form->addRow(QStringLiteral("昵称"), m_regDisplayName);
    form->addRow(QStringLiteral("密码"), m_regPassword);

    m_registerButton = new QPushButton(QStringLiteral("注册"), page);
    m_registerButton->setObjectName(QStringLiteral("primaryButton"));
    connect(m_registerButton, &QPushButton::clicked, this, &LoginDialog::onRegisterClicked);
    connect(m_regPassword, &QLineEdit::returnPressed, this, &LoginDialog::onRegisterClicked);
    form->addRow(m_registerButton);

    return page;
}

void LoginDialog::updateButtons()
{
    m_loginButton->setEnabled(!m_busy);
    m_registerButton->setEnabled(!m_busy);
    m_loginUsername->setEnabled(!m_busy);
    m_loginPassword->setEnabled(!m_busy);
    m_regUsername->setEnabled(!m_busy);
    m_regDisplayName->setEnabled(!m_busy);
    m_regPassword->setEnabled(!m_busy);
    m_serverEdit->setEnabled(!m_busy);
}

void LoginDialog::setBusy(bool busy)
{
    m_busy = busy;
    updateButtons();
}

void LoginDialog::showMessage(const QString& text, Tone tone)
{
    m_messageLabel->setText(text);
    m_messageLabel->setProperty("tone",
                                tone == Tone::Error   ? QStringLiteral("error")
                                : tone == Tone::Success ? QStringLiteral("success")
                                                        : QStringLiteral("neutral"));
    repolish(m_messageLabel);
}

bool LoginDialog::applyServerAddress()
{
    QString host;
    quint16 port = 0;
    if (!Session::parseServerAddress(m_serverEdit->text(), &host, &port)) {
        showMessage(QStringLiteral("服务器地址格式应为「主机:端口」，例如 192.168.1.10:9527"),
                    Tone::Error);
        return false;
    }

    if (host == m_session->serverHost() && port == m_session->serverPort()) {
        return true; // 没改，沿用现有连接
    }

    // 换服务器必须重连：旧连接指向的是另一台主机，继续用会登到错误的地方
    Session::saveServerAddress(QStringLiteral("%1:%2").arg(host).arg(port));
    m_session->connectToServer(host, port);
    return true;
}

void LoginDialog::onLoginClicked()
{
    const QString username = m_loginUsername->text().trimmed();
    const QString password = m_loginPassword->text();
    if (username.isEmpty() || password.isEmpty()) {
        showMessage(QStringLiteral("请输入用户名和密码"), Tone::Error);
        return;
    }
    if (!applyServerAddress()) {
        return;
    }
    setBusy(true);
    showMessage(QStringLiteral("正在登录…"), Tone::Neutral);
    m_session->login(username, password);
}

void LoginDialog::onRegisterClicked()
{
    const QString username = m_regUsername->text().trimmed();
    const QString displayName = m_regDisplayName->text().trimmed();
    const QString password = m_regPassword->text();
    if (username.isEmpty() || displayName.isEmpty() || password.isEmpty()) {
        showMessage(QStringLiteral("请完整填写注册信息"), Tone::Error);
        return;
    }
    if (!applyServerAddress()) {
        return;
    }
    setBusy(true);
    showMessage(QStringLiteral("正在注册…"), Tone::Neutral);
    m_session->registerAccount(username, password, displayName);
}

void LoginDialog::onLoginSucceeded()
{
    setBusy(false);
    accept(); // 交由 main.cpp 切到主窗口
}

void LoginDialog::onLoginFailed(const QString& message)
{
    setBusy(false);
    showMessage(message, Tone::Error);
}

void LoginDialog::onRegisterSucceeded(const QString& message)
{
    setBusy(false);
    showMessage(message, Tone::Success);

    // 注册成功不自动登录：切回登录页并预填用户名（§4.2）
    m_loginUsername->setText(m_regUsername->text().trimmed());
    m_loginPassword->clear();
    m_tabs->setCurrentIndex(0);
    m_loginPassword->setFocus();
}

void LoginDialog::onRegisterFailed(const QString& message)
{
    setBusy(false);
    showMessage(message, Tone::Error);
}

void LoginDialog::onStateChanged(Session::State state)
{
    m_statusLabel->setText(QStringLiteral("● %1").arg(stateText(state)));
    m_statusLabel->setProperty("state", stateKey(state));
    repolish(m_statusLabel);
}

void LoginDialog::onErrorOccurred(const QString& message)
{
    // 正在等待登录/注册结果时，连接类错误也要解除忙碌态，避免界面卡在"正在…"
    setBusy(false);
    showMessage(message, Tone::Error);
}
