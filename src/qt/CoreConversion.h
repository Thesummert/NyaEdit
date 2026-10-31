#pragma once
#include "Encoding.h"
#include <nya/core/Encoding.h>
#include <QString>
#include <filesystem>
namespace nya::qt {
inline core::Encoding toCore(const Encoding &e) { return {e.name.toStdString(), e.bom, e.confidence, e.detected}; }
inline Encoding fromCore(const core::Encoding &e) { return {QString::fromStdString(e.name), e.bom, e.confidence, e.detected}; }
inline std::filesystem::path path(const QString &s) { return std::filesystem::path(s.toStdU16String()); }
inline QString path(const std::filesystem::path &s) { return QString::fromStdU16String(s.u16string()); }
}
