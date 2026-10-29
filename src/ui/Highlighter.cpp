#include "Highlighter.h"
#include <QTextDocument>

namespace nya {
Highlighter::Highlighter(QTextDocument *document) : QSyntaxHighlighter(document)
{
    m_tokens = QRegularExpression(QStringLiteral(R"(("(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*')|\b(\d+(?:\.\d+)?(?:[eE][+-]?\d+)?)\b|\b([A-Za-z_]\w*)\b|(?://|\#).*$|/\*|\*/)"));
    setDark(true);
}

void Highlighter::setDark(bool dark)
{
    m_keyword = QColor(dark ? "#bda6f8" : "#8253bb");
    m_string = QColor(dark ? "#a4d7a0" : "#368650");
    m_comment = QColor(dark ? "#6a7b91" : "#8b96a4");
    m_number = QColor(dark ? "#ecc48d" : "#a9681c");
    rehighlight();
}

void Highlighter::setLanguage(const QString &suffix)
{
    const auto ext = suffix.toLower();
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
