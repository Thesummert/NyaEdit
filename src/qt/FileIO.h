#pragma once
#include "Encoding.h"
#include <QDateTime>

namespace nya {
enum class LineEnding { LF, CRLF, CR };
QString lineEndingName(LineEnding ending);

struct LoadedFile {
    QString path;
    QString text;
    Encoding encoding;
    LineEnding ending = LineEnding::LF;
    bool mixedEndings = false;
    QDateTime modified;
    qint64 byteSize = 0;
    QString error;
    bool ok() const { return error.isEmpty(); }
};

constexpr qint64 MaxFileBytes = 128LL * 1024 * 1024;
LoadedFile loadFile(const QString &path, const QString &forcedEncoding = {});
bool saveFile(const QString &path, const QString &text, const Encoding &encoding,
              LineEnding ending, QString *error);
}
