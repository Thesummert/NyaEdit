#include "FileIO.h"
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace nya {
QString lineEndingName(LineEnding ending)
{
    switch (ending) {
    case LineEnding::CRLF: return QStringLiteral("CRLF");
    case LineEnding::CR: return QStringLiteral("CR");
    default: return QStringLiteral("LF");
    }
}

LoadedFile loadFile(const QString &path, const QString &forcedEncoding)
{
    LoadedFile result;
    const QFileInfo info(path);
    result.path = info.canonicalFilePath();
    if (result.path.isEmpty()) result.path = info.absoluteFilePath();
    QFile file(result.path);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = file.errorString();
        return result;
    }
    if (file.size() > MaxFileBytes) {
        result.error = QStringLiteral("文件超过 128 MiB 的编辑上限。");
        return result;
    }
    const QByteArray bytes = file.read(MaxFileBytes + 1);
    if (file.error() != QFileDevice::NoError || bytes.size() > MaxFileBytes || !file.atEnd()) {
        result.error = QStringLiteral("文件读取失败或超过 128 MiB 上限：%1").arg(file.errorString());
        return result;
    }
    result.byteSize = bytes.size();
    result.modified = info.lastModified();
    auto decoded = forcedEncoding.isEmpty() ? EncodingService::detect(bytes)
                                           : EncodingService::decode(bytes, {forcedEncoding, false, 100, false});
    if (!decoded.ok()) {
        result.error = decoded.error;
        return result;
    }
    result.encoding = decoded.encoding;
    int crlf = 0, lf = 0, cr = 0;
    for (int i = 0; i < decoded.text.size(); ++i) {
        if (decoded.text.at(i) == '\r') {
            if (i + 1 < decoded.text.size() && decoded.text.at(i + 1) == '\n') { ++crlf; ++i; }
            else ++cr;
        } else if (decoded.text.at(i) == '\n') ++lf;
    }
    result.ending = crlf > 0 && crlf >= lf && crlf >= cr ? LineEnding::CRLF
                  : cr > lf ? LineEnding::CR : LineEnding::LF;
    result.mixedEndings = int(crlf > 0) + int(cr > 0) + int(lf > 0) > 1;
    decoded.text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    decoded.text.replace('\r', '\n');
    // QTextDocument treats these separators as paragraphs too. Reject instead of silently changing them.
    if (decoded.text.contains(QChar(0x2028)) || decoded.text.contains(QChar(0x2029))) {
        result.error = QStringLiteral("文件含 U+2028/U+2029 分隔符，当前编辑器无法无损保留，已取消打开。");
        return result;
    }
    result.text = std::move(decoded.text);
    return result;
}

bool saveFile(const QString &path, const QString &text, const Encoding &encoding,
              LineEnding ending, QString *error)
{
    QString output = text;
    if (ending == LineEnding::CRLF) output.replace(QStringLiteral("\n"), QStringLiteral("\r\n"));
    else if (ending == LineEnding::CR) output.replace('\n', '\r');
    const QByteArray bytes = EncodingService::encode(output, encoding, error);
    if (!error->isEmpty()) return false;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        *error = file.errorString();
        return false;
    }
    return true;
}
}
