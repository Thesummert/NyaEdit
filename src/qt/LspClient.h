#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QUrl>

namespace nya {
struct LspServerOptions {
    QString program;
    QStringList arguments;
    QString rootPath;
    QString workingDirectory;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    QJsonValue initializationOptions = QJsonValue::Null;
    int requestTimeoutMs = 10000;
    int shutdownTimeoutMs = 1500;
};

// One stdio language server per instance. Call from its QObject thread with an
// active event loop. Positions are zero-based UTF-16; document text uses LF.
class LspClient : public QObject {
    Q_OBJECT
public:
    enum class State { Stopped, Starting, Initializing, Ready, ShuttingDown };
    Q_ENUM(State)

    explicit LspClient(QObject *parent = nullptr);
    ~LspClient() override;
    State state() const { return m_state; }
    QJsonObject serverCapabilities() const { return m_capabilities; }
    bool start(const LspServerOptions &options);
    void shutdown(); // shutdown -> exit; forcibly stop after shutdownTimeoutMs.
    void stop();     // Immediately terminate and fail outstanding requests.

    // Returns -1 / false when not ready or the message cannot be queued.
    // Results retain their LSP JSON shape (including arrays and null).
    qint64 request(const QString &method, const QJsonValue &params = QJsonObject());
    bool notify(const QString &method, const QJsonValue &params = QJsonObject());
    bool cancelRequest(qint64 id);

    // Helpers honor the server's static textDocumentSync capabilities. For
    // incremental servers, changeDocument replaces the previous whole range.
    bool openDocument(const QUrl &uri, const QString &languageId, const QString &text, int version = 1);
    bool changeDocument(const QUrl &uri, const QString &text, int version);
    bool saveDocument(const QUrl &uri);
    bool closeDocument(const QUrl &uri);
    qint64 completion(const QUrl &uri, int line, int character);
    qint64 hover(const QUrl &uri, int line, int character);
    qint64 definition(const QUrl &uri, int line, int character);

signals:
    void stateChanged(nya::LspClient::State state);
    void ready();
    void responseReceived(qint64 id, const QJsonValue &result);
    // JSON-RPC error object. Local codes: -32000 stopped, -32001 timeout,
    // -32800 canceled. Late responses after cancellation/timeout are ignored.
    void requestFailed(qint64 id, const QJsonObject &error);
    void notificationReceived(const QString &method, const QJsonValue &params);
    void diagnosticsPublished(const QUrl &uri, const QJsonArray &diagnostics);
    void serverLog(const QByteArray &bytes); // Raw stderr, separate from protocol stdout.
    void failed(const QString &message);    // Terminal process/protocol error.

private:
    enum class RequestKind { User, Initialize, Shutdown };
    struct Document { QString text; int version; };
    void setState(State state);
    bool send(const QJsonObject &message);
    qint64 sendRequest(const QString &method, const QJsonValue &params, RequestKind kind);
    void initialize();
    void readOutput();
    void dispatch(const QJsonObject &message);
    void fail(const QString &message);
    qint64 requestPosition(const QString &method, const QUrl &uri, int line, int character);

    QPointer<QProcess> m_process;
    State m_state = State::Stopped;
    LspServerOptions m_options;
    QJsonObject m_capabilities;
    QByteArray m_input;
    int m_bodyLength = -1;
    qint64 m_nextId = 0;
    QHash<qint64, RequestKind> m_pending;
    QHash<QString, Document> m_documents;
    bool m_openClose = false;
    int m_syncKind = 0;
};
}
