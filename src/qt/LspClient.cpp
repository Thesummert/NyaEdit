#include "LspClient.h"
#include <QCoreApplication>
#include <QDir>
#include <QJsonDocument>
#include <QTimer>
#include <cmath>

namespace nya {
namespace {
constexpr int MaxHeader = 8 * 1024;
constexpr int MaxBody = 16 * 1024 * 1024;
QJsonObject rpcError(int code, const QString &message)
{
    return {{"code", code}, {"message", message}};
}
QString documentUri(const QUrl &uri)
{
    return uri.isValid() && !uri.isRelative() ? uri.toString(QUrl::FullyEncoded) : QString();
}
QJsonObject identifier(const QString &uri) { return {{"uri", uri}}; }
QJsonObject position(int line, int character) { return {{"line", line}, {"character", character}}; }
bool lifecycleMethod(const QString &method)
{
    return method == "initialize" || method == "initialized" || method == "shutdown" || method == "exit";
}
}

LspClient::LspClient(QObject *parent) : QObject(parent) {}

LspClient::~LspClient()
{
    // stop() is asynchronous; an earlier stopped process may still be reaping
    // when its client is destroyed or has already started another server.
    const auto processes = findChildren<QProcess *>(QString(), Qt::FindDirectChildrenOnly);
    for (auto *process : processes) {
        disconnect(process, nullptr, this, nullptr);
        if (process->state() != QProcess::NotRunning) {
            process->kill();
            process->waitForFinished(1000);
        }
    }
}

void LspClient::setState(State state)
{
    if (m_state == state) return;
    m_state = state;
    emit stateChanged(state);
}

bool LspClient::start(const LspServerOptions &options)
{
    if (m_state != State::Stopped || options.program.trimmed().isEmpty()
        || options.requestTimeoutMs <= 0 || options.shutdownTimeoutMs <= 0) return false;
    m_options = options;
    auto *process = new QProcess(this);
    m_process = process;
    process->setProcessChannelMode(QProcess::SeparateChannels);
    process->setProcessEnvironment(options.environment);
    const auto directory = options.workingDirectory.isEmpty() ? options.rootPath : options.workingDirectory;
    if (!directory.isEmpty()) process->setWorkingDirectory(directory);
    connect(process, &QProcess::started, this, [this, process] {
        if (m_process == process && m_state == State::Starting) initialize();
    });
    connect(process, &QProcess::readyReadStandardOutput, this, [this, process] {
        if (m_process == process) readOutput();
    });
    connect(process, &QProcess::readyReadStandardError, this, [this, process] {
        // Read bounded chunks, without buffering stderr for the lifetime of the server.
        while (m_process == process) {
            process->setReadChannel(QProcess::StandardError);
            const auto bytes = process->read(64 * 1024);
            process->setReadChannel(QProcess::StandardOutput);
            if (bytes.isEmpty()) break;
            emit serverLog(bytes);
        }
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (m_process != process) return;
        if (error == QProcess::FailedToStart || error == QProcess::ReadError || error == QProcess::WriteError)
            fail(QStringLiteral("LSP 进程错误：%1").arg(process->errorString()));
    });
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), process, &QObject::deleteLater);
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
        [this, process](int code, QProcess::ExitStatus status) {
            if (m_process != process) return;
            readOutput(); // A server may exit immediately after its last response.
            if (m_process != process) return;
            if (m_state == State::ShuttingDown && code == 0 && status == QProcess::NormalExit) stop();
            else fail(QStringLiteral("LSP 进程已退出（%1）。").arg(code));
        });
    setState(State::Starting);
    if (m_process != process) return true;
    process->start(options.program, options.arguments);
    QTimer::singleShot(options.requestTimeoutMs, this, [this, process = QPointer<QProcess>(process)] {
        if (m_process == process && m_state == State::Starting) fail(QStringLiteral("LSP 启动超时。"));
    });
    return true;
}

bool LspClient::send(const QJsonObject &message)
{
    if (!m_process || m_process->state() != QProcess::Running) return false;
    const auto body = QJsonDocument(message).toJson(QJsonDocument::Compact);
    if (body.size() > MaxBody || m_process->bytesToWrite() + body.size() > 2 * MaxBody) return false;
    const auto frame = "Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body;
    return m_process->write(frame) == frame.size();
}

qint64 LspClient::sendRequest(const QString &method, const QJsonValue &params, RequestKind kind)
{
    const auto id = ++m_nextId;
    if (!send({{"jsonrpc", "2.0"}, {"id", double(id)}, {"method", method}, {"params", params}})) return -1;
    m_pending.insert(id, kind);
    QTimer::singleShot(m_options.requestTimeoutMs, this, [this, id] {
        if (!m_pending.contains(id)) return;
        const auto kind = m_pending.take(id);
        if (kind != RequestKind::User) { fail(QStringLiteral("LSP 生命周期请求超时。")); return; }
        if (m_state == State::Ready)
            send({{"jsonrpc", "2.0"}, {"method", "$/cancelRequest"}, {"params", QJsonObject{{"id", double(id)}}}});
        emit requestFailed(id, rpcError(-32001, QStringLiteral("LSP 请求超时。")));
    });
    return id;
}

void LspClient::initialize()
{
    const auto process = m_process;
    setState(State::Initializing);
    if (m_process != process || m_state != State::Initializing) return;
    const QJsonValue root = m_options.rootPath.isEmpty() ? QJsonValue(QJsonValue::Null)
        : QJsonValue(QUrl::fromLocalFile(QDir(m_options.rootPath).absolutePath()).toString(QUrl::FullyEncoded));
    const QJsonObject capabilities{{"general", QJsonObject{{"positionEncodings", QJsonArray{"utf-16"}}}}};
    const QJsonObject params{{"processId", double(QCoreApplication::applicationPid())},
        {"clientInfo", QJsonObject{{"name", "NyaEditCore"}}}, {"rootUri", root},
        {"capabilities", capabilities}, {"initializationOptions", m_options.initializationOptions}};
    if (sendRequest("initialize", params, RequestKind::Initialize) < 0) fail(QStringLiteral("无法发送 LSP 初始化请求。"));
}

qint64 LspClient::request(const QString &method, const QJsonValue &params)
{
    if (m_state != State::Ready || method.isEmpty() || lifecycleMethod(method)
        || (!params.isObject() && !params.isArray() && !params.isNull())) return -1;
    return sendRequest(method, params, RequestKind::User);
}

bool LspClient::notify(const QString &method, const QJsonValue &params)
{
    return m_state == State::Ready && !method.isEmpty() && !lifecycleMethod(method)
        && (params.isObject() || params.isArray() || params.isNull())
        && send({{"jsonrpc", "2.0"}, {"method", method}, {"params", params}});
}

bool LspClient::cancelRequest(qint64 id)
{
    if (!m_pending.contains(id) || m_pending.value(id) != RequestKind::User) return false;
    if (m_state == State::Ready)
        send({{"jsonrpc", "2.0"}, {"method", "$/cancelRequest"}, {"params", QJsonObject{{"id", double(id)}}}});
    m_pending.remove(id);
    emit requestFailed(id, rpcError(-32800, QStringLiteral("LSP 请求已取消。")));
    return true;
}

void LspClient::shutdown()
{
    if (m_state == State::Stopped || m_state == State::ShuttingDown) return;
    if (m_state != State::Ready) { stop(); return; }
    const auto process = m_process;
    setState(State::ShuttingDown);
    if (m_process != process || m_state != State::ShuttingDown) return;
    if (sendRequest("shutdown", QJsonValue::Null, RequestKind::Shutdown) < 0) { stop(); return; }
    QTimer::singleShot(m_options.shutdownTimeoutMs, this, [this, process] {
        if (m_process == process && m_state == State::ShuttingDown) stop();
    });
}

void LspClient::stop()
{
    if (m_state == State::Stopped) return;
    auto *process = m_process.data();
    m_process = nullptr;
    if (process) {
        disconnect(process, nullptr, this, nullptr);
        process->kill();
        if (process->state() == QProcess::NotRunning) process->deleteLater();
    }
    const auto pending = m_pending;
    m_pending.clear(); m_documents.clear(); m_capabilities = {};
    m_input.clear(); m_bodyLength = -1; m_syncKind = 0; m_openClose = false;
    setState(State::Stopped);
    for (auto it = pending.cbegin(); it != pending.cend(); ++it)
        if (it.value() == RequestKind::User)
            emit requestFailed(it.key(), rpcError(-32000, QStringLiteral("LSP 连接已停止。")));
}

void LspClient::fail(const QString &message)
{
    stop();
    emit failed(message);
}

void LspClient::readOutput()
{
    const auto process = m_process;
    while (process && m_process == process && process->bytesAvailable() > 0) {
        m_input += process->read(64 * 1024);
        while (m_process == process) {
            if (m_bodyLength < 0) {
                const auto end = m_input.indexOf("\r\n\r\n");
                if (end < 0) {
                    if (m_input.size() > MaxHeader) fail(QStringLiteral("LSP 消息头过大。"));
                    break;
                }
                if (end > MaxHeader) { fail(QStringLiteral("LSP 消息头过大。")); return; }
                const auto headers = m_input.left(end).split('\n');
                for (const auto &header : headers) {
                    const auto colon = header.indexOf(':');
                    if (colon < 0) { fail(QStringLiteral("无效的 LSP 消息头。")); return; }
                    if (header.left(colon).trimmed().toLower() != "content-length") continue;
                    const auto value = header.mid(colon + 1).trimmed();
                    bool ok = false;
                    const auto length = value.toLongLong(&ok);
                    for (const char c : value) if (c < '0' || c > '9') ok = false;
                    if (!ok || length <= 0 || length > MaxBody || m_bodyLength >= 0) {
                        fail(QStringLiteral("无效的 LSP Content-Length。")); return;
                    }
                    m_bodyLength = int(length);
                }
                if (m_bodyLength < 0) { fail(QStringLiteral("LSP 消息缺少 Content-Length。")); return; }
                m_input.remove(0, end + 4);
            }
            if (m_input.size() < m_bodyLength) break;
            QJsonParseError error;
            const auto message = QJsonDocument::fromJson(m_input.left(m_bodyLength), &error);
            m_input.remove(0, m_bodyLength); m_bodyLength = -1;
            if (error.error != QJsonParseError::NoError || !message.isObject()) {
                fail(QStringLiteral("无效的 LSP JSON 消息。")); return;
            }
            dispatch(message.object());
        }
    }
}

void LspClient::dispatch(const QJsonObject &message)
{
    if (message.value("jsonrpc") != QJsonValue("2.0")) { fail(QStringLiteral("无效的 LSP JSON-RPC 版本。")); return; }
    const auto id = message.value("id");
    const auto method = message.value("method");
    if (method.isString()) {
        if (!id.isUndefined()) {
            // No dynamic registration, workspace edits or other server requests
            // are advertised by this minimal client. Always reply to avoid hangs.
            send({{"jsonrpc", "2.0"}, {"id", id},
                  {"error", rpcError(-32601, QStringLiteral("Unsupported client method: %1").arg(method.toString()))}});
            return;
        }
        const auto params = message.value("params");
        if (method == QJsonValue("textDocument/publishDiagnostics")) {
            const auto object = params.toObject();
            emit diagnosticsPublished(QUrl(object.value("uri").toString()), object.value("diagnostics").toArray());
        }
        emit notificationReceived(method.toString(), params);
        return;
    }
    const auto number = id.toDouble(-1);
    if (!id.isDouble() || number < 0 || number > 9007199254740991.0 || std::floor(number) != number) {
        fail(QStringLiteral("无效的 LSP 响应 ID。")); return;
    }
    const auto requestId = qint64(number);
    if (!m_pending.contains(requestId)) return;
    const auto error = message.value("error");
    if (message.contains("result") == message.contains("error")
        || (message.contains("error") && (!error.isObject()
            || !error.toObject().value("code").isDouble() || !error.toObject().value("message").isString()))) {
        fail(QStringLiteral("无效的 LSP 响应内容。")); return;
    }
    const auto kind = m_pending.take(requestId);
    if (message.contains("error")) {
        if (kind == RequestKind::User) emit requestFailed(requestId, message.value("error").toObject());
        else fail(QStringLiteral("LSP 生命周期请求失败：%1").arg(message.value("error").toObject().value("message").toString()));
        return;
    }
    const auto result = message.value("result");
    if (kind == RequestKind::Initialize) {
        if (!result.isObject() || !result.toObject().value("capabilities").isObject()) {
            fail(QStringLiteral("无效的 LSP 初始化结果。")); return;
        }
        m_capabilities = result.toObject().value("capabilities").toObject();
        if (m_capabilities.value("positionEncoding").toString("utf-16") != "utf-16") {
            fail(QStringLiteral("LSP 服务端未使用 UTF-16 位置编码。")); return;
        }
        const auto sync = m_capabilities.value("textDocumentSync");
        m_syncKind = sync.isDouble() ? sync.toInt() : sync.toObject().value("change").toInt();
        m_openClose = sync.isDouble() ? m_syncKind != 0 : sync.toObject().value("openClose").toBool();
        if (!send({{"jsonrpc", "2.0"}, {"method", "initialized"}, {"params", QJsonObject()}})) {
            fail(QStringLiteral("无法发送 LSP initialized 通知。")); return;
        }
        const auto process = m_process;
        setState(State::Ready);
        if (m_process == process && m_state == State::Ready) emit ready();
    } else if (kind == RequestKind::Shutdown) {
        if (send({{"jsonrpc", "2.0"}, {"method", "exit"}}) && m_process) m_process->closeWriteChannel();
        else stop();
    } else emit responseReceived(requestId, result);
}

bool LspClient::openDocument(const QUrl &uri, const QString &languageId, const QString &text, int version)
{
    const auto key = documentUri(uri);
    if (m_state != State::Ready || key.isEmpty() || languageId.isEmpty() || m_documents.contains(key)) return false;
    if (m_openClose && !notify("textDocument/didOpen", QJsonObject{{"textDocument",
        QJsonObject{{"uri", key}, {"languageId", languageId}, {"version", version}, {"text", text}}}})) return false;
    m_documents.insert(key, {text, version});
    return true;
}

bool LspClient::changeDocument(const QUrl &uri, const QString &text, int version)
{
    const auto key = documentUri(uri);
    auto it = m_documents.find(key);
    if (m_state != State::Ready || it == m_documents.end() || version <= it->version
        || (m_syncKind != 1 && m_syncKind != 2)) return false;
    QJsonObject change{{"text", text}};
    if (m_syncKind == 2) {
        const auto &previous = it->text;
        const int lastNewline = int(previous.lastIndexOf('\n'));
        change.insert("range", QJsonObject{{"start", position(0, 0)},
            {"end", position(int(previous.count('\n')), int(previous.size()) - lastNewline - 1)}});
    }
    if (!notify("textDocument/didChange", QJsonObject{{"textDocument", QJsonObject{{"uri", key}, {"version", version}}},
        {"contentChanges", QJsonArray{change}}})) return false;
    *it = {text, version};
    return true;
}

bool LspClient::saveDocument(const QUrl &uri)
{
    const auto key = documentUri(uri);
    const auto it = m_documents.constFind(key);
    if (it == m_documents.cend()) return false;
    const auto save = m_capabilities.value("textDocumentSync").toObject().value("save");
    if (save != QJsonValue(true) && !save.isObject()) return false;
    QJsonObject params{{"textDocument", identifier(key)}};
    if (save.toObject().value("includeText").toBool()) params.insert("text", it->text);
    return notify("textDocument/didSave", params);
}

bool LspClient::closeDocument(const QUrl &uri)
{
    const auto key = documentUri(uri);
    if (m_state != State::Ready || !m_documents.contains(key)) return false;
    if (m_openClose && !notify("textDocument/didClose", QJsonObject{{"textDocument", identifier(key)}})) return false;
    m_documents.remove(key);
    return true;
}

qint64 LspClient::requestPosition(const QString &method, const QUrl &uri, int line, int character)
{
    const auto key = documentUri(uri);
    if (key.isEmpty() || line < 0 || character < 0) return -1;
    return request(method, QJsonObject{{"textDocument", identifier(key)}, {"position", position(line, character)}});
}
qint64 LspClient::completion(const QUrl &uri, int line, int character)
{
    return requestPosition("textDocument/completion", uri, line, character);
}
qint64 LspClient::hover(const QUrl &uri, int line, int character)
{
    return requestPosition("textDocument/hover", uri, line, character);
}
qint64 LspClient::definition(const QUrl &uri, int line, int character)
{
    return requestPosition("textDocument/definition", uri, line, character);
}
}
