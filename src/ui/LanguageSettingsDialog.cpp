#include "LanguageSettingsDialog.h"
#include "TreeSitterManager.h"
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QDir>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>

namespace nya {
LanguageSettingsDialog::LanguageSettingsDialog(const LanguageSettings &settings, QWidget *parent, StoragePaths paths)
    : QDialog(parent), m_paths(std::move(paths))
{
    const auto defaults = StoragePaths::standard();
    if (m_paths.configDirectory.isEmpty()) m_paths.configDirectory = defaults.configDirectory;
    if (m_paths.dataDirectory.isEmpty()) m_paths.dataDirectory = defaults.dataDirectory;
    setObjectName("languageSettingsDialog"); setWindowTitle(QStringLiteral("语言支持")); resize(720, 760);
    auto *outer = new QVBoxLayout(this);
    auto *scroll = new QScrollArea; scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
    auto *body = new QWidget; scroll->setWidget(body); outer->addWidget(scroll);
    auto *bodyLayout = new QVBoxLayout(body); bodyLayout->setContentsMargins(16, 16, 16, 12);
    auto *tabs = new QTabWidget; tabs->setObjectName("languageSettingsTabs"); bodyLayout->addWidget(tabs);
    auto *treePage = new QWidget; tabs->addTab(treePage, QStringLiteral("Tree-sitter"));
    auto *layout = new QVBoxLayout(treePage); layout->setSpacing(12);
    auto *description = new QLabel(QStringLiteral("Tree-sitter 提供语法着色。使用编辑器独立配置，可下载所需语言解析器；运行时需要已安装的 Tree-sitter CLI 和 C/C++ 编译器。"));
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
            if (m_manager && m_manager->isDownloading()) return;
            const auto path = QFileDialog::getOpenFileName(this, config ? QStringLiteral("选择 Tree-sitter 配置") : QStringLiteral("选择 Tree-sitter 可执行文件"), input->text());
            if (!path.isEmpty()) input->setText(path);
        });
        return input;
    };
    m_executable = addPath(QStringLiteral("可执行文件"), settings.executable, "treeSitterExecutable", false);
    m_executable->setPlaceholderText("tree-sitter");
    m_config = addPath(QStringLiteral("配置文件"), settings.configPath.isEmpty() ? m_paths.treeSitterConfigFile() : settings.configPath, "treeSitterConfigPath", true);
    m_config->setPlaceholderText(m_paths.treeSitterConfigFile()); layout->addLayout(form);
    auto *parserRow = new QHBoxLayout;
    m_parserDirectory = new QLineEdit(settings.parserDirectory.isEmpty() ? m_paths.treeSitterParserDirectory() : settings.parserDirectory);
    m_parserDirectory->setObjectName("treeSitterParserDirectory"); m_parserDirectory->setAccessibleName(QStringLiteral("解析器存放目录"));
    auto *browseParsers = new QPushButton(QStringLiteral("浏览…"));
    parserRow->addWidget(new QLabel(QStringLiteral("解析器存放目录"))); parserRow->addWidget(m_parserDirectory, 1); parserRow->addWidget(browseParsers);
    layout->addLayout(parserRow);
    connect(browseParsers, &QPushButton::clicked, this, [this] {
        if (m_manager->isDownloading()) return;
        const auto directory = QFileDialog::getExistingDirectory(this, QStringLiteral("选择解析器存放目录"), m_parserDirectory->text());
        if (!directory.isEmpty()) m_parserDirectory->setText(directory);
    });
    auto *useEditorConfig = new QPushButton(QStringLiteral("使用编辑器独立配置")); useEditorConfig->setObjectName("treeSitterUseEditorConfig");
    layout->addWidget(useEditorConfig, 0, Qt::AlignLeft);
    connect(useEditorConfig, &QPushButton::clicked, this, [this] {
        if (m_manager->isDownloading()) return;
        m_config->setText(m_paths.treeSitterConfigFile()); m_parserDirectory->setText(m_paths.treeSitterParserDirectory());
    });
    auto *status = new QLabel; status->setObjectName("treeSitterDetection"); status->setWordWrap(true);
    const auto updateStatus = [this, status] {
        const auto executable = m_executable->text().trimmed();
        const auto path = TreeSitter::findExecutable(executable.isEmpty() ? QStringLiteral("tree-sitter") : executable);
        status->setText(path.isEmpty() ? QStringLiteral("未检测到可执行文件；启用后仍会保留基础着色。") : QStringLiteral("已找到：%1").arg(path));
    };
    connect(m_executable, &QLineEdit::textChanged, this, updateStatus); updateStatus(); layout->addWidget(status);
    layout->addWidget(new QLabel(QStringLiteral("语言解析器（来源：github.com/tree-sitter）")));
    m_parsers = new QListWidget; m_parsers->setObjectName("treeSitterParsers"); m_parsers->setMinimumHeight(170);
    layout->addWidget(m_parsers);
    auto *downloadRow = new QHBoxLayout;
    m_download = new QPushButton(QStringLiteral("下载所选解析器")); m_download->setObjectName("treeSitterDownload");
    m_cancelDownload = new QPushButton(QStringLiteral("取消下载")); m_cancelDownload->setObjectName("treeSitterCancelDownload"); m_cancelDownload->setEnabled(false);
    downloadRow->addWidget(m_download); downloadRow->addWidget(m_cancelDownload); downloadRow->addStretch(); layout->addLayout(downloadRow);
    m_downloadStatus = new QLabel; m_downloadStatus->setObjectName("treeSitterDownloadStatus"); m_downloadStatus->setWordWrap(true); m_downloadStatus->setTextFormat(Qt::PlainText);
    layout->addWidget(m_downloadStatus);
    auto *parserHint = new QLabel(QStringLiteral("下载需要 Git 和网络连接。确定后将存放目录加入配置文件的 parser-directories，并应用到所有文档。已下载的文件会保留，取消设置不会删除它们。更换目录不会移动原有解析器。"));
    parserHint->setWordWrap(true); layout->addWidget(parserHint); layout->addStretch();
    m_manager = new TreeSitterManager(this);
    connect(m_parserDirectory, &QLineEdit::textChanged, this, &LanguageSettingsDialog::refreshParsers);
    connect(m_parsers, &QListWidget::currentRowChanged, this, [this] { setDownloading(m_manager->isDownloading()); });
    connect(m_download, &QPushButton::clicked, this, [this] {
        if (m_parsers->currentRow() < 0) return;
        setDownloading(true);
        m_manager->download(TreeSitterManager::catalog().at(m_parsers->currentRow()).id, m_parserDirectory->text().trimmed());
    });
    connect(m_cancelDownload, &QPushButton::clicked, this, [this] {
        m_manager->cancel(); setDownloading(false); m_downloadStatus->setText(QStringLiteral("下载已取消，未完成的文件会自动清理。"));
    });
    connect(m_manager, &TreeSitterManager::progress, m_downloadStatus, &QLabel::setText);
    connect(m_manager, &TreeSitterManager::failed, this, [this](const QString &message) { setDownloading(false); m_downloadStatus->setText(message); });
    connect(m_manager, &TreeSitterManager::installed, this, [this](const QString &path) {
        setDownloading(false); refreshParsers(); m_downloadStatus->setText(QStringLiteral("已下载到：%1\n点击确定保存配置并开始使用。 ").arg(path));
    });
    auto *lspPage = new QWidget; tabs->addTab(lspPage, QStringLiteral("LSP"));
    layout = new QVBoxLayout(lspPage); layout->setSpacing(12);
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
    hint->setWordWrap(true); bodyLayout->addWidget(hint);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    m_buttons = buttons;
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("确定")); buttons->button(QDialogButtonBox::Ok)->setObjectName("primary");
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    connect(buttons, &QDialogButtonBox::accepted, this, &LanguageSettingsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &LanguageSettingsDialog::reject); outer->addWidget(buttons);
    refreshParsers();
}

void LanguageSettingsDialog::refreshParsers()
{
    const int selected = qMax(0, m_parsers->currentRow());
    QSignalBlocker blocker(m_parsers);
    m_parsers->clear();
    const auto directory = m_parserDirectory->text().trimmed();
    for (const auto &parser : TreeSitterManager::catalog()) {
        auto *item = new QListWidgetItem(QStringLiteral("%1  (.%2) — %3").arg(parser.name, parser.extensions,
            TreeSitterManager::isInstalled(parser, directory) ? QStringLiteral("已下载") : QStringLiteral("未下载")), m_parsers);
        item->setToolTip(parser.repository() + "\n" + QDir(directory).filePath(parser.directoryName()));
    }
    m_parsers->setCurrentRow(selected); setDownloading(m_manager->isDownloading());
}

void LanguageSettingsDialog::setDownloading(bool downloading)
{
    m_parserDirectory->setEnabled(!downloading); m_config->setEnabled(!downloading); m_parsers->setEnabled(!downloading);
    const int row = m_parsers->currentRow();
    m_download->setEnabled(!downloading && row >= 0 &&
        !TreeSitterManager::isInstalled(TreeSitterManager::catalog().at(row), m_parserDirectory->text().trimmed()));
    m_cancelDownload->setEnabled(downloading);
    if (m_buttons) m_buttons->button(QDialogButtonBox::Ok)->setEnabled(!downloading);
}

void LanguageSettingsDialog::accept()
{
    if (m_manager->isDownloading()) return;
    QString error;
    const auto selected = settings();
    if (!TreeSitterManager::saveConfiguration(selected.configPath, selected.parserDirectory, &error)) {
        findChild<QTabWidget *>("languageSettingsTabs")->setCurrentIndex(0);
        m_downloadStatus->setText(error); return;
    }
    QDialog::accept();
}

void LanguageSettingsDialog::reject()
{
    m_manager->cancel(); QDialog::reject();
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
    if (result.configPath.isEmpty()) result.configPath = m_paths.treeSitterConfigFile();
    result.parserDirectory = m_parserDirectory->text().trimmed();
    if (result.parserDirectory.isEmpty()) result.parserDirectory = m_paths.treeSitterParserDirectory();
    result.lspEnabled = m_lspEnabled->isChecked();
    result.lspServers = m_servers;
    if (m_selected >= 0) result.lspServers[m_selected] = editedServer();
    return result;
}
}
