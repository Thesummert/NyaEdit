#include "TreeSitterManager.h"
#include "TreeSitter.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTimer>
#include <algorithm>

namespace nya {
namespace {
bool validRepository(const QString &path)
{
    QFile file(QDir(path).filePath("tree-sitter.json"));
    if (!file.open(QIODevice::ReadOnly)) return false;
    const auto grammars = QJsonDocument::fromJson(file.readAll()).object().value("grammars").toArray();
    if (grammars.isEmpty()) return false;
    for (const auto &entry : grammars) {
        const auto grammar = entry.toObject();
        if (!QFileInfo::exists(QDir(path).filePath(grammar.value("path").toString() + "/src/parser.c"))) return false;
        const auto highlights = grammar.value("highlights");
        const auto queries = highlights.isArray() ? highlights.toArray() : QJsonArray{highlights};
        if (queries.isEmpty()) return false;
        for (const auto &query : queries)
            if (!query.isString() || !QFileInfo(QDir(path).filePath(query.toString())).isFile()) return false;
    }
    return true;
}
}

QString TreeSitterParser::repository() const { return QStringLiteral("https://github.com/tree-sitter/tree-sitter-%1.git").arg(id); }

const QVector<TreeSitterParser> &TreeSitterManager::catalog()
{
    static const QVector<TreeSitterParser> parsers{
        {"c", "C", "c, h", {}}, {"cpp", "C++", "cpp, cc, cxx, hpp, hxx, h", "c"},
        {"json", "JSON", "json", {}}, {"python", "Python", "py", {}},
        {"rust", "Rust", "rs", {}}, {"go", "Go", "go", {}},
        {"javascript", "JavaScript / JSX", "js, mjs, cjs, jsx", {}},
        {"typescript", "TypeScript / TSX", "ts, tsx", "javascript"}};
    return parsers;
}

bool TreeSitterManager::isInstalled(const TreeSitterParser &parser, const QString &directory)
{
    return !directory.isEmpty() && validRepository(QDir(directory).filePath(parser.directoryName()));
}

bool TreeSitterManager::saveConfiguration(const QString &configPath, const QString &parserDirectory, QString *error)
{
    auto fail = [error](const QString &message) { if (error) *error = message; return false; };
    if (configPath.isEmpty() || parserDirectory.isEmpty() || !QDir::isAbsolutePath(configPath) || !QDir::isAbsolutePath(parserDirectory))
        return fail(QStringLiteral("Tree-sitter 配置文件和解析器存放目录必须为绝对路径。"));
    QJsonObject config;
    if (QFileInfo::exists(configPath)) {
        QFile file(configPath);
        if (!file.open(QIODevice::ReadOnly)) return fail(QStringLiteral("无法读取 Tree-sitter 配置：%1").arg(configPath));
        QJsonParseError parseError;
        const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject())
            return fail(QStringLiteral("Tree-sitter 配置不是有效的 JSON 对象，原文件已保留：%1").arg(configPath));
        config = document.object();
        if (config.contains("parser-directories") && !config.value("parser-directories").isArray())
            return fail(QStringLiteral("Tree-sitter 配置的 parser-directories 必须为数组，原文件已保留。"));
    }
    auto directories = config.value("parser-directories").toArray();
    const auto normalized = QDir::cleanPath(parserDirectory);
    bool found = false;
    for (const auto &directory : directories) {
        if (!directory.isString()) return fail(QStringLiteral("parser-directories 中的目录必须为字符串，原文件已保留。"));
        if (QDir::cleanPath(directory.toString()) == normalized) found = true;
    }
    if (!QDir().mkpath(QFileInfo(configPath).absolutePath()) || !QDir().mkpath(parserDirectory))
        return fail(QStringLiteral("无法创建 Tree-sitter 配置或解析器存放目录。"));
    if (found) return true;
    directories.append(normalized); config.insert("parser-directories", directories);
    QSaveFile file(configPath);
    const auto bytes = QJsonDocument(config).toJson(QJsonDocument::Indented);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return fail(QStringLiteral("无法保存 Tree-sitter 配置：%1（%2）").arg(configPath, file.errorString()));
    return true;
}

struct TreeSitterManager::Download {
    QTemporaryDir temporary;
    TreeSitterParser parser;
    QString destination;
    QStringList repositories;
    QStringList targets;
    int next = 0;
    bool cancelled = false;
    explicit Download(const QString &root) : temporary(QDir(root).filePath(".nyaedit-parser-XXXXXX")) {}
};

TreeSitterManager::TreeSitterManager(QObject *parent, QString gitExecutable)
    : QObject(parent), m_gitExecutable(std::move(gitExecutable)) {}
TreeSitterManager::~TreeSitterManager() { cancel(); }
bool TreeSitterManager::isDownloading() const { return bool(m_download); }

void TreeSitterManager::cancel()
{
    const auto download = m_download;
    if (download) download->cancelled = true;
    m_download.reset();
    if (m_process) {
        auto *process = m_process.data(); m_process = nullptr;
        process->setParent(nullptr);
        if (process->state() == QProcess::NotRunning) {
            if (download) download->temporary.remove();
            process->deleteLater();
        } else process->kill();
        // The finished connection retains the staging directory until Git has exited.
    } else if (download) download->temporary.remove();
}

void TreeSitterManager::finishFailure(const QString &message)
{
    cancel(); emit failed(message);
}

void TreeSitterManager::download(const QString &id, const QString &directory)
{
    if (isDownloading()) return;
    const auto &parsers = catalog();
    const auto it = std::find_if(parsers.cbegin(), parsers.cend(), [&id](const TreeSitterParser &parser) { return parser.id == id; });
    if (it == parsers.cend()) { emit failed(QStringLiteral("未知的 Tree-sitter 解析器。")); return; }
    if (directory.isEmpty() || !QDir::isAbsolutePath(directory) || !QDir().mkpath(directory)) {
        emit failed(QStringLiteral("无法创建解析器存放目录，请填写可写的绝对路径。")); return;
    }
    const auto destination = QDir(directory).filePath(it->directoryName());
    if (QFileInfo::exists(destination)) {
        emit failed(QStringLiteral("目标已存在，已保留原目录：%1。请选择其他存放目录，或先手动移走此目录。 ").arg(destination)); return;
    }
    if (TreeSitter::findExecutable(m_gitExecutable).isEmpty()) {
        emit failed(QStringLiteral("未找到 Git，请先安装 Git 后下载解析器。")); return;
    }
    auto download = std::make_shared<Download>(directory);
    if (!download->temporary.isValid()) { emit failed(QStringLiteral("无法创建解析器下载临时目录。")); return; }
    download->parser = *it; download->destination = destination;
    download->repositories << it->repository(); download->targets << download->temporary.filePath("repository");
    if (!it->dependency.isEmpty()) {
        TreeSitterParser dependency{it->dependency, {}, {}, {}};
        download->repositories << dependency.repository();
        download->targets << download->temporary.filePath("repository/node_modules/" + dependency.directoryName());
    }
    m_download = download; cloneNext(download);
}

void TreeSitterManager::cloneNext(const std::shared_ptr<Download> &download)
{
    if (m_download != download) return;
    if (download->next == download->repositories.size()) {
        if (!validRepository(download->targets.first())) {
            finishFailure(QStringLiteral("下载的解析器缺少生成的 parser.c、语言清单或高亮查询，未安装。")); return;
        }
        if (!QDir().rename(download->targets.first(), download->destination)) {
            finishFailure(QStringLiteral("无法安装解析器，目标目录可能已存在或不可写：%1").arg(download->destination)); return;
        }
        download->temporary.remove(); m_download.reset(); emit installed(download->destination); return;
    }
    const auto index = download->next++;
    QDir().mkpath(QFileInfo(download->targets.at(index)).absolutePath());
    auto *process = new QProcess(this); m_process = process;
    process->setProcessChannelMode(QProcess::MergedChannels);
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("GIT_TERMINAL_PROMPT", "0"); process->setProcessEnvironment(environment);
    auto output = std::make_shared<QByteArray>();
    connect(process, &QProcess::readyReadStandardOutput, this, [process, output] {
        output->append(process->readAllStandardOutput());
        if (output->size() > 8192) *output = output->right(8192);
    });
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), process, [process, download] {
        if (download->cancelled) download->temporary.remove();
        process->deleteLater();
    });
    connect(process, &QProcess::errorOccurred, process, [process, download](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) { download->temporary.remove(); process->deleteLater(); }
    });
    connect(process, &QProcess::errorOccurred, this, [this, download](QProcess::ProcessError error) {
        if (m_download == download && error == QProcess::FailedToStart)
            finishFailure(QStringLiteral("无法启动 Git 下载解析器。"));
    });
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, process, download, output](int code, QProcess::ExitStatus status) {
        if (m_download != download) return;
        m_process = nullptr;
        if (code != 0 || status != QProcess::NormalExit) {
            output->append(process->readAllStandardOutput());
            finishFailure(QStringLiteral("解析器下载失败：\n%1").arg(QString::fromUtf8(output->right(8192)).trimmed())); return;
        }
        cloneNext(download);
    });
    const QPointer<TreeSitterManager> owner(this);
    QTimer::singleShot(120000, process, [owner, download] {
        if (owner && owner->m_download == download) owner->finishFailure(QStringLiteral("解析器下载超时，请检查网络后重试。"));
    });
    emit progress(QStringLiteral("正在下载 %1（%2/%3）…").arg(download->parser.name).arg(download->next).arg(download->repositories.size()));
    process->start(TreeSitter::findExecutable(m_gitExecutable), {"clone", "--depth", "1", "--progress", "--", download->repositories.at(index), download->targets.at(index)});
}
}
