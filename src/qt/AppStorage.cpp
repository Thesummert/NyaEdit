#include "AppStorage.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QSaveFile>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>

namespace nya {
namespace {
QString normalizedPath(const QString &path)
{
    const QFileInfo info(path);
    return QDir::cleanPath(info.canonicalFilePath().isEmpty() ? info.absoluteFilePath() : info.canonicalFilePath());
}
QString pathKey(QString path)
{
#ifdef Q_OS_WIN
    path = path.toCaseFolded();
#endif
    return path;
}
}

StoragePaths StoragePaths::standard()
{
    return {QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation),
            QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)};
}
StoragePaths StoragePaths::underDirectory(const QString &root)
{
    return {QDir(root).absoluteFilePath("config"), QDir(root).absoluteFilePath("data")};
}
QString StoragePaths::editorConfigFile() const { return QDir(configDirectory).filePath("editor.ini"); }
QString StoragePaths::systemConfigFile() const { return QDir(configDirectory).filePath("system.ini"); }
QString StoragePaths::historyFile() const { return QDir(dataDirectory).filePath("history.json"); }

AppStorage::AppStorage(StoragePaths paths, QObject *parent) : QObject(parent), m_paths(std::move(paths))
{
    const auto defaults = StoragePaths::standard();
    if (m_paths.configDirectory.isEmpty()) m_paths.configDirectory = defaults.configDirectory;
    if (m_paths.dataDirectory.isEmpty()) m_paths.dataDirectory = defaults.dataDirectory;
}
AppStorage::~AppStorage() = default;

bool AppStorage::fail(const QString &message)
{
    m_lastError = message; emit errorOccurred(message); return false;
}
bool AppStorage::syncSettings(QSettings &settings)
{
    settings.sync();
    return settings.status() == QSettings::NoError || fail(QStringLiteral("无法保存配置：%1").arg(settings.fileName()));
}

bool AppStorage::initialize(bool importLegacySettings)
{
    m_importLegacy = importLegacySettings;
    if (m_paths.configDirectory.isEmpty() || m_paths.dataDirectory.isEmpty() ||
        !QDir().mkpath(m_paths.configDirectory) || !QDir().mkpath(m_paths.dataDirectory))
        return fail(QStringLiteral("无法创建配置或历史记录目录：%1；%2").arg(m_paths.configDirectory, m_paths.dataDirectory));
    const bool newEditor = !QFileInfo::exists(m_paths.editorConfigFile());
    const bool newHistory = !QFileInfo::exists(m_paths.historyFile());
    m_editor = std::make_unique<QSettings>(m_paths.editorConfigFile(), QSettings::IniFormat);
    m_system = std::make_unique<QSettings>(m_paths.systemConfigFile(), QSettings::IniFormat);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    m_editor->setIniCodec("UTF-8"); m_system->setIniCodec("UTF-8");
#endif
    m_editor->setAtomicSyncRequired(true); m_system->setAtomicSyncRequired(true);
    const QMap<QString, QVariant> defaults{
        {"meta/schemaVersion", 1}, {"appearance/dark", true}, {"appearance/sidebar", true},
        {"animation/interface", true}, {"animation/cursor", true}, {"animation/selection", true}, {"animation/speed", 1.0},
        {"editor/fontFamily", QString()}, {"editor/fontSize", 13}, {"editor/wrap", false}, {"search/fuzzy", true},
        {"language/treeSitterEnabled", false}, {"language/treeSitterExecutable", QStringLiteral("tree-sitter")},
        {"language/treeSitterConfigPath", QString()}, {"language/lspEnabled", false},
        {"language/lspExecutable", QStringLiteral("clangd")}, {"language/lspArguments", QString()},
        {"language/lspExtensions", QStringLiteral("c,cpp,cc,cxx,h,hpp,hxx")}};
    for (auto it = defaults.cbegin(); it != defaults.cend(); ++it)
        if (!m_editor->contains(it.key())) m_editor->setValue(it.key(), it.value());
    if (!m_system->contains("meta/schemaVersion")) m_system->setValue("meta/schemaVersion", 1);
    QSettings legacy;
    if (importLegacySettings && newEditor) {
        const QStringList keys{"appearance/dark", "appearance/sidebar", "editor/fontSize", "editor/wrap", "window/geometry", "window/splitter"};
        for (const auto &key : keys) if (legacy.contains(key)) m_editor->setValue(key, legacy.value(key));
    }
    bool ok = syncSettings(*m_editor);
    if (!syncSettings(*m_system)) ok = false;
    if (!newHistory) {
        m_historyReadable = loadHistory();
        return m_historyReadable && ok;
    }
    QVector<RecentEntry> entries;
    QString lastDirectory;
    if (importLegacySettings) {
        QSet<QString> seen;
        for (const auto &path : legacy.value("files/recent").toStringList()) {
            if (!QDir::isAbsolutePath(path)) continue;
            const QString normalized = normalizedPath(path), key = pathKey(normalized);
            if (seen.contains(key)) continue;
            seen.insert(key);
            entries.append({normalized, QFileInfo(normalized).isDir() ? RecentKind::Folder : RecentKind::File, QDateTime::currentDateTimeUtc()});
            if (entries.size() == HistoryLimit) break;
        }
        lastDirectory = legacy.value("files/lastDirectory").toString();
        if (!lastDirectory.isEmpty() && !QDir::isAbsolutePath(lastDirectory)) lastDirectory.clear();
    }
    if (!writeHistory(entries, lastDirectory)) return false;
    m_history = std::move(entries); m_lastDirectory = lastDirectory;
    return ok;
}

QVariant AppStorage::value(const QString &key, const QVariant &fallback) const
{
    return m_editor ? m_editor->value(key, fallback) : fallback;
}
bool AppStorage::setValue(const QString &key, const QVariant &value)
{
    if (!m_editor) return fail(QStringLiteral("编辑器配置尚未初始化。"));
    m_editor->setValue(key, value);
    if (!syncSettings(*m_editor)) return false;
    emit configurationChanged(key); return true;
}
QVariant AppStorage::systemValue(const QString &key, const QVariant &fallback) const
{
    return m_system ? m_system->value(key, fallback) : fallback;
}
bool AppStorage::setSystemValue(const QString &key, const QVariant &value)
{
    if (!m_system) return fail(QStringLiteral("系统配置尚未初始化。"));
    m_system->setValue(key, value);
    if (!syncSettings(*m_system)) return false;
    emit systemConfigurationChanged(key); return true;
}

bool AppStorage::loadHistory()
{
    QFile file(m_paths.historyFile());
    if (!file.open(QIODevice::ReadOnly)) return fail(QStringLiteral("无法读取历史记录：%1").arg(file.fileName()));
    if (file.size() > 1024 * 1024) return fail(QStringLiteral("历史记录文件过大，已保留原文件：%1").arg(file.fileName()));
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(file.readAll(), &error);
    const auto root = document.object();
    if (error.error != QJsonParseError::NoError || !document.isObject() ||
        root.value("schemaVersion").toInt() != 1 || !root.value("entries").isArray())
        return fail(QStringLiteral("历史记录格式无效或版本不受支持，已保留原文件。可在设置中清除历史后重新记录。"));
    QVector<RecentEntry> entries;
    QSet<QString> seen;
    for (const auto &item : root.value("entries").toArray()) {
        const auto object = item.toObject();
        const auto kind = object.value("kind").toString();
        const auto path = object.value("path").toString();
        if ((kind != "file" && kind != "folder") || !QDir::isAbsolutePath(path)) continue;
        const auto normalized = normalizedPath(path), key = pathKey(normalized);
        if (seen.contains(key)) continue;
        seen.insert(key);
        entries.append({normalized, kind == "folder" ? RecentKind::Folder : RecentKind::File,
                        QDateTime::fromString(object.value("lastOpened").toString(), Qt::ISODateWithMs)});
        if (entries.size() == HistoryLimit) break;
    }
    m_history = std::move(entries);
    m_lastDirectory = root.value("lastDirectory").toString();
    if (!m_lastDirectory.isEmpty() && !QDir::isAbsolutePath(m_lastDirectory)) m_lastDirectory.clear();
    return true;
}

bool AppStorage::writeHistory(const QVector<RecentEntry> &entries, const QString &lastDirectory)
{
    QJsonArray array;
    for (const auto &entry : entries)
        array.append(QJsonObject{{"path", entry.path}, {"kind", entry.kind == RecentKind::Folder ? "folder" : "file"},
                                 {"lastOpened", entry.lastOpened.toUTC().toString(Qt::ISODateWithMs)}});
    const auto bytes = QJsonDocument(QJsonObject{{"schemaVersion", 1}, {"entries", array},
                                                {"lastDirectory", lastDirectory}}).toJson(QJsonDocument::Indented);
    QSaveFile file(m_paths.historyFile());
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return fail(QStringLiteral("无法保存历史记录：%1（%2）").arg(file.fileName(), file.errorString()));
    return true;
}

bool AppStorage::record(const QString &path, RecentKind kind)
{
    if (!m_historyReadable) return fail(QStringLiteral("历史记录暂不可写，请先在设置中清除损坏的历史记录。"));
    const QFileInfo info(path);
    if (!info.exists() || (kind == RecentKind::Folder ? !info.isDir() : !info.isFile())) return false;
    const auto normalized = normalizedPath(path), key = pathKey(normalized);
    QVector<RecentEntry> entries{{normalized, kind, QDateTime::currentDateTimeUtc()}};
    for (const auto &entry : m_history) {
        if (pathKey(entry.path) != key) entries.append(entry);
        if (entries.size() == HistoryLimit) break;
    }
    const QString directory = kind == RecentKind::Folder ? normalized : QFileInfo(normalized).absolutePath();
    if (!writeHistory(entries, directory)) return false;
    m_history = std::move(entries); m_lastDirectory = directory; emit historyChanged(); return true;
}

bool AppStorage::clearHistory()
{
    if (!writeHistory({}, {})) return false;
    m_history.clear(); m_lastDirectory.clear(); m_historyReadable = true;
    bool ok = true;
    if (m_importLegacy) {
        QSettings legacy;
        legacy.remove("files/recent"); legacy.remove("files/lastDirectory"); legacy.sync();
        if (legacy.status() != QSettings::NoError) ok = fail(QStringLiteral("当前历史记录已清空，但无法清除旧版历史存储。"));
    }
    emit historyChanged(); return ok;
}
}
