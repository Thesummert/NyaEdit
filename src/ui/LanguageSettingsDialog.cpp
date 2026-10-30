#include "LanguageSettingsDialog.h"
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace nya {
LanguageSettingsDialog::LanguageSettingsDialog(const LanguageSettings &settings, QWidget *parent) : QDialog(parent)
{
    setObjectName("languageSettingsDialog"); setWindowTitle(QStringLiteral("语言支持")); resize(610, 340);
    auto *layout = new QVBoxLayout(this); layout->setContentsMargins(24, 24, 24, 20); layout->setSpacing(16);
    auto *description = new QLabel(QStringLiteral("使用系统 Tree-sitter 提供语法着色。需要已安装 CLI，并在其配置中添加语言解析器和高亮规则；这项功能不提供 LSP 补全或跳转。"));
    description->setWordWrap(true); layout->addWidget(description);
    m_enabled = new QCheckBox(QStringLiteral("启用 Tree-sitter 语法着色"));
    m_enabled->setObjectName("treeSitterEnabled"); m_enabled->setChecked(settings.treeSitterEnabled); layout->addWidget(m_enabled);
    auto *form = new QFormLayout; form->setSpacing(12);
    auto addPath = [this, form](const QString &label, const QString &value, const QString &name, bool config) {
        auto *row = new QWidget; auto *line = new QHBoxLayout(row); line->setContentsMargins(0, 0, 0, 0);
        auto *input = new QLineEdit(value); input->setObjectName(name); input->setAccessibleName(label);
        auto *browse = new QPushButton(QStringLiteral("浏览…"));
        line->addWidget(input, 1); line->addWidget(browse); form->addRow(label, row);
        connect(browse, &QPushButton::clicked, this, [this, input, config] {
            const auto path = QFileDialog::getOpenFileName(this, config ? QStringLiteral("选择 Tree-sitter 配置") : QStringLiteral("选择 Tree-sitter 可执行文件"), input->text());
            if (!path.isEmpty()) input->setText(path);
        });
        return input;
    };
    m_executable = addPath(QStringLiteral("可执行文件"), settings.executable, "treeSitterExecutable", false);
    m_executable->setPlaceholderText("tree-sitter");
    m_config = addPath(QStringLiteral("配置文件"), settings.configPath, "treeSitterConfigPath", true);
    m_config->setPlaceholderText(QStringLiteral("留空使用 Tree-sitter 系统默认配置")); layout->addLayout(form);
    auto *status = new QLabel; status->setObjectName("treeSitterDetection"); status->setWordWrap(true);
    const auto updateStatus = [this, status] {
        const auto path = TreeSitter::findExecutable(this->settings().executable);
        status->setText(path.isEmpty() ? QStringLiteral("未检测到可执行文件；启用后仍会保留基础着色。") : QStringLiteral("已找到：%1").arg(path));
    };
    connect(m_executable, &QLineEdit::textChanged, this, updateStatus); updateStatus(); layout->addWidget(status);
    auto *hint = new QLabel(QStringLiteral("确认后应用到所有文档并保存。未安装工具、缺少解析器或调用失败时自动使用基础着色。"));
    hint->setWordWrap(true); layout->addWidget(hint);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("确定")); buttons->button(QDialogButtonBox::Ok)->setObjectName("primary");
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject); layout->addWidget(buttons);
}

LanguageSettings LanguageSettingsDialog::settings() const
{
    const auto executable = m_executable->text().trimmed();
    return {m_enabled->isChecked(), executable.isEmpty() ? QStringLiteral("tree-sitter") : executable, m_config->text().trimmed()};
}
}
