#pragma once

#include <QFile>
#include <QString>
#include <QtGlobal>

// 深色主题的「唯一颜色来源」（见 docs/TECH_DESIGN.md §7）。
// 约定：
//   * 颜色只在本文件定义，禁止在 UI 代码里散落硬编码颜色；
//   * app.qss 内用 @token 占位，由 styleSheet() 在加载时统一替换 → 便于以后换肤。
namespace Theme {

// 主色 / 强调
inline const QString kAccent        = QStringLiteral("#5B8DEF");
inline const QString kAccentHover   = QStringLiteral("#74A0F5");
inline const QString kAccentPressed = QStringLiteral("#4A79D6");

// 底色层级
inline const QString kBgBase        = QStringLiteral("#1B1D21");
inline const QString kBgPanel       = QStringLiteral("#22252A");
inline const QString kBgCard        = QStringLiteral("#2A2E35");
inline const QString kBgHover       = QStringLiteral("#313640");
inline const QString kBgSelected    = QStringLiteral("#374050");

// 分隔线
inline const QString kBorder        = QStringLiteral("#353A42");

// 文本
inline const QString kTextPrimary   = QStringLiteral("#E6E8EB");
inline const QString kTextSecondary = QStringLiteral("#9AA1AC");
// 主色实心按钮上的文字（需与主色形成高对比，故单独成 token 而非复用 kTextPrimary）
inline const QString kTextOnAccent  = QStringLiteral("#FFFFFF");

// 状态
inline const QString kOnline        = QStringLiteral("#3FB950");
inline const QString kWarning       = QStringLiteral("#D29922");
inline const QString kOffline       = QStringLiteral("#6E7681");
inline const QString kDanger        = QStringLiteral("#E5534B");

// 窗口整体不透明度（1.0 = 完全不透明）。
// 取 0.94：能透出一点桌面层次，又不会让正文可读性下降。
inline constexpr qreal kWindowOpacity = 0.94;

// 载入 :/style/app.qss 并把 @token 替换为上面的颜色值。
// 注意：替换按「长 token 优先」进行，避免 @accent 误伤 @accentHover / @accentPressed。
inline QString styleSheet()
{
    QFile file(QStringLiteral(":/style/app.qss"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString(); // 资源缺失时不阻塞启动（M1 骨架容忍）
    }
    QString qss = QString::fromUtf8(file.readAll());

    qss.replace(QStringLiteral("@accentPressed"), kAccentPressed);
    qss.replace(QStringLiteral("@accentHover"),   kAccentHover);
    qss.replace(QStringLiteral("@accent"),        kAccent);
    qss.replace(QStringLiteral("@bgSelected"),    kBgSelected);
    qss.replace(QStringLiteral("@bgHover"),       kBgHover);
    qss.replace(QStringLiteral("@bgPanel"),       kBgPanel);
    qss.replace(QStringLiteral("@bgCard"),        kBgCard);
    qss.replace(QStringLiteral("@bgBase"),        kBgBase);
    qss.replace(QStringLiteral("@border"),        kBorder);
    qss.replace(QStringLiteral("@textSecondary"), kTextSecondary);
    qss.replace(QStringLiteral("@textPrimary"),   kTextPrimary);
    qss.replace(QStringLiteral("@textOnAccent"),  kTextOnAccent);
    qss.replace(QStringLiteral("@online"),        kOnline);
    qss.replace(QStringLiteral("@warning"),       kWarning);
    qss.replace(QStringLiteral("@offline"),       kOffline);
    qss.replace(QStringLiteral("@danger"),        kDanger);

    return qss;
}

} // namespace Theme
