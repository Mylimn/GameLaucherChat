#pragma once

#include <QMessageBox>
#include <QString>

class QWidget;

// 系统原生窗口/对话框的深色适配：
// Qt 自绘的控件由 app.qss 换肤，但 Windows 标题栏、系统文件选择框不受 QSS 控制，
// 在深色界面里会露出浅色，这里统一处理（见 docs/TECH_DESIGN.md §7）。

// 把窗口的原生标题栏切成深色。非 Windows 平台为空实现。
// 调用时会创建原生窗口句柄，窗口 show 前后均可调用。
void applyDarkTitleBar(QWidget* widget);

// 深色消息框：等价于 QMessageBox 的静态便捷函数，但标题栏也是深色。
int showDarkMessageBox(QWidget* parent,
                       QMessageBox::Icon icon,
                       const QString& title,
                       const QString& text,
                       QMessageBox::StandardButtons buttons = QMessageBox::Ok,
                       QMessageBox::StandardButton defaultButton = QMessageBox::NoButton);

// 深色「选择文件」对话框：强制 Qt 自绘（不用系统原生框），以便套用 app.qss。
// 用户取消时返回空字符串。
QString getOpenFilePath(QWidget* parent,
                        const QString& title,
                        const QString& nameFilter,
                        const QString& startDir = QString());
