#include "FileIO.h"
#include "CoreConversion.h"
#include <nya/core/FileIO.h>
#include <QFileInfo>
namespace nya {
QString lineEndingName(LineEnding ending) { return QString::fromStdString(core::lineEndingName(static_cast<core::LineEnding>(ending))); }
LoadedFile loadFile(const QString &path, const QString &forcedEncoding) {
    const auto file = core::loadFile(qt::path(path), forcedEncoding.toStdString());
    LoadedFile result;
    result.path = qt::path(file.path);
    result.text = QString::fromStdU16String(file.text);
    result.encoding = qt::fromCore(file.encoding);
    result.ending = static_cast<LineEnding>(file.ending);
    result.mixedEndings = file.mixedEndings;
    result.modified = QFileInfo(result.path).lastModified();
    result.byteSize = file.byteSize;
    result.error = QString::fromStdString(file.error);
    // This restriction belongs to the QTextDocument frontend, not the backend.
    if (result.ok() && (result.text.contains(QChar(0x2028)) || result.text.contains(QChar(0x2029)))) {
        result.text.clear();
        result.error = QStringLiteral("文件含 U+2028/U+2029 分隔符，当前编辑器无法无损保留，已取消打开。");
    }
    return result;
}
bool saveFile(const QString &path, const QString &text, const Encoding &encoding, LineEnding ending, QString *error) {
    std::string message;
    const bool ok = core::saveFile(qt::path(path), text.toStdU16String(), qt::toCore(encoding), static_cast<core::LineEnding>(ending), &message);
    *error = QString::fromStdString(message);
    return ok;
}
}
