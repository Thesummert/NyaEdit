#include "Highlighter.h"
#include <QTextDocument>
#include <QTextBlock>
#include <algorithm>

namespace nya {
Highlighter::Highlighter(QTextDocument *document) : QSyntaxHighlighter(document)
{
    m_tokens = QRegularExpression(QStringLiteral(R"(("(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*')|\b(\d+(?:\.\d+)?(?:[eE][+-]?\d+)?)\b|\b([A-Za-z_]\w*)\b|(?://|\#).*$|/\*|\*/)"));
    setDark(true);
    m_syntaxTimer.setSingleShot(true); m_syntaxTimer.setInterval(250);
    connect(&m_syntaxTimer, &QTimer::timeout, this, [this] {
        rehighlight();
        if (!m_settings.treeSitterEnabled || m_suffix.isEmpty()) return;
        auto text = this->document()->toRawText();
        text.replace(QChar::ParagraphSeparator, QChar('\n'));
        text.replace(QChar::LineSeparator, QChar('\n'));
        setTreeSitterStatus(TreeSitterState::Pending, QStringLiteral("Tree-sitter 正在解析当前文档。"));
        m_treeSitter.highlight(text, m_suffix, m_settings);
    });
    connect(document, &QTextDocument::contentsChange, this, [this](int, int removed, int added) {
        if (removed || added) scheduleTreeSitter();
    });
    connect(&m_treeSitter, &TreeSitter::highlighted, this, [this](const QVector<SyntaxSpan> &spans) {
        m_spans = spans; m_treeReady = true; rehighlight();
        setTreeSitterStatus(TreeSitterState::Working, QStringLiteral("Tree-sitter 已成功解析当前文档，语法着色正常工作。"));
    });
    connect(&m_treeSitter, &TreeSitter::failed, this, [this](const QString &message) {
        setTreeSitterStatus(TreeSitterState::Error, message);
    });
}

void Highlighter::setTreeSitterStatus(TreeSitterState state, const QString &detail)
{
    if (m_treeStatus.state == state && m_treeStatus.detail == detail) return;
    m_treeStatus = {state, detail}; emit languageStatusChanged(detail);
}

void Highlighter::setLanguageSettings(const LanguageSettings &settings)
{
    m_settings = settings; scheduleTreeSitter(); rehighlight();
}

void Highlighter::scheduleTreeSitter()
{
    m_treeSitter.cancel(); m_treeReady = false; m_spans.clear(); m_syntaxTimer.stop();
    if (!m_settings.treeSitterEnabled)
        setTreeSitterStatus(TreeSitterState::Disabled, QStringLiteral("Tree-sitter 已关闭，可在设置 → 语言支持中开启。"));
    else if (m_suffix.isEmpty())
        setTreeSitterStatus(TreeSitterState::Idle, QStringLiteral("当前文档未指定语言，保存为带文件扩展名的代码文件后开始解析。"));
    else if (document()->characterCount() <= 1)
        setTreeSitterStatus(TreeSitterState::Idle, QStringLiteral("当前文档为空，等待输入内容。"));
    else {
        setTreeSitterStatus(TreeSitterState::Pending, QStringLiteral("Tree-sitter 等待更新当前文档。"));
        m_syntaxTimer.start();
    }
}

QColor Highlighter::captureColor(const QString &capture) const
{
    const auto kind = capture.section('.', 0, 0);
    if (kind == "comment") return m_comment;
    if (kind == "string") return m_string;
    if (kind == "number" || kind == "constant" || kind == "boolean") return m_number;
    if (kind == "keyword" || kind == "operator") return m_keyword;
    if (kind == "type" || kind == "constructor" || kind == "tag") return QColor(m_dark ? "#78d4c6" : "#157b72");
    if (kind == "function") return QColor(m_dark ? "#89b9f7" : "#2967ad");
    return {};
}

void Highlighter::setDark(bool dark)
{
    m_dark = dark;
    m_keyword = QColor(dark ? "#bda6f8" : "#8253bb");
    m_string = QColor(dark ? "#a4d7a0" : "#368650");
    m_comment = QColor(dark ? "#6a7b91" : "#8b96a4");
    m_number = QColor(dark ? "#ecc48d" : "#a9681c");
    rehighlight();
}

void Highlighter::setLanguage(const QString &suffix)
{
    const auto ext = suffix.toLower();
    m_suffix = ext;
    scheduleTreeSitter();
    if (QStringList{"cpp", "c", "h", "hpp", "cc", "cxx", "js", "ts", "jsx", "tsx", "java", "rs", "go", "css"}.contains(ext)) m_language = "c";
    else if (ext == "py" || ext == "sh" || ext == "yaml" || ext == "yml" || ext == "toml") m_language = "python";
    else if (ext == "json") m_language = "json";
    else if (ext == "md" || ext == "markdown") m_language = "markdown";
    else m_language.clear();
    const QString words = m_language == "python"
        ? QStringLiteral("and as assert async await break class continue def del elif else except False finally for from global if import in is lambda None nonlocal not or pass raise return True try while with yield")
        : QStringLiteral("alignas auto bool break case catch char class const constexpr continue default delete do double else enum explicit export extern false float for friend if import inline int let long namespace new noexcept nullptr null override private protected public register return short signed sizeof static struct switch template this throw true try typedef typename union unsigned using var virtual void volatile while function const fn mut pub impl use package func interface string");
    m_keywords.clear();
    for (const QString &word : words.split(' ')) m_keywords.insert(word);
    rehighlight();
}

void Highlighter::highlightBlock(const QString &text)
{
    if (m_treeReady) {
        setCurrentBlockState(0);
        if (text.size() > 12000) return;
        const int start = currentBlock().position(), end = start + int(text.size());
        auto it = std::lower_bound(m_spans.cbegin(), m_spans.cend(), start,
                                  [](const SyntaxSpan &span, int pos) { return span.start + span.length <= pos; });
        for (; it != m_spans.cend() && it->start < end; ++it) {
            const auto color = captureColor(it->capture);
            const int begin = qMax(start, it->start), finish = qMin(end, it->start + it->length);
            if (color.isValid() && finish > begin) setFormat(begin - start, finish - begin, color);
        }
        return;
    }
    if (m_language.isEmpty() || text.size() > 12000) return;
    if (m_language == "markdown") {
        if (text.startsWith('#')) { setFormat(0, int(text.size()), m_keyword); }
        else if (text.startsWith('>')) setFormat(0, int(text.size()), m_comment);
        else if (text.startsWith("```")) setFormat(0, int(text.size()), m_string);
        return;
    }
    int begin = 0;
    setCurrentBlockState(0);
    if (m_language == "c" && previousBlockState() == 1) {
        const int end = int(text.indexOf("*/"));
        if (end < 0) { setFormat(0, int(text.size()), m_comment); setCurrentBlockState(1); return; }
        setFormat(0, end + 2, m_comment); begin = end + 2;
    }
    auto it = m_tokens.globalMatch(text, begin);
    while (it.hasNext()) {
        const auto match = it.next();
        const int start = int(match.capturedStart()), length = int(match.capturedLength());
        const QString token = match.captured();
        if (m_language == "c" && token == "/*") {
            const int end = int(text.indexOf("*/", start + 2));
            if (end < 0) { setFormat(start, int(text.size()) - start, m_comment); setCurrentBlockState(1); break; }
            setFormat(start, end + 2 - start, m_comment);
            it = m_tokens.globalMatch(text, end + 2);
        } else if ((m_language == "c" && token.startsWith("//")) ||
                   (m_language == "python" && token.startsWith('#'))) {
            setFormat(start, length, m_comment);
        } else if (match.capturedLength(1) > 0) setFormat(start, length, m_string);
        else if (match.capturedLength(2) > 0) setFormat(start, length, m_number);
        else if (m_keywords.contains(token)) setFormat(start, length, m_keyword);
    }
}
}
