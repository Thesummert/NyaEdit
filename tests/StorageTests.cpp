#include "AppStorage.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

using namespace nya;

class StorageTests : public QObject {
    Q_OBJECT
    QTemporaryDir m_legacy;
    static void createFile(const QString &path, const QByteArray &bytes = "sample")
    {
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(bytes), qint64(bytes.size()));
    }
private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName("NyaEditStorageTests");
        QCoreApplication::setApplicationName("StorageTests");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_legacy.path());
    }
    void standardPaths()
    {
        const auto paths = StoragePaths::standard();
        QCOMPARE(paths.configDirectory, QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation));
        QCOMPARE(paths.dataDirectory, QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation));
        QVERIFY(paths.editorConfigFile().endsWith("/editor.ini"));
        QVERIFY(paths.systemConfigFile().endsWith("/system.ini"));
        QVERIFY(paths.historyFile().endsWith("/history.json"));
    }
    void settingsAndReservedSystemConfiguration()
    {
        QTemporaryDir dir; const auto paths = StoragePaths::underDirectory(dir.path());
        {
            AppStorage storage(paths); QVERIFY2(storage.initialize(), qPrintable(storage.lastError()));
            QVERIFY(QFileInfo::exists(paths.editorConfigFile())); QVERIFY(QFileInfo::exists(paths.systemConfigFile()));
            QVERIFY(QFileInfo::exists(paths.historyFile()));
            QCOMPARE(storage.value("editor/fontSize").toInt(), 13);
            QSignalSpy changed(&storage, &AppStorage::configurationChanged);
            QVERIFY(storage.setValue("editor/fontSize", 17)); QVERIFY(storage.setValue("editor/wrap", true));
            QVERIFY(storage.setValue("appearance/dark", false));
            QVERIFY(storage.setValue("window/geometry", QByteArray::fromHex("0001feff")));
            QCOMPARE(changed.count(), 4);
            QSignalSpy systemChanged(&storage, &AppStorage::systemConfigurationChanged);
            QVERIFY(storage.setSystemValue("future/option", QStringLiteral("中文配置"))); QCOMPARE(systemChanged.count(), 1);
        }
        AppStorage reloaded(paths); QVERIFY(reloaded.initialize());
        QCOMPARE(reloaded.value("editor/fontSize").toInt(), 17); QVERIFY(reloaded.value("editor/wrap").toBool());
        QVERIFY(!reloaded.value("appearance/dark").toBool());
        QCOMPARE(reloaded.value("window/geometry").toByteArray(), QByteArray::fromHex("0001feff"));
        QCOMPARE(reloaded.systemValue("future/option").toString(), QStringLiteral("中文配置"));
    }
    void historyRoundTripAndPromotion()
    {
        QTemporaryDir dir; const auto paths = StoragePaths::underDirectory(dir.path());
        const QString file = dir.filePath(QStringLiteral("单独打开.txt")), folder = dir.filePath(QStringLiteral("文件夹"));
        createFile(file); QVERIFY(QDir().mkpath(folder));
        {
            AppStorage storage(paths); QVERIFY(storage.initialize());
            QVERIFY(storage.record(file, RecentKind::File)); QVERIFY(storage.record(folder, RecentKind::Folder));
            QCOMPARE(storage.history().size(), 2); QCOMPARE(storage.history()[0].kind, RecentKind::Folder);
            QVERIFY(storage.record(QDir(dir.path()).filePath("./" + QFileInfo(file).fileName()), RecentKind::File));
            QCOMPARE(storage.history().size(), 2); QCOMPARE(storage.history()[0].path, QFileInfo(file).canonicalFilePath());
            QVERIFY(storage.history()[0].lastOpened.isValid());
            QVERIFY(!storage.record(dir.filePath("missing"), RecentKind::File));
            QVERIFY(!storage.record(folder, RecentKind::File));
        }
        QVERIFY(QFile::remove(file));
        AppStorage storage(paths); QVERIFY(storage.initialize());
        QCOMPARE(storage.history().size(), 2); QCOMPARE(storage.history()[0].kind, RecentKind::File);
        QCOMPARE(storage.history()[1].kind, RecentKind::Folder); // Missing files remain visible in history.
        QCOMPARE(storage.lastDirectory(), QFileInfo(dir.path()).canonicalFilePath());
    }
    void historyLimitAndClearPreservesSettings()
    {
        QTemporaryDir dir; const auto paths = StoragePaths::underDirectory(dir.path());
        AppStorage storage(paths); QVERIFY(storage.initialize());
        QVERIFY(storage.setValue("editor/fontSize", 19)); QVERIFY(storage.setSystemValue("future/enabled", true));
        for (int i = 0; i < 35; ++i) {
            const auto path = dir.filePath(QString("%1.txt").arg(i)); createFile(path); QVERIFY(storage.record(path, RecentKind::File));
        }
        QCOMPARE(storage.history().size(), AppStorage::HistoryLimit);
        QVERIFY(storage.history().first().path.endsWith("/34.txt")); QVERIFY(storage.history().last().path.endsWith("/5.txt"));
        QSignalSpy changed(&storage, &AppStorage::historyChanged);
        QVERIFY(storage.clearHistory()); QCOMPARE(changed.count(), 1);
        QVERIFY(storage.history().isEmpty()); QVERIFY(storage.lastDirectory().isEmpty());
        AppStorage reloaded(paths); QVERIFY(reloaded.initialize()); QVERIFY(reloaded.history().isEmpty());
        QCOMPARE(reloaded.value("editor/fontSize").toInt(), 19); QVERIFY(reloaded.systemValue("future/enabled").toBool());
    }
    void migrationAndClearingLegacyPaths()
    {
        QTemporaryDir dir; const auto paths = StoragePaths::underDirectory(dir.path());
        const auto file = dir.filePath("legacy.txt"); createFile(file);
        {
            QSettings legacy; legacy.clear(); legacy.setValue("editor/fontSize", 18);
            legacy.setValue("appearance/dark", false);
            legacy.setValue("files/recent", QStringList{file, dir.path(), file});
            legacy.setValue("files/lastDirectory", dir.path()); legacy.sync();
        }
        AppStorage storage(paths); QVERIFY(storage.initialize(true));
        QCOMPARE(storage.value("editor/fontSize").toInt(), 18); QVERIFY(!storage.value("appearance/dark").toBool());
        QCOMPARE(storage.history().size(), 2); QCOMPARE(storage.history()[1].kind, RecentKind::Folder);
        QVERIFY(storage.clearHistory());
        QSettings legacy; QVERIFY(!legacy.contains("files/recent")); QVERIFY(!legacy.contains("files/lastDirectory"));
        legacy.setValue("files/recent", QStringList{file}); legacy.sync();
        AppStorage reloaded(paths); QVERIFY(reloaded.initialize(true)); QVERIFY(reloaded.history().isEmpty());
        legacy.clear(); legacy.sync();
    }
    void corruptHistoryIsPreservedUntilExplicitClear()
    {
        QTemporaryDir dir; const auto paths = StoragePaths::underDirectory(dir.path());
        QVERIFY(QDir().mkpath(paths.dataDirectory));
        const QByteArray broken("{broken json"); createFile(paths.historyFile(), broken);
        const auto sample = dir.filePath("sample.txt"); createFile(sample);
        AppStorage storage(paths); QVERIFY(!storage.initialize()); QVERIFY(!storage.lastError().isEmpty());
        QVERIFY(!storage.record(sample, RecentKind::File));
        QFile file(paths.historyFile()); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), broken); file.close();
        QVERIFY(storage.clearHistory()); QVERIFY(storage.record(sample, RecentKind::File));
        AppStorage reloaded(paths); QVERIFY(reloaded.initialize()); QCOMPARE(reloaded.history().size(), 1);
    }
    void failedWriteDoesNotClearHistoryInMemory()
    {
        QTemporaryDir dir; const auto paths = StoragePaths::underDirectory(dir.path());
        const auto first = dir.filePath("first.txt"), second = dir.filePath("second.txt"); createFile(first); createFile(second);
        AppStorage storage(paths); QVERIFY(storage.initialize()); QVERIFY(storage.record(first, RecentKind::File));
        QVERIFY(QFile::remove(paths.historyFile())); QVERIFY(QDir().mkdir(paths.historyFile()));
        QSignalSpy changed(&storage, &AppStorage::historyChanged);
        QVERIFY(!storage.record(second, RecentKind::File)); QVERIFY(!storage.clearHistory());
        QCOMPARE(changed.count(), 0); QCOMPARE(storage.history().size(), 1);
        QCOMPARE(storage.history().first().path, QFileInfo(first).canonicalFilePath());
        QVERIFY(QDir().rmdir(paths.historyFile())); QVERIFY(storage.record(second, RecentKind::File));
        QCOMPARE(storage.history().size(), 2);
    }
};
QTEST_GUILESS_MAIN(StorageTests)
#include "StorageTests.moc"
