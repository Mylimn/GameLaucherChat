#include "ui/WindowChrome.h"

#include <QFileDialog>
#include <QWidget>

#ifdef Q_OS_WIN
// 只用到 DWM 的一个导出函数，动态取地址即可，不必新增链接依赖（AGENTS §6 依赖约束）
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

namespace {

using DwmSetWindowAttributeFn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);

// DWMWA_USE_IMMERSIVE_DARK_MODE：Win10 2004+ / Win11 用 20，Win10 1809~1903 用 19。
// 两个都试一遍，失败无副作用；更老的系统不支持该属性，标题栏维持系统默认。
constexpr DWORD kAttrDarkMode = 20;
constexpr DWORD kAttrDarkModeLegacy = 19;

} // namespace
#endif

void applyDarkTitleBar(QWidget* widget)
{
#ifdef Q_OS_WIN
    if (!widget) {
        return;
    }
    HMODULE dwm = ::LoadLibraryW(L"dwmapi.dll");
    if (!dwm) {
        return;
    }
    auto setAttribute = reinterpret_cast<DwmSetWindowAttributeFn>(
        ::GetProcAddress(dwm, "DwmSetWindowAttribute"));
    if (setAttribute) {
        const HWND hwnd = reinterpret_cast<HWND>(widget->winId());
        const BOOL enabled = TRUE;
        if (FAILED(setAttribute(hwnd, kAttrDarkMode, &enabled, sizeof(enabled)))) {
            setAttribute(hwnd, kAttrDarkModeLegacy, &enabled, sizeof(enabled));
        }
    }
    ::FreeLibrary(dwm);
#else
    Q_UNUSED(widget);
#endif
}

int showDarkMessageBox(QWidget* parent, QMessageBox::Icon icon, const QString& title,
                       const QString& text, QMessageBox::StandardButtons buttons,
                       QMessageBox::StandardButton defaultButton)
{
    QMessageBox box(icon, title, text, buttons, parent);
    box.setDefaultButton(defaultButton);
    applyDarkTitleBar(&box);
    return box.exec();
}

QString getOpenFilePath(QWidget* parent, const QString& title, const QString& nameFilter,
                        const QString& startDir)
{
    QFileDialog dialog(parent, title, startDir, nameFilter);
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    dialog.setFileMode(QFileDialog::ExistingFile);
    // 系统原生文件框无法套用 app.qss，在深色界面里会整块闪白
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);
    applyDarkTitleBar(&dialog);

    if (dialog.exec() != QDialog::Accepted) {
        return QString();
    }
    const QStringList selected = dialog.selectedFiles();
    return selected.isEmpty() ? QString() : selected.constFirst();
}
