#include "EditorLsp.h"
#include "Editor.h"
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTextBlock>

namespace nya {
namespace {
QString languageId(const QString &suffix)
{
    static const QHash<QString, QString> languages{
        {"h", "cpp"}, {"hpp", "cpp"}, {"hxx", "cpp"}, {"hh", "cpp"}, {"cc", "cpp"}, {"cxx", "cpp"},
        {"py", "python"}, {"pyw", "python"}, {"js", "javascript"}, {"mjs", "javascript"}, {"cjs", "javascript"}, {"jsx", "javascriptreact"},
        {"ts", "typescript"}, {"mts", "typescript"}, {"cts", "typescript"}, {"tsx", "typescriptreact"}, {"rs", "rust"}, {"cs", "csharp"},
        {"m", "objective-c"}, {"mm", "objective-cpp"}, {"sh", "shellscript"}, {"bash", "shellscript"}, {"md", "markdown"},
        {"rb", "ruby"}, {"kt", "kotlin"}, {"kts", "kotlin"}, {"ex", "elixir"}, {"exs", "elixir"},
        {"yml", "yaml"}, {"htm", "html"}, {"tex", "latex"}};
    return languages.value(suffix, suffix);
}
QString findServer(const QString &program)
{
    auto path = QStandardPaths::findExecutable(program);
#ifdef Q_OS_MAC
    if (path.isEmpty() && !program.contains('/'))
        path = QStandardPaths::findExecutable(program, {"/opt/homebrew/bin", "/usr/local/bin", "/opt/local/bin"});
#endif
    return path.isEmpty() ? program : path;
}
bool capability(const QJsonValue &value) { return value == QJsonValue(true) || value.isObject(); }
}

EditorLsp::EditorLsp(QObject *parent) : QObject(parent)
{
    setObjectName("editorLsp");
    m_syncTimer.setSingleShot(true); m_syncTimer.setInterval(150);
    m_completionTimer.setSingleShot(true); m_completionTimer.setInterval(200);
    connect(&m_syncTimer, &QTimer::timeout, this, [this] {
        for (auto *editor : m_editors) refreshEditor(editor);
    });
    connect(&m_completionTimer, &QTimer::timeout, this, [this] {
        if (m_completionEditor && m_completionEditor == m_current && m_current->hasFocus())
            requestFeature(m_completionEditor, false, true);
    });
    addServer();
}

void EditorLsp::addServer()
{
    auto owned = std::make_unique<Server>();
    auto *server = owned.get();
    auto *client = server->client = new LspClient(this);
    m_servers.push_back(std::move(owned));
    connect(client, &LspClient::ready, this, [this, server] {
        for (auto *editor : m_editors)
            if (serverFor(editor) == server) refreshEditor(editor);
        emit statusChanged();
    });
    connect(client, &LspClient::stateChanged, this, [this] { emit statusChanged(); });
    connect(client, &LspClient::failed, this, [this, server](const QString &error) {
        server->error = error;
        if (!server->log.trimmed().isEmpty()) server->error += '\n' + QString::fromUtf8(server->log.trimmed());
        server->pending.clear();
        for (auto it = m_documents.begin(); it != m_documents.end();) {
            if (it->client == server->client) {
                it.key()->hideCompletions(); it = m_documents.erase(it);
            } else ++it;
        }
        emit statusChanged();
    });
    connect(client, &LspClient::serverLog, this, [server](const QByteArray &bytes) { server->log = (server->log + bytes).right(4096); });
    connect(client, &LspClient::responseReceived, this, [this, server](qint64 id, const QJsonValue &result) { receive(server, id, result); });
    connect(client, &LspClient::requestFailed, this, [this, server](qint64 id, const QJsonObject &error) {
        if (!server->pending.contains(id)) return;
        const auto pending = server->pending.take(id);
        if (!pending.automatic && pending.editor && pending.editor == m_current && error.value("code").toInt() != -32800)
            emit message(QStringLiteral("LSP 请求失败：%1").arg(error.value("message").toString()));
    });
}

void EditorLsp::configure(const LanguageSettings &settings, const QString &rootPath)
{
    m_syncTimer.stop(); m_completionTimer.stop(); m_completionEditor = nullptr;
    m_documents.clear();
    for (const auto &server : m_servers) {
        server->pending.clear(); server->client->stop(); server->error.clear(); server->log.clear();
    }
    m_settings = settings; m_rootPath = rootPath;
    while (m_servers.size() > size_t(settings.lspServers.size())) {
        delete m_servers.back()->client;
        m_servers.pop_back();
    }
    while (m_servers.size() < size_t(settings.lspServers.size())) addServer();
    for (size_t i = 0; i < m_servers.size(); ++i) m_servers[i]->settings = settings.lspServers.at(int(i));
    for (auto *editor : m_editors) editor->hideCompletions();
    // Prefer the active document when there is no workspace folder.
    if (m_current) refreshEditor(m_current);
    for (auto *editor : m_editors) refreshEditor(editor);
    emit statusChanged();
}

void EditorLsp::addEditor(Editor *editor)
{
    if (m_editors.contains(editor)) return;
    m_editors.insert(editor);
    connect(editor, &Editor::textChanged, this, [this, editor] {
        // Qt also emits this signal for formatting-only changes. Keep requests
        // and the completion list when the synchronized source is unchanged.
        const auto document = m_documents.constFind(editor);
        if (document != m_documents.cend() && document->text == editor->contents()) return;
        cancelRequests(editor); editor->hideCompletions(); m_syncTimer.start();
    });
    connect(editor, &Editor::cursorPositionChanged, this, [this, editor] {
        cancelRequests(editor);
        if (m_completionEditor == editor) m_completionTimer.stop();
    });
    connect(editor, &Editor::completionRequested, this, [this, editor](bool automatic) { complete(editor, automatic); });
    connect(editor, &Editor::definitionRequested, this, [this, editor] { requestFeature(editor, true, false); });
    connect(editor, &QObject::destroyed, this, [this, editor] {
        // Do not access Editor members from QObject::destroyed.
        m_editors.remove(editor);
        if (m_documents.contains(editor)) {
            const auto document = m_documents.take(editor); document.client->closeDocument(document.uri);
        }
        cancelRequests(nullptr);
    });
}

void EditorLsp::removeEditor(Editor *editor)
{
    cancelRequests(editor); editor->hideCompletions();
    if (m_completionEditor == editor) { m_completionTimer.stop(); m_completionEditor = nullptr; }
    if (m_documents.contains(editor)) {
        const auto document = m_documents.take(editor); document.client->closeDocument(document.uri);
    }
    m_editors.remove(editor); disconnect(editor, nullptr, this, nullptr);
}

void EditorLsp::setCurrentEditor(Editor *editor)
{
    if (m_current == editor) return;
    m_completionTimer.stop();
    if (m_current) { cancelRequests(m_current); m_current->hideCompletions(); }
    m_current = editor;
    if (editor) refreshEditor(editor);
    emit statusChanged();
}

EditorLsp::Server *EditorLsp::serverFor(Editor *editor) const
{
    if (!editor || editor->path.isEmpty()) return nullptr;
    const auto suffix = QFileInfo(editor->path).suffix().toLower();
    if (suffix.isEmpty()) return nullptr;
    for (const auto &server : m_servers) {
        if (!server->settings.enabled) continue;
        const auto extensions = server->settings.extensions.toLower().split(QRegularExpression("[,;\\s]+"), Qt::SkipEmptyParts);
        if (extensions.contains(suffix)) return server.get();
    }
    return nullptr;
}

bool EditorLsp::eligible(Editor *editor) const
{
    return editor && m_editors.contains(editor) && m_settings.lspEnabled && !editor->path.isEmpty()
        && !editor->loading && !editor->largeFile && !editor->isReadOnly()
        && editor->document()->characterCount() <= 2 * 1024 * 1024 && serverFor(editor);
}

void EditorLsp::ensureStarted(Server *server, Editor *editor)
{
    if (server->client->state() != LspClient::State::Stopped || !server->error.isEmpty()) return;
    LspServerOptions options;
    options.program = findServer(server->settings.executable);
    options.arguments = QProcess::splitCommand(server->settings.arguments);
    options.rootPath = m_rootPath.isEmpty() ? QFileInfo(editor->path).absolutePath() : m_rootPath;
    if (!server->client->start(options)) {
        server->error = QStringLiteral("无法启动 LSP，请检查服务器路径和启动参数。"); emit statusChanged();
    }
}

void EditorLsp::refreshEditor(Editor *editor, bool saved)
{
    if (!m_editors.contains(editor)) return;
    const auto uri = QUrl::fromLocalFile(editor->path);
    auto *server = eligible(editor) ? serverFor(editor) : nullptr;
    auto it = m_documents.find(editor);
    if (it != m_documents.end() && (!server || it->uri != uri || it->client != server->client)) {
        cancelRequests(editor); editor->hideCompletions();
        it->client->closeDocument(it->uri); m_documents.erase(it); it = m_documents.end();
    }
    if (!server) return;
    ensureStarted(server, editor);
    auto *client = server->client;
    if (client->state() != LspClient::State::Ready) return;
    const auto text = editor->contents();
    if (it == m_documents.end()) {
        if (!client->openDocument(uri, languageId(QFileInfo(editor->path).suffix().toLower()), text)) return;
        it = m_documents.insert(editor, {client, uri, text, 1});
    } else if (it->text != text) {
        if (!client->changeDocument(uri, text, it->version + 1)) return;
        it->text = text; ++it->version;
    }
    if (saved) client->saveDocument(uri);
}

void EditorLsp::cancelRequests(Editor *editor)
{
    for (const auto &server : m_servers) {
        const auto keys = server->pending.keys();
        for (const auto id : keys) {
            if (server->pending.value(id).editor == editor || server->pending.value(id).editor.isNull()) {
                server->pending.remove(id); server->client->cancelRequest(id);
            }
        }
    }
}

void EditorLsp::complete(Editor *editor, bool automatic)
{
    m_completionTimer.stop();
    if (!automatic) { requestFeature(editor, false, false); return; }
    if (!eligible(editor) || editor != m_current || editor->textCursor().hasSelection()) return;
    const auto cursor = editor->textCursor();
    const auto before = cursor.block().text().left(cursor.positionInBlock());
    if (before.isEmpty()) return;
    int prefixLength = 0;
    for (int i = int(before.size()) - 1; i >= 0 && (before.at(i).isLetterOrNumber() || before.at(i) == '_'); --i) ++prefixLength;
    const auto triggers = serverFor(editor)->client->serverCapabilities().value("completionProvider").toObject().value("triggerCharacters").toArray();
    bool trigger = false;
    for (const auto &value : triggers) if (before.endsWith(value.toString()) && !value.toString().isEmpty()) trigger = true;
    if (prefixLength < 2 && !trigger) return;
    m_completionEditor = editor; m_completionTimer.start();
}

void EditorLsp::requestFeature(Editor *editor, bool definition, bool automatic)
{
    m_completionTimer.stop();
    if (!eligible(editor)) {
        if (!automatic) emit message(QStringLiteral("请在“设置 → 语言支持”启用 LSP，并打开后缀匹配的代码文件；大文件模式会跳过 LSP。"));
        return;
    }
    refreshEditor(editor);
    auto *server = serverFor(editor);
    auto *client = server->client;
    if (client->state() != LspClient::State::Ready || !m_documents.contains(editor)) {
        if (!automatic) emit message(server->error.isEmpty() ? QStringLiteral("LSP 正在启动，请就绪后重试。") : server->error);
        return;
    }
    if (m_documents.value(editor).text != editor->contents()) {
        if (!automatic) emit message(QStringLiteral("LSP 无法同步当前文档，暂时无法请求语言功能。"));
        return;
    }
    if (!capability(client->serverCapabilities().value(definition ? "definitionProvider" : "completionProvider"))) {
        if (!automatic) emit message(definition ? QStringLiteral("此语言服务器不支持定义跳转。") : QStringLiteral("此语言服务器不支持代码补全。"));
        return;
    }
    const auto cursor = editor->textCursor();
    if (cursor.hasSelection()) return;
    cancelRequests(editor); editor->hideCompletions();
    const auto uri = m_documents.value(editor).uri;
    const auto id = definition ? client->definition(uri, cursor.blockNumber(), cursor.positionInBlock())
                               : client->completion(uri, cursor.blockNumber(), cursor.positionInBlock());
    if (id < 0) { if (!automatic) emit message(QStringLiteral("无法发送 LSP 请求。")); return; }
    server->pending.insert(id, {editor, uri, m_documents.value(editor).version, cursor.position(), definition, automatic});
}

void EditorLsp::receive(Server *server, qint64 id, const QJsonValue &result)
{
    if (!server->pending.contains(id)) return;
    const auto pending = server->pending.take(id);
    auto *editor = pending.editor.data();
    if (!editor || editor != m_current || !eligible(editor) || !m_documents.contains(editor)
        || m_documents.value(editor).client != server->client
        || pending.uri != m_documents.value(editor).uri || pending.version != m_documents.value(editor).version
        || m_documents.value(editor).text != editor->contents()
        || pending.position != editor->textCursor().position() || editor->textCursor().hasSelection()) return;
    if (!pending.definition) {
        if (!editor->hasFocus() || !editor->isVisible()) return;
        if (!editor->showCompletions(result) && !pending.automatic) emit message(QStringLiteral("没有可用的补全项。"));
        return;
    }
    QJsonArray locations;
    const auto values = result.isArray() ? result.toArray() : QJsonArray{result};
    for (const auto &value : values) {
        const auto object = value.toObject();
        const auto uri = object.value("uri").toString(object.value("targetUri").toString());
        const auto range = object.value("range").toObject(object.value("targetSelectionRange").toObject(object.value("targetRange").toObject()));
        if (!uri.isEmpty() && range.value("start").isObject()) locations.append(QJsonObject{{"uri", uri}, {"range", range}});
    }
    if (locations.isEmpty()) emit message(QStringLiteral("未找到定义。"));
    else emit definitionsFound(editor, locations);
}

QString EditorLsp::statusText() const
{
    if (!m_settings.lspEnabled) return QStringLiteral("LSP · 关闭");
    auto *server = serverFor(m_current);
    if (!server) return QStringLiteral("LSP · 等待");
    if (!server->error.isEmpty()) return QStringLiteral("LSP · 异常");
    switch (server->client->state()) {
    case LspClient::State::Ready: return QStringLiteral("LSP · 就绪");
    case LspClient::State::Starting:
    case LspClient::State::Initializing: return QStringLiteral("LSP · 启动中");
    default: return QStringLiteral("LSP · 等待");
    }
}
QString EditorLsp::statusDetail() const
{
    if (!m_settings.lspEnabled) return QStringLiteral("在“设置 → 语言支持”开启 LSP 补全与定义跳转。");
    auto *server = serverFor(m_current);
    if (!server) return QStringLiteral("当前文件未匹配已启用的语言服务器。\n点击打开语言支持设置，添加服务器及文件后缀。");
    const auto detail = QStringLiteral("服务器：%1（%2）\n适用后缀：%3\n")
        .arg(server->settings.name, server->settings.executable, server->settings.extensions);
    if (!server->error.isEmpty()) return detail + server->error + QStringLiteral("\n点击配置并重新连接。");
    if (server->client->state() == LspClient::State::Ready)
        return detail + QStringLiteral("补全：Alt+/ 或 Ctrl/⌘+Space；定义：F12 或 Ctrl/⌘ 点击。\n点击可配置并重新连接。");
    return detail + QStringLiteral("打开后缀匹配的已保存文件后自动连接；大文件模式会跳过 LSP。\n点击打开语言支持设置。");
}
}
