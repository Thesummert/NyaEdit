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
    setObjectName("languageSettingsDialog"); setWindowTitle(QStringLiteral("语言支持")); resize(650, 590);
    auto *layout = new QVBoxLayout(this); layout->setContentsMargins(24, 24, 24, 20); layout->setSpacing(16);
    auto *description = new QLabel(QStringLiteral("Tree-sitter 提供语法着色，LSP 语言服务器提供代码补全和定义跳转。请先安装所需工具，两项功能可以独立开启。"));
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
        const auto executable = m_executable->text().trimmed();
        const auto path = TreeSitter::findExecutable(executable.isEmpty() ? QStringLiteral("tree-sitter") : executable);
        status->setText(path.isEmpty() ? QStringLiteral("未检测到可执行文件；启用后仍会保留基础着色。") : QStringLiteral("已找到：%1").arg(path));
    };
    connect(m_executable, &QLineEdit::textChanged, this, updateStatus); updateStatus(); layout->addWidget(status);
    m_lspEnabled = new QCheckBox(QStringLiteral("启用 LSP 补全与定义跳转"));
    m_lspEnabled->setObjectName("lspEnabled"); m_lspEnabled->setChecked(settings.lspEnabled); layout->addWidget(m_lspEnabled);
    auto *lspForm = new QFormLayout; lspForm->setSpacing(12);
    auto *serverRow = new QWidget; auto *serverLayout = new QHBoxLayout(serverRow); serverLayout->setContentsMargins(0, 0, 0, 0);
    m_lspExecutable = new QLineEdit(settings.lspExecutable); m_lspExecutable->setObjectName("lspExecutable");
    m_lspExecutable->setPlaceholderText(QStringLiteral("clangd 或服务器完整路径"));
    auto *browseServer = new QPushButton(QStringLiteral("浏览…"));
    serverLayout->addWidget(m_lspExecutable, 1); serverLayout->addWidget(browseServer);
    connect(browseServer, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getOpenFileName(this, QStringLiteral("选择 LSP 语言服务器"), m_lspExecutable->text());
        if (!path.isEmpty()) m_lspExecutable->setText(path);
    });
    lspForm->addRow(QStringLiteral("LSP 服务器"), serverRow);
    m_lspArguments = new QLineEdit(settings.lspArguments); m_lspArguments->setObjectName("lspArguments");
    m_lspArguments->setPlaceholderText(QStringLiteral("例如 --background-index；含空格的参数用双引号包围"));
    lspForm->addRow(QStringLiteral("启动参数"), m_lspArguments);
    m_lspExtensions = new QLineEdit(settings.lspExtensions); m_lspExtensions->setObjectName("lspExtensions");
    m_lspExtensions->setPlaceholderText(QStringLiteral("例如 c,cpp,h,hpp 或 py；逗号分隔，不含点号"));
    lspForm->addRow(QStringLiteral("文件后缀"), m_lspExtensions); layout->addLayout(lspForm);
    auto *lspHint = new QLabel(QStringLiteral("默认使用 clangd 支持 C/C++；其他语言需更换服务器和后缀（如 pylsp / py）。打开匹配的已保存文件后启动服务器。补全：Alt+/；定义跳转：F12 或 Ctrl/⌘ 点击。"));
    lspHint->setWordWrap(true); layout->addWidget(lspHint);
    auto *hint = new QLabel(QStringLiteral("确认后保存并应用到所有文档，重新连接 LSP；取消不会更改当前设置。工具不可用时保留基础编辑功能，错误可在状态栏查看。"));
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
    LanguageSettings result{m_enabled->isChecked(), executable.isEmpty() ? QStringLiteral("tree-sitter") : executable, m_config->text().trimmed()};
    result.lspEnabled = m_lspEnabled->isChecked();
    result.lspExecutable = m_lspExecutable->text().trimmed();
    if (result.lspExecutable.isEmpty()) result.lspExecutable = QStringLiteral("clangd");
    result.lspArguments = m_lspArguments->text().trimmed();
    result.lspExtensions = m_lspExtensions->text().trimmed();
    return result;
}
}
