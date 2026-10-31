#include "Search.h"
#include <nya/core/Search.h>

namespace nya {
SearchResult Search::find(const QString &text, const SearchOptions &options, int limit)
{
    const auto found = core::Search::find(text.toStdU16String(),
        {options.pattern.toStdU16String(), options.caseSensitive, options.wholeWord, options.regex}, limit);
    SearchResult result;
    result.error = QString::fromStdString(found.error); result.truncated = found.truncated;
    for (const auto &match : found.matches) result.matches.append({match.start, match.length});
    return result;
}

QString Search::replacementForMatch(const QString &text, const SearchOptions &options,
    const QString &replacement, int matchIndex, QString *error)
{
    std::string message;
    const auto result = core::Search::replacementForMatch(text.toStdU16String(),
        {options.pattern.toStdU16String(), options.caseSensitive, options.wholeWord, options.regex},
        replacement.toStdU16String(), matchIndex, &message);
    *error = QString::fromStdString(message);
    return QString::fromStdU16String(result);
}

QString Search::replaceAll(const QString &text, const SearchOptions &options,
                           const QString &replacement, int *count, QString *error)
{
    std::string message;
    const auto result = core::Search::replaceAll(text.toStdU16String(),
        {options.pattern.toStdU16String(), options.caseSensitive, options.wholeWord, options.regex},
        replacement.toStdU16String(), count, &message);
    *error = QString::fromStdString(message);
    return QString::fromStdU16String(result);
}
}
