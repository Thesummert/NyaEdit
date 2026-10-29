#pragma once
#include <QSyntaxHighlighter>
#include <QRegularExpression>
#include <QSet>

namespace nya {
class Highlighter : public QSyntaxHighlighter {
public:
    explicit Highlighter(QTextDocument *document);
    void setLanguage(const QString &suffix);
    void setDark(bool dark);
protected:
    void highlightBlock(const QString &text) override;
private:
    QString m_language;
    QSet<QString> m_keywords;
    QRegularExpression m_tokens;
    QColor m_keyword, m_string, m_comment, m_number;
};
}
