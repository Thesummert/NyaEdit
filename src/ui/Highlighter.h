#pragma once
#include <QSyntaxHighlighter>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>
#include "TreeSitter.h"

namespace nya {
class Highlighter : public QSyntaxHighlighter {
    Q_OBJECT
public:
    explicit Highlighter(QTextDocument *document);
    void setLanguage(const QString &suffix);
    void setDark(bool dark);
    void setLanguageSettings(const LanguageSettings &settings);
    LanguageSettings languageSettings() const { return m_settings; }
    TreeSitterStatus treeSitterStatus() const { return m_treeStatus; }
signals:
    void languageStatusChanged(const QString &status);
protected:
    void highlightBlock(const QString &text) override;
private:
    void scheduleTreeSitter();
    void setTreeSitterStatus(TreeSitterState state, const QString &detail);
    QColor captureColor(const QString &capture) const;
    TreeSitter m_treeSitter;
    QTimer m_syntaxTimer;
    LanguageSettings m_settings;
    TreeSitterStatus m_treeStatus;
    QString m_suffix;
    QVector<SyntaxSpan> m_spans;
    bool m_treeReady = false;
    bool m_dark = true;
    QString m_language;
    QSet<QString> m_keywords;
    QRegularExpression m_tokens;
    QColor m_keyword, m_string, m_comment, m_number;
};
}
