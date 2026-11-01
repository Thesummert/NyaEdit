#pragma once
#include "LspClient.h"
#include "TreeSitter.h"
#include <QSet>
#include <QTimer>
#include <memory>
#include <vector>

namespace nya {
class Editor;

// Servers are started on demand; each document follows its first enabled
// extension match. Request IDs and failures are scoped to their server.
class EditorLsp : public QObject {
    Q_OBJECT
public:
    explicit EditorLsp(QObject *parent = nullptr);
    void configure(const LanguageSettings &settings, const QString &rootPath);
    void addEditor(Editor *editor);
    void removeEditor(Editor *editor);
    void refreshEditor(Editor *editor, bool saved = false);
    void setCurrentEditor(Editor *editor);
    QString statusText() const;
    QString statusDetail() const;
signals:
    void statusChanged();
    void message(const QString &text);
    void definitionsFound(nya::Editor *source, const QJsonArray &locations);
private:
    struct SyncedDocument { LspClient *client; QUrl uri; QString text; int version = 1; };
    struct Pending {
        QPointer<Editor> editor;
        QUrl uri;
        int version;
        int position;
        bool definition;
        bool automatic;
    };
    struct Server {
        LspClient *client;
        LspServerSettings settings;
        QString error;
        QByteArray log;
        QHash<qint64, Pending> pending;
    };
    void addServer();
    Server *serverFor(Editor *editor) const;
    bool eligible(Editor *editor) const;
    void ensureStarted(Server *server, Editor *editor);
    void cancelRequests(Editor *editor);
    void complete(Editor *editor, bool automatic);
    void requestFeature(Editor *editor, bool definition, bool automatic);
    void receive(Server *server, qint64 id, const QJsonValue &result);
    std::vector<std::unique_ptr<Server>> m_servers;
    LanguageSettings m_settings;
    QString m_rootPath;
    QSet<Editor *> m_editors;
    QHash<Editor *, SyncedDocument> m_documents;
    QPointer<Editor> m_current;
    QPointer<Editor> m_completionEditor;
    QTimer m_syncTimer;
    QTimer m_completionTimer;
};
}
