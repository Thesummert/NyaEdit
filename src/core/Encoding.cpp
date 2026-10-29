#include "Encoding.h"

#include <unicode/ucnv.h>
#include <unicode/ucnv_err.h>
#include <unicode/ucsdet.h>
#include <memory>

namespace nya {
namespace {
using Converter = std::unique_ptr<UConverter, decltype(&ucnv_close)>;

QString canonical(QString name)
{
    name = name.toUpper();
    if (name == "GB2312" || name == "GBK") return QStringLiteral("GB18030");
    if (name == "SHIFT_JIS" || name == "SJIS") return QStringLiteral("Shift_JIS");
    if (name == "WINDOWS-1252" || name == "ISO-8859-1") return name.toLower();
    return name;
}

bool binaryText(const QString &text)
{
    int control = 0;
    const int count = qMin(8192, int(text.size()));
    for (int i = 0; i < count; ++i) {
        const ushort c = text.at(i).unicode();
        if (c == 0) return true;
        if (c < 32 && c != '\n' && c != '\r' && c != '\t' && c != '\f') ++control;
    }
    return control > qMax(2, count / 100);
}
}

QString Encoding::label() const
{
    return name + (bom ? QStringLiteral(" BOM") : QString());
}

QStringList EncodingService::supported()
{
    return {"UTF-8", "UTF-16LE", "UTF-16BE", "UTF-32LE", "UTF-32BE", "GB18030",
            "Big5", "Shift_JIS", "EUC-JP", "EUC-KR", "windows-1252", "windows-1251", "ISO-8859-1"};
}

QByteArray EncodingService::bomFor(const QString &name)
{
    const QString upper = name.toUpper();
    if (upper == "UTF-8") return QByteArray::fromHex("efbbbf");
    if (upper == "UTF-16LE") return QByteArray::fromHex("fffe");
    if (upper == "UTF-16BE") return QByteArray::fromHex("feff");
    if (upper == "UTF-32LE") return QByteArray::fromHex("fffe0000");
    if (upper == "UTF-32BE") return QByteArray::fromHex("0000feff");
    return {};
}

DecodeResult EncodingService::decode(const QByteArray &bytes, const Encoding &encoding)
{
    DecodeResult result;
    result.encoding = encoding;
    UErrorCode status = U_ZERO_ERROR;
    Converter converter(ucnv_open(encoding.name.toLatin1().constData(), &status), ucnv_close);
    if (U_FAILURE(status)) {
        result.error = QStringLiteral("不支持的编码：%1").arg(encoding.name);
        return result;
    }
    ucnv_setToUCallBack(converter.get(), UCNV_TO_U_CALLBACK_STOP, nullptr, nullptr, nullptr, &status);
    const QByteArray bom = bomFor(encoding.name);
    const int offset = !bom.isEmpty() && bytes.startsWith(bom) ? int(bom.size()) : 0;
    result.encoding.bom = offset != 0;
    const char *source = bytes.constData() + offset;
    const int length = int(bytes.size()) - offset;
    int32_t required = ucnv_toUChars(converter.get(), nullptr, 0, source, length, &status);
    if (status != U_BUFFER_OVERFLOW_ERROR && U_FAILURE(status)) {
        result.error = QStringLiteral("文件包含无效的 %1 字节序列，请尝试其他编码。").arg(encoding.name);
        return result;
    }
    status = U_ZERO_ERROR;
    result.text.resize(required);
    ucnv_toUChars(converter.get(), reinterpret_cast<UChar *>(result.text.data()), required,
                 source, length, &status);
    if (U_FAILURE(status)) result.error = QStringLiteral("解码失败：%1").arg(QString::fromLatin1(u_errorName(status)));
    return result;
}

DecodeResult EncodingService::detect(const QByteArray &bytes)
{
    // Check longer BOMs first: UTF-32LE begins with the UTF-16LE BOM.
    const QStringList unicode = {"UTF-32LE", "UTF-32BE", "UTF-8", "UTF-16LE", "UTF-16BE"};
    for (const QString &name : unicode) {
        if (bytes.startsWith(bomFor(name))) {
            auto result = decode(bytes, {name, true, 100, true});
            if (result.ok() && binaryText(result.text)) result.error = QStringLiteral("该文件包含二进制内容，无法作为文本打开。");
            return result;
        }
    }
    auto utf8 = decode(bytes, {"UTF-8", false, 100, true});
    if (utf8.ok() && !binaryText(utf8.text)) return utf8;

    UErrorCode status = U_ZERO_ERROR;
    std::unique_ptr<UCharsetDetector, decltype(&ucsdet_close)> detector(ucsdet_open(&status), ucsdet_close);
    if (U_SUCCESS(status)) {
        // Detection is bounded; every proposed codec is validated against the entire file.
        ucsdet_setText(detector.get(), bytes.constData(), qMin(int(bytes.size()), 256 * 1024), &status);
        int32_t count = 0;
        const UCharsetMatch **matches = ucsdet_detectAll(detector.get(), &count, &status);
        for (int i = 0; U_SUCCESS(status) && i < count; ++i) {
            const QString name = canonical(QString::fromLatin1(ucsdet_getName(matches[i], &status)));
            const int confidence = ucsdet_getConfidence(matches[i], &status);
            if (U_FAILURE(status)) break;
            auto result = decode(bytes, {name, false, confidence, true});
            if (result.ok() && !binaryText(result.text)) return result;
        }
    }
    DecodeResult result;
    result.error = QStringLiteral("无法可靠解码此文件，或文件包含二进制内容。可通过“文件 → 按指定编码打开”手动选择编码。");
    return result;
}

QByteArray EncodingService::encode(const QString &text, const Encoding &encoding, QString *error)
{
    error->clear();
    UErrorCode status = U_ZERO_ERROR;
    Converter converter(ucnv_open(encoding.name.toLatin1().constData(), &status), ucnv_close);
    if (U_FAILURE(status)) {
        *error = QStringLiteral("不支持的编码：%1").arg(encoding.name);
        return {};
    }
    ucnv_setFallback(converter.get(), false);
    ucnv_setFromUCallBack(converter.get(), UCNV_FROM_U_CALLBACK_STOP, nullptr, nullptr, nullptr, &status);
    const auto *source = reinterpret_cast<const UChar *>(text.utf16());
    const int32_t required = ucnv_fromUChars(converter.get(), nullptr, 0, source, int(text.size()), &status);
    if (status != U_BUFFER_OVERFLOW_ERROR && U_FAILURE(status)) {
        *error = QStringLiteral("存在 %1 无法表示的字符，保存已取消。请选择 UTF-8 或其他 Unicode 编码。").arg(encoding.name);
        return {};
    }
    status = U_ZERO_ERROR;
    QByteArray bytes(required, '\0');
    ucnv_fromUChars(converter.get(), bytes.data(), required, source, int(text.size()), &status);
    if (U_FAILURE(status)) {
        *error = QStringLiteral("编码失败：%1").arg(QString::fromLatin1(u_errorName(status)));
        return {};
    }
    if (encoding.bom) bytes.prepend(bomFor(encoding.name));
    return bytes;
}
}
