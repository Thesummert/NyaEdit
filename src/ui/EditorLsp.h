#pragma once
#include "LspClient.h"
#include "TreeSitter.h"
#include <QSet>
#include <QTimer>

namespace nya {
class Editor;

// One server per window/workspace; documents and requests follow their editor.
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
    struct SyncedDocument { QUrl uri; QString text; int version = 1; };
    struct Pending {
        QPointer<Editor> editor;
        QUrl uri;
        int version;
        int position;
        bool definition;
        bool automatic;
    };
    bool eligible(Editor *editor) const;
    void ensureStarted(Editor *editor);
    void cancelRequests(Editor *editor);
    void complete(Editor *editor, bool automatic);
    void requestFeature(Editor *editor, bool definition, bool automatic);
    void receive(qint64 id, const QJsonValue &result);
    LspClient *m_client;
    LanguageSettings m_settings;
    QString m_rootPath;
    QString m_error;
    QByteArray m_log;
    QSet<Editor *> m_editors;
    QHash<Editor *, SyncedDocument> m_documents;
    QHash<qint64, Pending> m_pending;
    QPointer<Editor> m_current;
    QPointer<Editor> m_completionEditor;
    QTimer m_syncTimer;
    QTimer m_completionTimer;
};
}
