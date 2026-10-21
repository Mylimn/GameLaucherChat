#pragma once

#include <QDialog>

#include "core/Session.h"

class QLabel;
class QLineEdit;
class QPushButton;
class QTabWidget;
class QWidget;

// 登录 / 注册对话框：一个页签切换两种意图（见 docs/TECH_DESIGN.md §7 视觉规范）。
// 登录成功后 accept()；注册成功不自动登录，切回登录页由用户再登录。
class LoginDialog : public QDialog {
    Q_OBJECT

public:
    explicit LoginDialog(Session* session, QWidget* parent = nullptr);

private slots:
    void onLoginClicked();
    void onRegisterClicked();

    void onLoginSucceeded();
    void onLoginFailed(const QString& message);
    void onRegisterSucceeded(const QString& message);
    void onRegisterFailed(const QString& message);
    void onStateChanged(Session::State state);
    void onErrorOccurred(const QString& message);

private:
    enum class Tone { Neutral, Success, Error };

    void buildUi();
    QWidget* buildLoginPage();
    QWidget* buildRegisterPage();
    void updateButtons();
    void setBusy(bool busy);
    void showMessage(const QString& text, Tone tone);
    // 把「服务器」输入框落到 Session（地址变了才重连），并记住本次输入；格式非法返回 false
    bool applyServerAddress();

    Session* m_session = nullptr;
    bool m_busy = false;

    QTabWidget* m_tabs = nullptr;

    QLineEdit* m_serverEdit = nullptr;

    QLineEdit* m_loginUsername = nullptr;
    QLineEdit* m_loginPassword = nullptr;

    QLineEdit* m_regUsername = nullptr;
    QLineEdit* m_regDisplayName = nullptr;
    QLineEdit* m_regPassword = nullptr;

    QPushButton* m_loginButton = nullptr;
    QPushButton* m_registerButton = nullptr;

    QLabel* m_messageLabel = nullptr;
    QLabel* m_statusLabel = nullptr;
};
