#pragma once

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QVariant>
#include <QVector>
#include <memory>

class QSettings;

namespace nya {
struct StoragePaths {
    QString configDirectory;
    QString dataDirectory;
    static StoragePaths standard();
    static StoragePaths underDirectory(const QString &root);
    QString editorConfigFile() const;
    QString systemConfigFile() const;
    QString historyFile() const;
};

enum class RecentKind { File, Folder };
struct RecentEntry {
    QString path;
    RecentKind kind = RecentKind::File;
    QDateTime lastOpened;
};

class AppStorage : public QObject {
    Q_OBJECT
public:
    explicit AppStorage(StoragePaths paths = {}, QObject *parent = nullptr);
    ~AppStorage() override;
    bool initialize(bool importLegacySettings = false);
    const StoragePaths &paths() const { return m_paths; }
    QString lastError() const { return m_lastError; }

    QVariant value(const QString &key, const QVariant &fallback = {}) const;
    bool setValue(const QString &key, const QVariant &value);
    // Reserved per-user application/system preferences, separate from editor settings.
    QVariant systemValue(const QString &key, const QVariant &fallback = {}) const;
    bool setSystemValue(const QString &key, const QVariant &value);

    const QVector<RecentEntry> &history() const { return m_history; }
    QString lastDirectory() const { return m_lastDirectory; }
    bool record(const QString &path, RecentKind kind);
    bool clearHistory();
    static constexpr int HistoryLimit = 30;
signals:
    void historyChanged();
    void configurationChanged(const QString &key);
    void systemConfigurationChanged(const QString &key);
    void errorOccurred(const QString &message);
private:
    bool fail(const QString &message);
    bool loadHistory();
    bool writeHistory(const QVector<RecentEntry> &entries, const QString &lastDirectory);
    bool syncSettings(QSettings &settings);
    StoragePaths m_paths;
    std::unique_ptr<QSettings> m_editor;
    std::unique_ptr<QSettings> m_system;
    QVector<RecentEntry> m_history;
    QString m_lastDirectory;
    QString m_lastError;
    bool m_historyReadable = true;
    bool m_importLegacy = false;
};
}
