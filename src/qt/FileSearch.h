#pragma once

#include <QString>
#include <QVector>
#include <atomic>
#include <memory>

namespace nya {
enum class FileSearchMode { Substring, Fuzzy };

struct FileSearchMatch {
    QString path;
    QString relativePath;
};

struct FileSearchResult {
    QVector<FileSearchMatch> matches;
    QString error;
    int skippedDirectories = 0;
    bool truncated = false;
    bool canceled = false;
};

class FileSearch {
public:
    // Case-insensitive relative-path search. Fuzzy matches characters in order,
    // allowing gaps. Does not follow directory symlinks.
    static FileSearchResult findFiles(const QString &root, const QString &query,
                                     const std::shared_ptr<std::atomic_bool> &cancel = {},
                                     int limit = 1000, FileSearchMode mode = FileSearchMode::Fuzzy);
};
}
