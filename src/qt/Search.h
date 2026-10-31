#pragma once
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
    static SearchResult find(const QString &text, const SearchOptions &options, int limit = 20000);
    static QString replacementForMatch(const QString &text, const SearchOptions &options,
        const QString &replacement, int matchIndex, QString *error);
    static QString replaceAll(const QString &text, const SearchOptions &options,
                              const QString &replacement, int *count, QString *error);
};
}
