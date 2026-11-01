#include "LanguageSettingsDialog.h"
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QPushButton>
#include <QVBoxLayout>

namespace nya {
LanguageSettingsDialog::LanguageSettingsDialog(const LanguageSettings &settings, QWidget *parent) : QDialog(parent)
{
    setObjectName("languageSettingsDialog"); setWindowTitle(QStringLiteral("语言支持")); resize(720, 760);
    auto *outer = new QVBoxLayout(this);
    auto *scroll = new QScrollArea; scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
    auto *body = new QWidget; scroll->setWidget(body); outer->addWidget(scroll);
    auto *layout = new QVBoxLayout(body); layout->setContentsMargins(16, 16, 16, 12); layout->setSpacing(12);
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
    m_servers = settings.lspServers;
    auto *serverListRow = new QHBoxLayout;
    m_lspServers = new QListWidget; m_lspServers->setObjectName("lspServers"); m_lspServers->setMaximumHeight(110);
    for (const auto &server : m_servers) m_lspServers->addItem(server.name);
    serverListRow->addWidget(m_lspServers, 1);
    auto *serverButtons = new QVBoxLayout;
    auto *addServer = new QPushButton(QStringLiteral("添加服务器")); addServer->setObjectName("lspAddServer");
    m_removeServer = new QPushButton(QStringLiteral("删除服务器")); m_removeServer->setObjectName("lspRemoveServer");
    serverButtons->addWidget(addServer); serverButtons->addWidget(m_removeServer); serverButtons->addStretch();
    serverListRow->addLayout(serverButtons); layout->addLayout(serverListRow);
    m_serverForm = new QWidget;
    auto *lspForm = new QFormLayout(m_serverForm); lspForm->setContentsMargins(0, 0, 0, 0); lspForm->setSpacing(12);
    m_serverName = new QLineEdit; m_serverName->setObjectName("lspServerName");
    m_serverEnabled = new QCheckBox(QStringLiteral("启用此服务器")); m_serverEnabled->setObjectName("lspServerEnabled");
    lspForm->addRow(QStringLiteral("名称"), m_serverName); lspForm->addRow(QString(), m_serverEnabled);
    auto *serverRow = new QWidget; auto *serverLayout = new QHBoxLayout(serverRow); serverLayout->setContentsMargins(0, 0, 0, 0);
    m_lspExecutable = new QLineEdit; m_lspExecutable->setObjectName("lspExecutable");
    m_lspExecutable->setPlaceholderText(QStringLiteral("clangd 或服务器完整路径"));
    auto *browseServer = new QPushButton(QStringLiteral("浏览…"));
    serverLayout->addWidget(m_lspExecutable, 1); serverLayout->addWidget(browseServer);
    connect(browseServer, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getOpenFileName(this, QStringLiteral("选择 LSP 语言服务器"), m_lspExecutable->text());
        if (!path.isEmpty()) m_lspExecutable->setText(path);
    });
    lspForm->addRow(QStringLiteral("LSP 服务器"), serverRow);
    m_lspArguments = new QLineEdit; m_lspArguments->setObjectName("lspArguments");
    m_lspArguments->setPlaceholderText(QStringLiteral("例如 --background-index；含空格的参数用双引号包围"));
    lspForm->addRow(QStringLiteral("启动参数"), m_lspArguments);
    m_lspExtensions = new QLineEdit; m_lspExtensions->setObjectName("lspExtensions");
    m_lspExtensions->setPlaceholderText(QStringLiteral("例如 c,cpp,h,hpp 或 py；逗号分隔，不含点号"));
    lspForm->addRow(QStringLiteral("文件后缀"), m_lspExtensions); layout->addWidget(m_serverForm);
    auto *lspHint = new QLabel(QStringLiteral("可添加多个语言服务器，例如 clangd 对应 c,cpp,h，pylsp 对应 py。打开匹配文件后分别启动，多个服务器可同时运行；后缀重复时使用列表中首个已启用的服务器。补全：Alt+/；定义：F12 或 Ctrl/⌘ 点击。"));
    lspHint->setWordWrap(true); layout->addWidget(lspHint);
    connect(m_lspServers, &QListWidget::currentRowChanged, this, &LanguageSettingsDialog::selectServer);
    connect(m_serverName, &QLineEdit::textChanged, this, [this](const QString &name) {
        if (m_selected >= 0) m_lspServers->item(m_selected)->setText(name);
    });
    connect(addServer, &QPushButton::clicked, this, [this] {
        saveServer();
        m_servers.append({QStringLiteral("新语言服务器"), {}, {}, {}, true});
        m_lspServers->addItem(m_servers.last().name); m_lspServers->setCurrentRow(m_servers.size() - 1);
    });
    connect(m_removeServer, &QPushButton::clicked, this, [this] {
        if (m_selected < 0) return;
        const int row = m_selected;
        m_selected = -1;
        m_servers.removeAt(row);
        { QSignalBlocker blocker(m_lspServers); delete m_lspServers->takeItem(row); }
        const int next = m_servers.isEmpty() ? -1 : qMin(row, int(m_servers.size()) - 1);
        { QSignalBlocker blocker(m_lspServers); m_lspServers->setCurrentRow(next); }
        selectServer(next);
    });
    if (m_servers.isEmpty()) selectServer(-1); else m_lspServers->setCurrentRow(0);
    auto *hint = new QLabel(QStringLiteral("确认后保存并应用到所有文档，重新连接 LSP；取消不会更改当前设置。工具不可用时保留基础编辑功能，错误可在状态栏查看。"));
    hint->setWordWrap(true); layout->addWidget(hint);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("确定")); buttons->button(QDialogButtonBox::Ok)->setObjectName("primary");
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject); outer->addWidget(buttons);
}

LspServerSettings LanguageSettingsDialog::editedServer() const
{
    return {m_serverName->text().trimmed(), m_lspExecutable->text().trimmed(),
            m_lspArguments->text().trimmed(), m_lspExtensions->text().trimmed(), m_serverEnabled->isChecked()};
}

void LanguageSettingsDialog::saveServer()
{
    if (m_selected >= 0) m_servers[m_selected] = editedServer();
}

void LanguageSettingsDialog::selectServer(int row)
{
    saveServer(); m_selected = row;
    m_serverForm->setEnabled(row >= 0); m_removeServer->setEnabled(row >= 0);
    const auto server = row >= 0 ? m_servers.at(row) : LspServerSettings{{}, {}, {}, {}, false};
    m_serverName->setText(server.name); m_serverEnabled->setChecked(server.enabled);
    m_lspExecutable->setText(server.executable); m_lspArguments->setText(server.arguments);
    m_lspExtensions->setText(server.extensions);
}

LanguageSettings LanguageSettingsDialog::settings() const
{
    const auto executable = m_executable->text().trimmed();
    LanguageSettings result{m_enabled->isChecked(), executable.isEmpty() ? QStringLiteral("tree-sitter") : executable, m_config->text().trimmed()};
    result.lspEnabled = m_lspEnabled->isChecked();
    result.lspServers = m_servers;
    if (m_selected >= 0) result.lspServers[m_selected] = editedServer();
    return result;
}
}
