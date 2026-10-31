#include "Encoding.h"
#include "FileIO.h"
#include "FileSearch.h"
#include "Search.h"
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace nya;

class CoreTests : public QObject {
    Q_OBJECT
private slots:
    void replacementForMatch()
    {
        QString error;
        const auto expanded = Search::replacementForMatch("width=12 height=34",
            {"(?<key>\\w+)=(\\d+)", true, false, true}, "${key}:$2:$$", 1, &error);
        QVERIFY(error.isEmpty()); QCOMPARE(expanded, QString("height:34:$"));
    }

    void fuzzyFileSearch()
    {
        QTemporaryDir dir;
        QVERIFY(QDir().mkpath(dir.filePath("src/ui")));
        for (const QString &path : {QString("src/ui/MainWindow.cpp"), QStringLiteral("文件搜索配置.txt"),
                                    QString::fromUtf8("🌙notes🐾.md")}) {
            QFile file(dir.filePath(path)); QVERIFY(file.open(QIODevice::WriteOnly));
        }
        const auto fuzzy = FileSearch::findFiles(dir.path(), "MWCP");
        QCOMPARE(fuzzy.matches.size(), 1);
        QCOMPARE(fuzzy.matches.first().relativePath, QString("src/ui/MainWindow.cpp"));
        QVERIFY(FileSearch::findFiles(dir.path(), "MWCP", {}, 1000, FileSearchMode::Substring).matches.isEmpty());
        QCOMPARE(FileSearch::findFiles(dir.path(), "window", {}, 1000, FileSearchMode::Substring).matches.size(), 1);
        QCOMPARE(FileSearch::findFiles(dir.path(), " S\\U\\MW ").matches.size(), 1);
        QCOMPARE(FileSearch::findFiles(dir.path(), QStringLiteral("文搜配")).matches.size(), 1);
        QVERIFY(FileSearch::findFiles(dir.path(), QStringLiteral("配搜文")).matches.isEmpty());
        QCOMPARE(FileSearch::findFiles(dir.path(), QString::fromUtf8("🌙🐾")).matches.size(), 1);
        QVERIFY(FileSearch::findFiles(dir.path(), "www").matches.isEmpty());
        const auto limited = FileSearch::findFiles(dir.path(), "t", {}, 1);
        QCOMPARE(limited.matches.size(), 1); QVERIFY(limited.truncated);
        auto cancel = std::make_shared<std::atomic_bool>(true);
        QVERIFY(FileSearch::findFiles(dir.path(), "mw", cancel).canceled);
    }
    void recursiveFileSearch()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QVERIFY(QDir().mkpath(dir.filePath("src/deep")));
        QVERIFY(QDir().mkpath(dir.filePath(".git/objects")));
        const QStringList paths = {"src/deep/Settings.cpp", "src/settings.json", "notes.txt", ".settings", ".git/objects/settings"};
        for (const auto &path : paths) {
            QFile file(dir.filePath(path)); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("fixture");
        }
        const auto result = FileSearch::findFiles(dir.path(), "SETTINGS");
        QVERIFY(result.error.isEmpty()); QVERIFY(!result.truncated); QVERIFY(!result.canceled);
        QCOMPARE(result.matches.size(), 3);
        QCOMPARE(result.matches[0].relativePath, QString(".settings"));
        QCOMPARE(result.matches[1].relativePath, QString("src/deep/Settings.cpp"));
        QCOMPARE(result.matches[2].path, dir.filePath("src/settings.json"));
        const auto pathMatch = FileSearch::findFiles(dir.path(), " SRC\\DEEP ");
        QCOMPARE(pathMatch.matches.size(), 1);
        QCOMPARE(pathMatch.matches[0].relativePath, QString("src/deep/Settings.cpp"));
    }
    void fileSearchLimitsAndCancellation()
    {
        QTemporaryDir dir;
        for (int i = 0; i < 5; ++i) {
            QFile file(dir.filePath(QStringLiteral("文档%1.txt").arg(i)));
            QVERIFY(file.open(QIODevice::WriteOnly));
        }
        const auto limited = FileSearch::findFiles(dir.path(), QStringLiteral("文档"), {}, 2);
        QCOMPARE(limited.matches.size(), 2); QVERIFY(limited.truncated);
        const auto exact = FileSearch::findFiles(dir.path(), ".txt", {}, 5);
        QCOMPARE(exact.matches.size(), 5); QVERIFY(!exact.truncated);
        auto cancel = std::make_shared<std::atomic_bool>(true);
        const auto canceled = FileSearch::findFiles(dir.path(), ".txt", cancel);
        QVERIFY(canceled.canceled); QVERIFY(canceled.matches.isEmpty());
        QVERIFY(!FileSearch::findFiles(dir.filePath("missing"), "file").error.isEmpty());
        QVERIFY(!FileSearch::findFiles({}, "file").error.isEmpty());
        QVERIFY(FileSearch::findFiles(dir.path(), "  ").matches.isEmpty());
    }
    void fileSearchDoesNotFollowDirectorySymlinks()
    {
#ifdef Q_OS_UNIX
        QTemporaryDir dir, outside;
        QFile file(outside.filePath("outside.txt")); QVERIFY(file.open(QIODevice::WriteOnly)); file.close();
        QVERIFY(QFile::link(outside.path(), dir.filePath("linked")));
        QVERIFY(QFile::link(dir.path(), dir.filePath("loop")));
        const auto result = FileSearch::findFiles(dir.path(), ".txt");
        QVERIFY(result.error.isEmpty()); QVERIFY(result.matches.isEmpty());
#else
        QSKIP("Directory symlink creation requires platform-specific permissions.");
#endif
    }
    void unicodeRoundTrip_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<bool>("bom");
        for (const QString &name : {QString("UTF-8"), QString("UTF-16LE"), QString("UTF-16BE"), QString("UTF-32LE"), QString("UTF-32BE")}) {
            for (bool bom : {false, true}) QTest::newRow(qPrintable(name + (bom ? "+BOM" : ""))) << name << bom;
        }
    }
    void unicodeRoundTrip()
    {
        QFETCH(QString, name); QFETCH(bool, bom);
        const QString text = QString::fromUtf8("NyaEdit 你好，世界。日本語 🌙\n第二行\tNBSP:\xc2\xa0\n");
        QString error;
        const auto bytes = EncodingService::encode(text, {name, bom, 100, false}, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        const auto decoded = EncodingService::decode(bytes, {name, false, 100, false});
        QVERIFY2(decoded.ok(), qPrintable(decoded.error));
        QCOMPARE(decoded.text, text); QCOMPARE(decoded.encoding.bom, bom);
        if (bom || name == "UTF-8") {
            const auto detected = EncodingService::detect(bytes);
            QVERIFY2(detected.ok(), qPrintable(detected.error));
            QCOMPARE(detected.encoding.name, name); QCOMPARE(detected.text, text);
        }
    }
    void legacyEncodings_data()
    {
        QTest::addColumn<QString>("name"); QTest::addColumn<QString>("text");
        QTest::newRow("GB18030") << QString("GB18030") << QStringLiteral("你好，世界。简体中文编码测试。🌙");
        QTest::newRow("Big5") << QString("Big5") << QStringLiteral("你好，世界。繁體中文編碼測試。");
        QTest::newRow("Shift_JIS") << QString("Shift_JIS") << QStringLiteral("こんにちは、日本語のテキストです。");
        QTest::newRow("EUC-JP") << QString("EUC-JP") << QStringLiteral("こんにちは、日本語です。");
        QTest::newRow("EUC-KR") << QString("EUC-KR") << QStringLiteral("안녕하세요 세계");
        QTest::newRow("windows-1252") << QString("windows-1252") << QStringLiteral("Café déjà vu — €");
        QTest::newRow("windows-1251") << QString("windows-1251") << QStringLiteral("Привет мир");
        QTest::newRow("Latin1") << QString("ISO-8859-1") << QStringLiteral("Café déjà vu");
    }
    void legacyEncodings()
    {
        QFETCH(QString, name); QFETCH(QString, text); QString error;
        const auto bytes = EncodingService::encode(text, {name, false, 100, false}, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        const auto decoded = EncodingService::decode(bytes, {name, false, 100, false});
        QVERIFY2(decoded.ok(), qPrintable(decoded.error)); QCOMPARE(decoded.text, text);
    }
    void chineseAutoDetection()
    {
        const QString text = QStringLiteral("这是一个中文文本编辑器的编码检测测试。支持查找替换、自动编码判断与手动更改编码。轻量级跨平台的原生界面可以帮助我们高效地编辑文档。编码检测需要足够长的文本样本，才能根据字符出现的频率做出合理的判断。\n").repeated(12);
        QString error;
        const auto bytes = EncodingService::encode(text, {"GB18030", false, 100, false}, &error);
        QVERIFY(error.isEmpty());
        const auto result = EncodingService::detect(bytes);
        QVERIFY2(result.ok(), qPrintable(result.error));
        QCOMPARE(result.text, text); QCOMPARE(result.encoding.name, QString("GB18030"));
    }
    void strictConversion()
    {
        QString error;
        EncodingService::encode(QStringLiteral("你好 🌙"), {"windows-1252", false, 100, false}, &error);
        QVERIFY(!error.isEmpty());
        QVERIFY(!EncodingService::decode(QByteArray::fromHex("c328"), {"UTF-8", false, 100, false}).ok());
        QVERIFY(!EncodingService::decode(QByteArray::fromHex("fffea1"), {"UTF-16LE", false, 100, false}).ok());
        QVERIFY(!EncodingService::decode("abc", {"nonexistent-codec", false, 100, false}).ok());
        QVERIFY(!EncodingService::detect(QByteArray(100, '\0')).ok());
    }
    void emptyFile()
    {
        auto result = EncodingService::detect({});
        QVERIFY(result.ok()); QVERIFY(result.text.isEmpty()); QCOMPARE(result.encoding.name, QString("UTF-8"));
        QString error;
        QCOMPARE(EncodingService::encode({}, {"UTF-8", false, 100, false}, &error), QByteArray());
        QVERIFY(error.isEmpty());
    }
    void fileRoundTrip_data()
    {
        QTest::addColumn<QString>("name"); QTest::addColumn<int>("ending");
        for (const auto &name : {QString("UTF-8"), QString("UTF-16LE"), QString("GB18030")})
            for (int ending = 0; ending < 3; ++ending) QTest::newRow(qPrintable(name + QString::number(ending))) << name << ending;
    }
    void fileRoundTrip()
    {
        QFETCH(QString, name); QFETCH(int, ending);
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("中文文件.txt"));
        const QString text = QStringLiteral("第一行\n第二行\n"); QString error;
        QVERIFY2(saveFile(path, text, {name, name == "UTF-16LE", 100, false}, LineEnding(ending), &error), qPrintable(error));
        const auto loaded = loadFile(path, name);
        QVERIFY2(loaded.ok(), qPrintable(loaded.error));
        QCOMPARE(loaded.text, text); QCOMPARE(loaded.ending, LineEnding(ending)); QVERIFY(!loaded.mixedEndings);
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); const auto original = file.readAll(); file.close();
        QVERIFY(saveFile(path, loaded.text, loaded.encoding, loaded.ending, &error));
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), original);
    }
    void failedSavePreservesFile()
    {
        QTemporaryDir dir; const QString path = dir.filePath("original.txt"); QString error;
        QVERIFY(saveFile(path, "original", {}, LineEnding::LF, &error));
        QVERIFY(!saveFile(path, QStringLiteral("中文"), {"windows-1252", false, 100, false}, LineEnding::LF, &error));
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), QByteArray("original"));
        QVERIFY(!saveFile(dir.filePath("missing/sub/file.txt"), "text", {}, LineEnding::LF, &error));
        QVERIFY(!loadFile(dir.filePath("missing.txt")).ok());
    }
    void mixedEndings()
    {
        QTemporaryDir dir; QFile file(dir.filePath("mixed.txt")); QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("one\r\ntwo\nthree\rfour\r\n"); file.close();
        const auto loaded = loadFile(file.fileName());
        QVERIFY(loaded.ok()); QVERIFY(loaded.mixedEndings); QCOMPARE(loaded.ending, LineEnding::CRLF);
        QCOMPARE(loaded.text, QString("one\ntwo\nthree\nfour\n"));
    }
    void plainSearch()
    {
        SearchOptions options{"cat"};
        QCOMPARE(Search::find("Cat cat scatter", options).matches.size(), 3);
        options.wholeWord = true; QCOMPARE(Search::find("Cat cat scatter", options).matches.size(), 2);
        options.caseSensitive = true; QCOMPARE(Search::find("Cat cat scatter", options).matches.size(), 1);
        options.pattern = "a.b"; options.wholeWord = false;
        const auto result = Search::find("a.b axb", options);
        QCOMPARE(result.matches.size(), 1); QCOMPARE(result.matches.first().start, 0);
    }
    void unicodeOffsetsAndWords()
    {
        const auto result = Search::find(QStringLiteral("🌙你好\n你好"), {QStringLiteral("你好")});
        QCOMPARE(result.matches.size(), 2); QCOMPARE(result.matches[0].start, 2); QCOMPARE(result.matches[1].start, 5);
        QCOMPARE(Search::find(QStringLiteral("café caféine CAFÉ"), {QStringLiteral("café"), false, true, false}).matches.size(), 2);
    }
    void regexReplace()
    {
        int count = 0; QString error;
        const SearchOptions options{R"((?<key>\w+)=(\d+))", true, false, true};
        const auto result = Search::replaceAll("width=12 height=34", options, "${key}:$2:$$", &count, &error);
        QVERIFY(error.isEmpty()); QCOMPARE(count, 2); QCOMPARE(result, QString("width:12:$ height:34:$"));
        QCOMPARE(Search::replaceAll("a.b a.b", {"a.b"}, "$1", &count, &error), QString("$1 $1"));
        QCOMPARE(count, 2);
    }
    void regexZeroWidthAndMultiline()
    {
        const SearchOptions options{"^", true, false, true};
        const auto found = Search::find("one\ntwo", options); QCOMPARE(found.matches.size(), 2);
        int count = 0; QString error;
        QCOMPARE(Search::replaceAll("one\ntwo", options, "> ", &count, &error), QString("> one\n> two"));
        QCOMPARE(count, 2);
        QCOMPARE(Search::find("ab", {"(?=.)", true, false, true}).matches.size(), 2);
        QCOMPARE(Search::find("first\nsecond", {"first\\nsecond", true, false, true}).matches.size(), 1);
    }
    void searchLimitsAndInvalidRegex()
    {
        const auto found = Search::find(QString(25000, 'a'), {"a"});
        QCOMPARE(found.matches.size(), 20000); QVERIFY(found.truncated);
        int count = 0; QString error;
        QCOMPARE(Search::replaceAll(QString(25000, 'a'), {"a"}, "b", &count, &error), QString(25000, 'b')); QCOMPARE(count, 25000);
        QVERIFY(!Search::find("abc", {"[", false, false, true}).error.isEmpty());
        QVERIFY(Search::find("abc", {}).matches.isEmpty());
        QCOMPARE(Search::replaceAll("abc", {"[", false, false, true}, "x", &count, &error), QString("abc"));
        QVERIFY(!error.isEmpty());
    }
};
QTEST_GUILESS_MAIN(CoreTests)
#include "CoreTests.moc"
