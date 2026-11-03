#pragma once
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QStringList>
#include <QVector>
#include <memory>

namespace nya {
struct TreeSitterParser {
    QString id;
    QString name;
    QString extensions;
    QString dependency;
    QString repository() const;
    QString directoryName() const { return QStringLiteral("tree-sitter-") + id; }
};

class TreeSitterManager : public QObject {
    Q_OBJECT
public:
    explicit TreeSitterManager(QObject *parent = nullptr, QString gitExecutable = QStringLiteral("git"));
    ~TreeSitterManager() override;
    static const QVector<TreeSitterParser> &catalog();
    static bool isInstalled(const TreeSitterParser &parser, const QString &directory);
    // Merge the parser root, preserving other directories, themes and extension keys.
    static bool saveConfiguration(const QString &configPath, const QString &parserDirectory, QString *error);
    bool isDownloading() const;
    void download(const QString &id, const QString &directory);
    void cancel();
signals:
    void progress(const QString &message);
    void installed(const QString &path);
    void failed(const QString &message);
private:
    struct Download;
    void cloneNext(const std::shared_ptr<Download> &download);
    void finishFailure(const QString &message);
    QString m_gitExecutable;
    QPointer<QProcess> m_process;
    std::shared_ptr<Download> m_download;
};
}
