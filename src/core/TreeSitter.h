#pragma once
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QVector>

namespace nya {
struct LanguageSettings {
    bool treeSitterEnabled = false;
    QString executable = QStringLiteral("tree-sitter");
    QString configPath;
};
struct SyntaxSpan {
    int start = 0;
    int length = 0;
    QString capture;
};
enum class TreeSitterState { Disabled, Idle, Pending, Working, Error };
struct TreeSitterStatus {
    TreeSitterState state = TreeSitterState::Disabled;
    QString detail = QStringLiteral("Tree-sitter 已关闭，可在设置 → 语言支持中开启。");
};

// Uses the installed CLI and its grammar/query configuration; no build-time dependency.
class TreeSitter : public QObject {
    Q_OBJECT
public:
    explicit TreeSitter(QObject *parent = nullptr) : QObject(parent) {}
    ~TreeSitter() override;
    void highlight(const QString &text, const QString &suffix, const LanguageSettings &settings);
    void cancel();
    static QString findExecutable(const QString &program);
    static bool parseHtml(const QByteArray &html, const QString &source, QVector<SyntaxSpan> *spans);
signals:
    void highlighted(const QVector<nya::SyntaxSpan> &spans);
    void failed(const QString &message);
private:
    QPointer<QProcess> m_process;
    quint64 m_generation = 0;
};
}
Q_DECLARE_METATYPE(nya::SyntaxSpan)
