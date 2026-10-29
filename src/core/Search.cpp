#include "Search.h"

namespace nya {
QRegularExpression Search::expression(const SearchOptions &options)
{
    QString pattern = options.regex ? options.pattern : QRegularExpression::escape(options.pattern);
    if (options.wholeWord)
        pattern = QStringLiteral("(?<![\\p{L}\\p{N}_])(?:%1)(?![\\p{L}\\p{N}_])").arg(pattern);
    auto flags = QRegularExpression::UseUnicodePropertiesOption | QRegularExpression::MultilineOption;
    if (!options.caseSensitive) flags |= QRegularExpression::CaseInsensitiveOption;
    return QRegularExpression(pattern, flags);
}

SearchResult Search::find(const QString &text, const SearchOptions &options, int limit)
{
    SearchResult result;
    if (options.pattern.isEmpty()) return result;
    const auto regex = expression(options);
    if (!regex.isValid()) { result.error = regex.errorString(); return result; }
    auto it = regex.globalMatch(text);
    while (it.hasNext()) {
        if (result.matches.size() >= limit) { result.truncated = true; break; }
        const auto match = it.next();
        result.matches.append({int(match.capturedStart()), int(match.capturedLength())});
    }
    return result;
}

QString Search::expand(const QString &replacement, const QRegularExpressionMatch &match)
{
    QString result;
    for (int i = 0; i < replacement.size(); ++i) {
        if (replacement.at(i) != '$' || i + 1 == replacement.size()) { result += replacement.at(i); continue; }
        const QChar next = replacement.at(i + 1);
        if (next == '$') { result += '$'; ++i; }
        else if (next.isDigit()) {
            int end = i + 1;
            while (end < replacement.size() && replacement.at(end).isDigit()) ++end;
            const int group = replacement.mid(i + 1, end - i - 1).toInt();
            if (group <= match.lastCapturedIndex()) result += match.captured(group);
            i = end - 1;
        } else if (next == '{') {
            const int end = int(replacement.indexOf('}', i + 2));
            if (end < 0) { result += '$'; continue; }
            result += match.captured(replacement.mid(i + 2, end - i - 2));
            i = end;
        } else result += '$';
    }
    return result;
}

QString Search::replaceAll(const QString &text, const SearchOptions &options,
                           const QString &replacement, int *count, QString *error)
{
    *count = 0;
    error->clear();
    if (options.pattern.isEmpty()) return text;
    const auto regex = expression(options);
    if (!regex.isValid()) { *error = regex.errorString(); return text; }
    QString output;
    output.reserve(text.size());
    int last = 0;
    auto it = regex.globalMatch(text);
    while (it.hasNext()) {
        const auto match = it.next();
        output += text.mid(last, match.capturedStart() - last);
        output += options.regex ? expand(replacement, match) : replacement;
        last = int(match.capturedEnd());
        ++*count;
    }
    output += text.mid(last);
    return output;
}
}
