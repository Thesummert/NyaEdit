#include "LspClient.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest>
#include <cstdio>
#include <memory>
#ifdef Q_OS_WIN
#include <fcntl.h>
#include <io.h>
#endif

using namespace nya;

static QByteArray frame(const QJsonObject &object)
{
    const auto body = QJsonDocument(object).toJson(QJsonDocument::Compact);
    return "Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body;
}
static void writeBytes(const QByteArray &bytes)
{
    std::fwrite(bytes.constData(), 1, size_t(bytes.size()), stdout);
    std::fflush(stdout);
}
static QJsonObject readMessage()
{
    QByteArray header;
    while (!header.endsWith("\r\n\r\n")) {
        const auto c = std::getchar();
        if (c == EOF || header.size() > 8192) return {};
        header += char(c);
    }
    const auto length = header.mid(16).trimmed().toInt();
    if (length <= 0 || length > 16 * 1024 * 1024) return {};
    QByteArray body(length, '\0');
    if (std::fread(body.data(), 1, size_t(length), stdin) != size_t(length)) return {};
    return QJsonDocument::fromJson(body).object();
}
static QJsonObject response(const QJsonValue &id, const QJsonValue &result)
{
    return {{"jsonrpc", "2.0"}, {"id", id}, {"result", result}};
}
static QJsonObject notification(const QString &method, const QJsonValue &params)
{
    return {{"jsonrpc", "2.0"}, {"method", method}, {"params", params}};
}

// A real stdio subprocess, shared by every platform, with no installed LSP dependency.
static int fixture(const QString &mode)
{
#ifdef Q_OS_WIN
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    auto initialize = readMessage();
    if (initialize.value("method") != QJsonValue("initialize")) return 2;
    const auto initialParams = initialize.value("params").toObject();
    if (mode == "init-timeout") { QThread::msleep(10000); return 0; }
    if (mode == "init-error") {
        writeBytes(frame({{"jsonrpc", "2.0"}, {"id", initialize.value("id")},
                          {"error", QJsonObject{{"code", -32603}, {"message", "fixture init error"}}}}));
        while (std::getchar() != EOF) {}
        return 0;
    }
    QJsonObject sync{{"openClose", true}, {"change", mode == "full" ? 1 : 2},
                     {"save", QJsonObject{{"includeText", true}}}};
    QJsonObject capabilities{{"textDocumentSync", sync}, {"hoverProvider", true},
                             {"completionProvider", QJsonObject()}, {"definitionProvider", true}};
    if (mode == "numeric") capabilities.insert("textDocumentSync", 1);
    if (mode == "none") capabilities.insert("textDocumentSync", QJsonObject{{"change", 0}});
    if (mode == "utf8") capabilities.insert("positionEncoding", "utf-8");
    auto first = frame(response(initialize.value("id"), QJsonObject{{"capabilities", capabilities}}));
    if (mode == "fragmented") {
        // Include lowercase headers, an extra header and splits inside UTF-8 JSON.
        first.replace("Content-Length", "content-length");
        first.replace("\r\n\r\n", "\r\nContent-Type: application/vscode-jsonrpc; charset=utf-8\r\n\r\n");
        for (int offset = 0; offset < first.size(); offset += 7) {
            writeBytes(first.mid(offset, 7)); QThread::msleep(1);
        }
    } else writeBytes(first);
    bool initialized = false;
    QJsonArray history;
    QJsonArray held;
    while (true) {
        const auto message = readMessage();
        if (message.isEmpty()) return 0;
        const auto method = message.value("method").toString();
        const auto id = message.value("id");
        const auto params = message.value("params");
        if (method == "initialized") { initialized = true; continue; }
        if (!initialized) return 3;
        if (method == "exit") return 0;
        if (method == "shutdown") {
            if (mode != "shutdown-timeout") writeBytes(frame(response(id, QJsonValue::Null)));
        } else if (mode.startsWith("ui") && method == "textDocument/completion") {
            if (mode == "ui-delay") QThread::msleep(350);
            const auto position = params.toObject().value("position").toObject();
            const QJsonObject range{{"start", QJsonObject{{"line", position.value("line")}, {"character", 0}}}, {"end", position}};
            writeBytes(frame(response(id, QJsonObject{{"isIncomplete", false}, {"items", QJsonArray{
                QJsonObject{{"label", "printValue"}, {"sortText", "0"}, {"detail", "fixture completion"},
                    {"textEdit", QJsonObject{{"range", range}, {"newText", "printValue"}}}},
                QJsonObject{{"label", "printOther"}, {"sortText", "1"}, {"insertText", "printOther"}}}}})));
        } else if (mode.startsWith("ui") && method == "textDocument/definition") {
            if (mode == "ui-delay") QThread::msleep(350);
            const auto root = QUrl(initialParams.value("rootUri").toString()).toLocalFile();
            const auto uri = QUrl::fromLocalFile(QDir(root).filePath("definition.cpp")).toString(QUrl::FullyEncoded);
            const QJsonObject range{{"start", QJsonObject{{"line", 1}, {"character", 4}}},
                                    {"end", QJsonObject{{"line", 1}, {"character", 10}}}};
            const QJsonObject location = mode == "ui-link"
                ? QJsonObject{{"targetUri", uri}, {"targetRange", range}, {"targetSelectionRange", range}}
                : QJsonObject{{"uri", uri}, {"range", range}};
            writeBytes(frame(response(id, mode == "ui-multiple" ? QJsonArray{location, location} : QJsonArray{location})));
        } else if (method == "fixture/history") {
            writeBytes(frame(response(id, QJsonObject{{"history", history}, {"initialize", initialParams}})));
        } else if (method == "fixture/hold") {
            held.append(id);
        } else if (method == "fixture/release") {
            QByteArray bytes;
            for (const auto &pending : held) bytes += frame(response(pending, "late"));
            held = {};
            writeBytes(bytes + frame(response(id, true)));
        } else if (method == "fixture/error") {
            writeBytes(frame({{"jsonrpc", "2.0"}, {"id", id},
                              {"error", QJsonObject{{"code", -32602}, {"message", "bad params"}, {"data", 42}}}}));
        } else if (method == "fixture/null") {
            writeBytes(frame(response(id, QJsonValue::Null)));
        } else if (method == "fixture/serverRequest") {
            writeBytes(frame({{"jsonrpc", "2.0"}, {"id", "server-1"}, {"method", "workspace/applyEdit"}, {"params", QJsonObject()}})
                       + frame(response(id, true)));
        } else if (method == "fixture/malformed") {
            writeBytes(frame({{"jsonrpc", "2.0"}, {"id", id}}));
        } else if (method == "fixture/raw") {
            writeBytes(params.toObject().value("bytes").toString().toUtf8());
        } else if (method == "fixture/crash") {
            return 17;
        } else if (method == "fixture/log") {
            std::fputs("fixture stderr\n", stderr); std::fflush(stderr);
            writeBytes(frame(response(id, true)));
        } else if (method == "textDocument/didOpen") {
            history.append(message);
            const auto uri = params.toObject().value("textDocument").toObject().value("uri");
            const QJsonObject diagnostic{{"range", QJsonObject{{"start", QJsonObject{{"line", 0}, {"character", 0}}},
                {"end", QJsonObject{{"line", 0}, {"character", 2}}}}}, {"severity", 2}, {"message", QStringLiteral("诊断😀")}};
            writeBytes(frame(notification("textDocument/publishDiagnostics",
                QJsonObject{{"uri", uri}, {"diagnostics", QJsonArray{diagnostic}}})));
        } else if (id.isUndefined() || method.isEmpty()) {
            history.append(message);
        } else {
            // Coalesced messages; results deliberately arrive in reverse order.
            if (method == "fixture/first") held.append(id);
            else if (method == "fixture/second") {
                writeBytes(frame(response(id, QJsonArray{2})) + frame(response(held.takeAt(0), QJsonArray{1})));
            } else writeBytes(frame(response(id, params)));
        }
    }
}

class LspTests : public QObject {
    Q_OBJECT
    LspServerOptions options(const QString &mode = {}) const
    {
        LspServerOptions result;
        result.program = QCoreApplication::applicationFilePath();
        result.arguments = QStringList{"--lsp-fixture", mode};
        result.requestTimeoutMs = 2000;
        result.shutdownTimeoutMs = 300;
        return result;
    }
private slots:
    void initTestCase()
    {
        qRegisterMetaType<LspClient::State>();
        qRegisterMetaType<QJsonValue>();
        qRegisterMetaType<QJsonObject>();
        qRegisterMetaType<QJsonArray>();
    }
    void lifecycleAndRequests()
    {
        LspClient client;
        QSignalSpy ready(&client, &LspClient::ready), replies(&client, &LspClient::responseReceived);
        QSignalSpy errors(&client, &LspClient::failed), logs(&client, &LspClient::serverLog);
        QCOMPARE(client.request("hover"), qint64(-1));
        QVERIFY(!client.notify("example"));
        auto settings = options("fragmented");
        QTemporaryDir root;
        settings.rootPath = root.path();
        settings.initializationOptions = QJsonObject{{"example", true}};
        QVERIFY(client.start(settings));
        QVERIFY(!client.start(settings));
        QCOMPARE(client.request("early"), qint64(-1));
        QTRY_COMPARE(ready.count(), 1);
        QVERIFY(client.state() == LspClient::State::Ready);
        QVERIFY(client.serverCapabilities().value("hoverProvider").toBool());
        const auto first = client.request("fixture/first"), second = client.request("fixture/second");
        QTRY_COMPARE(replies.count(), 2);
        QCOMPARE(replies.at(0).at(0).toLongLong(), second);
        QCOMPARE(replies.at(1).at(0).toLongLong(), first);
        QCOMPARE(qvariant_cast<QJsonValue>(replies.at(1).at(1)).toArray(), QJsonArray{1});
        client.request("fixture/null");
        QTRY_COMPARE(replies.count(), 3);
        QVERIFY(qvariant_cast<QJsonValue>(replies.last().at(1)).isNull());
        client.request("fixture/history");
        QTRY_COMPARE(replies.count(), 4);
        const auto initial = qvariant_cast<QJsonValue>(replies.last().at(1)).toObject().value("initialize").toObject();
        QCOMPARE(initial.value("rootUri").toString(), QUrl::fromLocalFile(root.path()).toString(QUrl::FullyEncoded));
        QCOMPARE(initial.value("initializationOptions"), settings.initializationOptions);
        QCOMPARE(initial.value("capabilities").toObject().value("general").toObject().value("positionEncodings").toArray(), QJsonArray{"utf-16"});
        client.request("fixture/log");
        QTRY_VERIFY(!logs.isEmpty());
        QCOMPARE(logs.first().first().toByteArray(), QByteArray("fixture stderr\n"));
        QCOMPARE(client.request("initialize"), qint64(-1));
        QVERIFY(!client.notify("exit"));
        client.shutdown();
        QCOMPARE(client.request("late"), qint64(-1));
        QTRY_VERIFY(client.state() == LspClient::State::Stopped);
        QCOMPARE(errors.count(), 0);
        QVERIFY(client.start(settings)); QTRY_COMPARE(ready.count(), 2);
        client.stop();
    }
    void documents_data()
    {
        QTest::addColumn<QString>("mode");
        for (const auto &mode : {"full", "incremental", "numeric"}) QTest::newRow(mode) << QString(mode);
    }
    void documents()
    {
        QFETCH(QString, mode);
        LspClient client;
        QSignalSpy ready(&client, &LspClient::ready), replies(&client, &LspClient::responseReceived);
        QSignalSpy diagnostics(&client, &LspClient::diagnosticsPublished);
        QVERIFY(client.start(options(mode))); QTRY_COMPARE(ready.count(), 1);
        const auto uri = QUrl::fromLocalFile(QStringLiteral("/tmp/文档 😀.cpp"));
        const auto source = QStringLiteral("第一行\n尾😀");
        QVERIFY(client.openDocument(uri, "cpp", source));
        QVERIFY(!client.openDocument(uri, "cpp", source));
        QTRY_COMPARE(diagnostics.count(), 1);
        QCOMPARE(diagnostics.first().first().toUrl(), uri);
        QCOMPARE(qvariant_cast<QJsonArray>(diagnostics.first().at(1)).first().toObject().value("message").toString(), QStringLiteral("诊断😀"));
        QVERIFY(!client.changeDocument(uri, "stale", 1));
        const auto changed = QStringLiteral("新内容😀\n");
        QVERIFY(client.changeDocument(uri, changed, 2));
        QCOMPARE(client.saveDocument(uri), mode != "numeric");
        const auto hover = client.hover(uri, 1, 3);
        QVERIFY(hover >= 0); QTRY_COMPARE(replies.count(), 1);
        const auto hoverParams = qvariant_cast<QJsonValue>(replies.last().at(1)).toObject();
        QCOMPARE(hoverParams.value("position").toObject(), (QJsonObject{{"line", 1}, {"character", 3}}));
        QVERIFY(client.completion(uri, 0, 0) >= 0);
        QVERIFY(client.definition(uri, 0, 0) >= 0);
        QCOMPARE(client.hover(uri, -1, 0), qint64(-1));
        QVERIFY(client.closeDocument(uri)); QVERIFY(!client.closeDocument(uri));
        const auto historyId = client.request("fixture/history");
        QTRY_COMPARE(replies.count(), 4);
        QCOMPARE(replies.last().first().toLongLong(), historyId);
        const auto history = qvariant_cast<QJsonValue>(replies.last().at(1)).toObject().value("history").toArray();
        QCOMPARE(history.size(), mode == "numeric" ? 3 : 4);
        const auto opened = history.at(0).toObject().value("params").toObject().value("textDocument").toObject();
        QCOMPARE(opened.value("text").toString(), source);
        QCOMPARE(opened.value("uri").toString(), uri.toString(QUrl::FullyEncoded));
        const auto update = history.at(1).toObject().value("params").toObject();
        QCOMPARE(update.value("textDocument").toObject().value("version").toInt(), 2);
        const auto change = update.value("contentChanges").toArray().first().toObject();
        QCOMPARE(change.value("text").toString(), changed);
        QCOMPARE(change.contains("range"), mode == "incremental");
        if (mode == "incremental")
            QCOMPARE(change.value("range").toObject().value("end").toObject(), (QJsonObject{{"line", 1}, {"character", 3}}));
        if (mode != "numeric")
            QCOMPARE(history.at(2).toObject().value("params").toObject().value("text").toString(), changed);
        client.shutdown(); QTRY_VERIFY(client.state() == LspClient::State::Stopped);
    }
    void noDocumentSyncAndServerRequests()
    {
        LspClient client; QSignalSpy ready(&client, &LspClient::ready), replies(&client, &LspClient::responseReceived);
        QVERIFY(client.start(options("none"))); QTRY_COMPARE(ready.count(), 1);
        const QUrl uri("file:///tmp/test.cpp");
        QVERIFY(client.openDocument(uri, "cpp", "x"));
        QVERIFY(!client.changeDocument(uri, "y", 2));
        QVERIFY(!client.saveDocument(uri));
        QVERIFY(client.closeDocument(uri));
        client.request("fixture/serverRequest"); QTRY_COMPARE(replies.count(), 1);
        client.request("fixture/history"); QTRY_COMPARE(replies.count(), 2);
        const auto history = qvariant_cast<QJsonValue>(replies.last().at(1)).toObject().value("history").toArray();
        QCOMPARE(history.size(), 1);
        QCOMPARE(history.first().toObject().value("id").toString(), QString("server-1"));
        QCOMPARE(history.first().toObject().value("error").toObject().value("code").toInt(), -32601);
    }
    void cancellationTimeoutAndErrors()
    {
        LspClient client; QSignalSpy ready(&client, &LspClient::ready), replies(&client, &LspClient::responseReceived);
        QSignalSpy errors(&client, &LspClient::requestFailed);
        auto settings = options(); settings.requestTimeoutMs = 200;
        QVERIFY(client.start(settings)); QTRY_COMPARE(ready.count(), 1);
        const auto canceled = client.request("fixture/hold");
        QVERIFY(client.cancelRequest(canceled)); QVERIFY(!client.cancelRequest(canceled));
        QCOMPARE(errors.count(), 1);
        QCOMPARE(qvariant_cast<QJsonObject>(errors.first().at(1)).value("code").toInt(), -32800);
        const auto timeout = client.request("fixture/hold");
        QTRY_COMPARE(errors.count(), 2);
        QCOMPARE(errors.last().first().toLongLong(), timeout);
        QCOMPARE(qvariant_cast<QJsonObject>(errors.last().at(1)).value("code").toInt(), -32001);
        const auto release = client.request("fixture/release");
        QTRY_COMPARE(replies.count(), 1);
        QCOMPARE(replies.first().first().toLongLong(), release);
        client.request("fixture/error"); QTRY_COMPARE(errors.count(), 3);
        QCOMPARE(qvariant_cast<QJsonObject>(errors.last().at(1)).value("data").toInt(), 42);
        const auto pending = client.request("fixture/hold");
        client.stop(); QCOMPARE(errors.count(), 4);
        QCOMPARE(errors.last().first().toLongLong(), pending);
        QCOMPARE(qvariant_cast<QJsonObject>(errors.last().at(1)).value("code").toInt(), -32000);
    }
    void protocolErrors_data()
    {
        QTest::addColumn<QString>("bytes");
        QTest::newRow("missing-length") << "Other: value\r\n\r\n";
        QTest::newRow("negative-length") << "Content-Length: -1\r\n\r\n";
        QTest::newRow("overflow-length") << "Content-Length: 999999999999999999999999\r\n\r\n";
        QTest::newRow("oversize-body") << "Content-Length: 16777217\r\n\r\n";
        QTest::newRow("duplicate-length") << "Content-Length: 2\r\nContent-Length: 2\r\n\r\n{}";
        QTest::newRow("invalid-json") << "Content-Length: 1\r\n\r\nx";
        QTest::newRow("oversize-header") << QString(8193, 'x');
        QTest::newRow("invalid-version") << QString::fromUtf8(frame({{"jsonrpc", "1.0"}}));
    }
    void protocolErrors()
    {
        QFETCH(QString, bytes);
        LspClient client; QSignalSpy ready(&client, &LspClient::ready), failures(&client, &LspClient::failed);
        QSignalSpy errors(&client, &LspClient::requestFailed);
        QVERIFY(client.start(options())); QTRY_COMPARE(ready.count(), 1);
        const auto id = client.request("fixture/raw", QJsonObject{{"bytes", bytes}});
        QTRY_COMPARE(failures.count(), 1);
        QVERIFY(client.state() == LspClient::State::Stopped);
        QCOMPARE(errors.count(), 1); QCOMPARE(errors.first().first().toLongLong(), id);
    }
    void lifecycleFailures_data()
    {
        QTest::addColumn<QString>("mode");
        for (const auto &mode : {"init-timeout", "init-error", "utf8", "fixture/crash", "fixture/malformed"})
            QTest::newRow(mode) << QString(mode);
    }
    void lifecycleFailures()
    {
        QFETCH(QString, mode);
        LspClient client; QSignalSpy ready(&client, &LspClient::ready), failures(&client, &LspClient::failed);
        QSignalSpy errors(&client, &LspClient::requestFailed);
        auto settings = options(mode); settings.requestTimeoutMs = 200;
        QVERIFY(client.start(settings));
        if (mode.startsWith("fixture/")) {
            QTRY_COMPARE(ready.count(), 1);
            QVERIFY(client.request(mode) >= 0);
        }
        QTRY_COMPARE(failures.count(), 1);
        QVERIFY(client.state() == LspClient::State::Stopped);
        QCOMPARE(errors.count(), mode.startsWith("fixture/") ? 1 : 0);
        QVERIFY(client.start(options())); QTRY_VERIFY(client.state() == LspClient::State::Ready);
    }
    void failedStartForcedShutdownAndDestruction()
    {
        LspClient client; QSignalSpy failures(&client, &LspClient::failed), ready(&client, &LspClient::ready);
        auto missing = options(); missing.program = "/nyaedit-missing/language-server";
        QVERIFY(client.start(missing)); QTRY_COMPARE(failures.count(), 1);
        QVERIFY(client.state() == LspClient::State::Stopped);
        QVERIFY(client.start(options("shutdown-timeout"))); QTRY_COMPARE(ready.count(), 1);
        client.shutdown(); client.shutdown();
        QTRY_VERIFY(client.state() == LspClient::State::Stopped);
        QCOMPARE(failures.count(), 1);
        {
            auto active = std::make_unique<LspClient>();
            QVERIFY(active->start(options())); QTRY_VERIFY(active->state() == LspClient::State::Ready);
        }
        QVERIFY(client.start(options())); client.stop();
        QTRY_VERIFY(client.state() == LspClient::State::Stopped);
    }
    void realClangdWhenConfigured()
    {
        const auto executable = qEnvironmentVariable("NYA_TEST_CLANGD_EXECUTABLE");
        if (executable.isEmpty()) QSKIP("Set NYA_TEST_CLANGD_EXECUTABLE for a real clangd integration check.");
        QTemporaryDir root; QVERIFY(root.isValid());
        const auto path = root.filePath("main.cpp");
        const QString text = "int answer = 42;\nint main() { return answer; }\n";
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(text.toUtf8()); file.close();
        LspClient client;
        QSignalSpy ready(&client, &LspClient::ready), replies(&client, &LspClient::responseReceived);
        QSignalSpy failures(&client, &LspClient::failed), errors(&client, &LspClient::requestFailed);
        LspServerOptions settings;
        settings.program = executable; settings.rootPath = root.path();
        QVERIFY(client.start(settings)); QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 10000);
        const auto uri = QUrl::fromLocalFile(path);
        QVERIFY(client.openDocument(uri, "cpp", text));
        const auto id = client.hover(uri, 0, 5); QVERIFY(id >= 0);
        QTRY_VERIFY_WITH_TIMEOUT(!replies.isEmpty() || !errors.isEmpty() || !failures.isEmpty(), 10000);
        QCOMPARE(errors.count(), 0); QCOMPARE(failures.count(), 0); QCOMPARE(replies.count(), 1);
        QCOMPARE(replies.first().first().toLongLong(), id);
        QVERIFY(qvariant_cast<QJsonValue>(replies.first().at(1)).toObject().contains("contents"));
        QVERIFY(client.closeDocument(uri)); client.shutdown();
        QTRY_VERIFY(client.state() == LspClient::State::Stopped);
        QCOMPARE(failures.count(), 0);
    }
};

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (app.arguments().value(1) == "--lsp-fixture") return fixture(app.arguments().value(2));
    LspTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "LspTests.moc"
