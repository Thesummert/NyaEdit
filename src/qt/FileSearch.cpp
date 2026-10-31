#include "FileSearch.h"
#include "Encoding.h"
#include "CoreConversion.h"
#include <nya/core/FileSearch.h>
namespace nya {
FileSearchResult FileSearch::findFiles(const QString &root, const QString &query,
    const std::shared_ptr<std::atomic_bool> &cancel, int limit, FileSearchMode mode) {
    const auto found = core::FileSearch::findFiles(qt::path(root), query.toStdU16String(), cancel, limit, static_cast<core::FileSearchMode>(mode));
    FileSearchResult result;
    result.error = QString::fromStdString(found.error);
    result.skippedDirectories = found.skippedDirectories;
    result.truncated = found.truncated; result.canceled = found.canceled;
    for (const auto &match : found.matches) result.matches.append({qt::path(match.path), qt::path(match.relativePath)});
    return result;
}
}
