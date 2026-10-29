#include "FileSearch.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QStringList>
#include <algorithm>

namespace nya {
namespace {
bool fuzzyMatches(const QString &path, const QVector<uint> &needle)
{
    int next = 0;
    for (const uint character : path.toCaseFolded().toUcs4()) {
        if (character == needle[next] && ++next == needle.size()) return true;
    }
    return false;
}
}

FileSearchResult FileSearch::findFiles(const QString &root, const QString &query,
                                     const std::shared_ptr<std::atomic_bool> &cancel, int limit, FileSearchMode mode)
{
    FileSearchResult result;
    const auto canceled = [&] { return cancel && cancel->load(std::memory_order_relaxed); };
    if (canceled()) { result.canceled = true; return result; }
    QString needle = query.trimmed();
    needle.replace('\\', '/');
    if (needle.isEmpty() || limit <= 0) return result;
    const auto foldedNeedle = needle.toCaseFolded().toUcs4();
    const QFileInfo rootInfo(root);
    if (root.isEmpty() || !rootInfo.isDir() || !rootInfo.isReadable()) {
        result.error = QStringLiteral("文件夹不存在或无法读取。");
        return result;
    }
    const QDir directory(rootInfo.absoluteFilePath());
    QStringList pending{directory.absolutePath()};
    while (!pending.isEmpty()) {
        if (canceled()) { result.canceled = true; return result; }
        const QString path = pending.takeLast();
        if (!QFileInfo(path).isReadable()) { ++result.skippedDirectories; continue; }
        QDirIterator entries(path, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
        while (entries.hasNext()) {
            if (canceled()) { result.canceled = true; return result; }
            entries.next();
            const QFileInfo info = entries.fileInfo();
            if (info.isDir()) {
                const auto name = info.fileName().toLower();
                if (!info.isSymLink() && name != ".git" && name != ".hg" && name != ".svn")
                    pending.append(info.absoluteFilePath());
                continue;
            }
            if (!info.isFile()) continue;
            const QString relative = directory.relativeFilePath(info.absoluteFilePath());
            if (mode == FileSearchMode::Fuzzy ? !fuzzyMatches(relative, foldedNeedle)
                                             : !relative.contains(needle, Qt::CaseInsensitive)) continue;
            if (result.matches.size() >= limit) { result.truncated = true; break; }
            result.matches.append({info.absoluteFilePath(), relative});
        }
        if (result.truncated) break;
    }
    std::sort(result.matches.begin(), result.matches.end(), [](const FileSearchMatch &a, const FileSearchMatch &b) {
        const int order = QString::compare(a.relativePath, b.relativePath, Qt::CaseInsensitive);
        return order == 0 ? a.relativePath < b.relativePath : order < 0;
    });
    return result;
}
}
