#pragma once
#include <QRegularExpression>
#include <QString>
#include <QVector>

namespace nya {
struct SearchOptions {
    QString pattern;
    bool caseSensitive = false;
    bool wholeWord = false;
    bool regex = false;
};
struct SearchMatch { int start; int length; };
struct SearchResult {
    QVector<SearchMatch> matches;
    QString error;
    bool truncated = false;
};
class Search {
public:
    static QRegularExpression expression(const SearchOptions &options);
    static SearchResult find(const QString &text, const SearchOptions &options, int limit = 20000);
    // Regex replacements support $1, ${name}, and $$ (a literal dollar sign).
    static QString expand(const QString &replacement, const QRegularExpressionMatch &match);
    static QString replaceAll(const QString &text, const SearchOptions &options,
                              const QString &replacement, int *count, QString *error);
};
}
