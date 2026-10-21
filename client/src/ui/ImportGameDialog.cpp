#include "ui/ImportGameDialog.h"

#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QProcess>
#include <QPushButton>
#include <QVBoxLayout>

#include "ui/WindowChrome.h"

ImportGameDialog::ImportGameDialog(const QString& exePath, QWidget* parent)
    : QDialog(parent)
    , m_exePath(exePath)
{
    setWindowTitle(QStringLiteral("添加游戏"));
    setModal(true);
    applyDarkTitleBar(this); // 与主窗口一致的深色标题栏
    setMinimumWidth(480);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(12);

    auto* title = new QLabel(QStringLiteral("添加游戏"), this);
    title->setObjectName(QStringLiteral("titleLabel"));

    auto* pathLabel = new QLabel(QDir::toNativeSeparators(exePath), this);
    pathLabel->setObjectName(QStringLiteral("subtitleLabel"));
    pathLabel->setWordWrap(true);
    pathLabel->setToolTip(QDir::toNativeSeparators(exePath));

    m_nameEdit = new QLineEdit(QFileInfo(exePath).completeBaseName(), this);
    m_nameEdit->setMaxLength(40);

    m_argsEdit = new QLineEdit(this);
    m_argsEdit->setPlaceholderText(QStringLiteral("可选，例如 -windowed --lang=zh"));

    m_coverEdit = new QLineEdit(this);
    m_coverEdit->setPlaceholderText(QStringLiteral("可选，选择一张本地图片"));
    m_coverEdit->setReadOnly(true);
    auto* browseButton = new QPushButton(QStringLiteral("浏览…"), this);
    browseButton->setObjectName(QStringLiteral("miniButton"));

    auto* coverRow = new QHBoxLayout;
    coverRow->setSpacing(8);
    coverRow->addWidget(m_coverEdit, 1);
    coverRow->addWidget(browseButton);

    auto* form = new QFormLayout;
    form->setSpacing(12);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->addRow(QStringLiteral("名称"), m_nameEdit);
    form->addRow(QStringLiteral("启动参数"), m_argsEdit);
    form->addRow(QStringLiteral("封面"), coverRow);

    m_errorLabel = new QLabel(this);
    m_errorLabel->setObjectName(QStringLiteral("messageLabel"));
    m_errorLabel->setProperty("tone", "error");
    m_errorLabel->setWordWrap(true);
    m_errorLabel->hide();

    auto* cancelButton = new QPushButton(QStringLiteral("取消"), this);
    cancelButton->setObjectName(QStringLiteral("secondaryButton"));
    auto* okButton = new QPushButton(QStringLiteral("添加"), this);
    okButton->setObjectName(QStringLiteral("primaryButton"));

    auto* buttonRow = new QHBoxLayout;
    buttonRow->setSpacing(8);
    buttonRow->addStretch(1);
    buttonRow->addWidget(cancelButton);
    buttonRow->addWidget(okButton);

    layout->addWidget(title);
    layout->addWidget(pathLabel);
    layout->addLayout(form);
    layout->addWidget(m_errorLabel);
    layout->addLayout(buttonRow);

    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(okButton, &QPushButton::clicked, this, &ImportGameDialog::onAccept);
    connect(browseButton, &QPushButton::clicked, this, &ImportGameDialog::onBrowseCover);

    m_nameEdit->setFocus();
    m_nameEdit->selectAll();
}

QString ImportGameDialog::gameName() const
{
    return m_nameEdit->text().trimmed();
}

QStringList ImportGameDialog::args() const
{
    // 用 Qt 自带的切分规则解析引号，避免与 QProcess 参数传递方式不一致
    return QProcess::splitCommand(m_argsEdit->text());
}

QString ImportGameDialog::coverPath() const
{
    return m_coverEdit->text().trimmed();
}

void ImportGameDialog::onBrowseCover()
{
    const QString path = getOpenFilePath(this, QStringLiteral("选择封面图片"),
                                         QStringLiteral("图片 (*.png *.jpg *.jpeg *.bmp)"));
    if (!path.isEmpty()) {
        m_coverEdit->setText(QDir::toNativeSeparators(path));
    }
}

void ImportGameDialog::onAccept()
{
    if (gameName().isEmpty()) {
        m_errorLabel->setText(QStringLiteral("请输入游戏名称"));
        m_errorLabel->show();
        m_nameEdit->setFocus();
        return;
    }
    accept();
}
