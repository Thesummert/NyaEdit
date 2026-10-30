#include "TreeSitter.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QXmlStreamReader>
#include <memory>

namespace nya {
TreeSitter::~TreeSitter() { cancel(); }

QString TreeSitter::findExecutable(const QString &program)
{
    if (program.isEmpty()) return {};
    auto path = QStandardPaths::findExecutable(program);
#ifdef Q_OS_MAC
    // Finder-launched applications often do not inherit the user's shell PATH.
    if (path.isEmpty() && program == "tree-sitter")
        path = QStandardPaths::findExecutable(program, {"/opt/homebrew/bin", "/usr/local/bin", "/opt/local/bin"});
#endif
    return path;
}

void TreeSitter::cancel()
{
    ++m_generation;
    if (m_process) {
        auto *process = m_process.data(); m_process = nullptr;
        // Reap asynchronously even if the editor is being destroyed.
        process->setParent(nullptr); process->kill();
    }
}

bool TreeSitter::parseHtml(const QByteArray &html, const QString &source, QVector<SyntaxSpan> *spans)
{
    // CLI <= 0.25 emits a table; newer versions emit pre/code. Only read code cells.
    const auto output = QString::fromUtf8(html);
    QString fragment;
    const QRegularExpression cells(QStringLiteral(R"(<td\s+class\s*=\s*(?:"line"|'line'|line)\s*>([\s\S]*?)</td>)"));
    auto matches = cells.globalMatch(output);
    while (matches.hasNext()) fragment += matches.next().captured(1);
    if (fragment.isEmpty()) {
        const QRegularExpression code(QStringLiteral(R"(<pre\b[^>]*>(?:<code\b[^>]*>)?([\s\S]*?)(?:</code>)?</pre>)"));
        const auto match = code.match(output);
        if (!match.hasMatch()) return false;
        fragment = match.captured(1);
    }
    QXmlStreamReader xml("<root>" + fragment + "</root>");
    QVector<QString> captures;
    QString decoded;
    QVector<SyntaxSpan> result;
    while (!xml.atEnd()) {
        const auto token = xml.readNext();
        if (token == QXmlStreamReader::StartElement) {
            if (xml.name() == QLatin1String("span")) {
                auto capture = xml.attributes().value("class").toString().simplified();
                capture.replace(' ', '.');
                captures.append(capture.isEmpty() && !captures.isEmpty() ? captures.last() : capture);
            } else if (xml.name() != QLatin1String("root")) return false;
        } else if (token == QXmlStreamReader::EndElement && xml.name() == QLatin1String("span")) {
            if (captures.isEmpty()) return false;
            captures.removeLast();
        } else if (token == QXmlStreamReader::Characters) {
            const auto text = xml.text().toString();
            if (!captures.isEmpty() && !captures.last().isEmpty() && !text.isEmpty())
                result.append({int(decoded.size()), int(text.size()), captures.last()});
            decoded += text;
        }
    }
    // Older table output may add a final newline; CLI 0.27 pre/code output removes
    // exactly one. Keep validating every other character (including spaces/NBSP)
    // so all capture positions still refer to the unchanged source snapshot.
    const bool omittedFinalNewline = source.endsWith('\n') && decoded + '\n' == source;
    if (xml.hasError() || (decoded != source && decoded != source + '\n' && !omittedFinalNewline)) return false;
    for (auto &span : result) span.length = qMax(0, qMin(span.length, int(source.size()) - span.start));
    *spans = std::move(result);
    return true;
}

void TreeSitter::highlight(const QString &text, const QString &suffix, const LanguageSettings &settings)
{
    cancel();
    if (!settings.treeSitterEnabled || suffix.isEmpty() || text.isEmpty()) return;
    const auto executable = findExecutable(settings.executable);
    if (executable.isEmpty()) { emit failed(QStringLiteral("未找到 Tree-sitter 可执行文件，使用基础着色。")); return; }
    const auto bytes = text.toUtf8();
    if (bytes.size() > 2 * 1024 * 1024) { emit failed(QStringLiteral("文档超过 2 MiB，已跳过 Tree-sitter。")); return; }
    auto temporary = std::make_shared<QTemporaryDir>(QDir::tempPath() + "/nyaedit-syntax-XXXXXX");
    // Keep the extension for the CLI's language detection; never overwrite the user's file.
    QString extension = suffix.toLower();
    if (!QRegularExpression(QStringLiteral("^[a-z0-9_+-]{1,32}$")).match(extension).hasMatch()) return;
    const auto path = temporary->filePath("snapshot." + extension);
    QFile file(path);
    if (!temporary->isValid() || !file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) {
        emit failed(QStringLiteral("无法创建 Tree-sitter 文档快照，使用基础着色。")); return;
    }
    file.close();
    auto *process = new QProcess(this); m_process = process;
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), process, &QObject::deleteLater);
    connect(process, &QProcess::errorOccurred, process, [process](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) process->deleteLater();
    });
    process->setWorkingDirectory(temporary->path());
    const auto generation = m_generation;
    auto output = std::make_shared<QByteArray>();
    auto errors = std::make_shared<QByteArray>();
    connect(process, &QProcess::readyReadStandardOutput, this, [this, process, output, generation] {
        const auto chunk = process->readAllStandardOutput();
        if (generation != m_generation) return;
        if (output->size() + chunk.size() > 32 * 1024 * 1024) {
            cancel(); emit failed(QStringLiteral("Tree-sitter 输出过大，使用基础着色。"));
        } else output->append(chunk);
    });
    connect(process, &QProcess::readyReadStandardError, this, [process, errors] {
        const auto chunk = process->readAllStandardError();
        if (errors->size() < 4096) errors->append(chunk.left(4096 - errors->size()));
    });
    connect(process, &QProcess::errorOccurred, this, [this, generation](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            if (generation == m_generation) { m_process = nullptr; emit failed(QStringLiteral("无法启动 Tree-sitter，使用基础着色。")); }
        }
    });
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, process, temporary, output, errors, text, extension, generation](int exitCode, QProcess::ExitStatus status) {
        if (generation != m_generation) return;
        m_process = nullptr;
        output->append(process->readAllStandardOutput());
        errors->append(process->readAllStandardError().left(qMax(0, 4096 - int(errors->size()))));
        auto diagnostic = QString::fromUtf8(*errors).trimmed();
        diagnostic.remove(QStringLiteral("Warning: --css-classes is deprecated, use --style classes instead"));
        diagnostic = diagnostic.trimmed();
        QVector<SyntaxSpan> spans;
        if (diagnostic.contains("No language found", Qt::CaseInsensitive))
            emit failed(QStringLiteral("未找到适用于 .%1 文件的 Tree-sitter 语言解析器，请检查配置中的 parser-directories 和对应解析器的安装；使用基础着色。\n%2")
                        .arg(extension, diagnostic));
        else if (status != QProcess::NormalExit || exitCode != 0)
            emit failed(QStringLiteral("Tree-sitter 调用失败（退出码 %1），使用基础着色。\n%2").arg(exitCode).arg(diagnostic));
        else if (output->size() <= 32 * 1024 * 1024 && parseHtml(*output, text, &spans))
            emit highlighted(spans);
        else emit failed(QStringLiteral("Tree-sitter 返回的高亮内容无法与文档匹配，使用基础着色。\n%1").arg(diagnostic));
    });
    const QPointer<TreeSitter> owner(this);
    QTimer::singleShot(5000, process, [owner, generation] {
        if (owner && generation == owner->m_generation && owner->m_process) {
            owner->cancel(); emit owner->failed(QStringLiteral("Tree-sitter 超时，使用基础着色。"));
        }
    });
    QStringList arguments{"highlight", "--html"};
    // This flag is supported by older CLIs too and preserves semantic capture names.
    arguments << "--css-classes";
    if (!settings.configPath.isEmpty()) arguments << "--config-path" << settings.configPath;
    arguments << path;
    process->start(executable, arguments);
}
}
