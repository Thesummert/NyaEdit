#include "Encoding.h"
#include "CoreConversion.h"
namespace nya {
namespace {
DecodeResult convert(const core::DecodeResult &r) { return {QString::fromStdU16String(r.text), qt::fromCore(r.encoding), QString::fromStdString(r.error)}; }
}
QString Encoding::label() const { return QString::fromStdString(qt::toCore(*this).label()); }
QStringList EncodingService::supported() {
    QStringList result;
    for (const auto &name : core::EncodingService::supported()) result.append(QString::fromStdString(name));
    return result;
}
QByteArray EncodingService::bomFor(const QString &name) { return QByteArray::fromStdString(core::EncodingService::bomFor(name.toStdString())); }
DecodeResult EncodingService::decode(const QByteArray &bytes, const Encoding &e) { return convert(core::EncodingService::decode(bytes.toStdString(), qt::toCore(e))); }
DecodeResult EncodingService::detect(const QByteArray &bytes) { return convert(core::EncodingService::detect(bytes.toStdString())); }
QByteArray EncodingService::encode(const QString &text, const Encoding &e, QString *error) {
    std::string message;
    auto result = core::EncodingService::encode(text.toStdU16String(), qt::toCore(e), &message);
    *error = QString::fromStdString(message);
    return QByteArray::fromStdString(result);
}
}
