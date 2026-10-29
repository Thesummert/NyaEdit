#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace nya {
struct Encoding {
    QString name = QStringLiteral("UTF-8");
    bool bom = false;
    int confidence = 100;
    bool detected = false;
    QString label() const;
};

struct DecodeResult {
    QString text;
    Encoding encoding;
    QString error;
    bool ok() const { return error.isEmpty(); }
};

class EncodingService {
public:
    static QStringList supported();
    static DecodeResult detect(const QByteArray &bytes);
    static DecodeResult decode(const QByteArray &bytes, const Encoding &encoding);
    static QByteArray encode(const QString &text, const Encoding &encoding, QString *error);
    static QByteArray bomFor(const QString &name);
};
}
