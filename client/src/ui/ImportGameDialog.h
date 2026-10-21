#pragma once

#include <QDialog>
#include <QStringList>

class QLabel;
class QLineEdit;

// 导入游戏（FR-4.8）：exe 已由文件选择框确定，这里只收集名称/参数/封面并做输入校验。
class ImportGameDialog : public QDialog {
    Q_OBJECT

public:
    explicit ImportGameDialog(const QString& exePath, QWidget* parent = nullptr);

    QString     gameName() const;
    QStringList args() const;
    QString     coverPath() const;

private slots:
    void onBrowseCover();
    void onAccept();

private:
    QString     m_exePath;
    QLineEdit*  m_nameEdit = nullptr;
    QLineEdit*  m_argsEdit = nullptr;
    QLineEdit*  m_coverEdit = nullptr;
    QLabel*     m_errorLabel = nullptr;
};
