#include "TreeSitter.h"
#include <QCoreApplication>
#include <QFile>
#include <QSignalSpy>
#include <QTextStream>
#include <QThread>
#include <QtTest>
#include <memory>

using namespace nya;

// A deterministic child process exercising the actual QProcess/snapshot boundary.
static int fixture(const QStringList &arguments)
{
    if (!arguments.contains("--html") || !arguments.contains("--css-classes")) return 9;
    QString mode;
    const int config = arguments.indexOf("--config-path");
    if (config >= 0) mode = arguments.value(config + 1);
    if (mode == "fail") { QTextStream(stderr) << "No language found"; return 1; }
    if (mode == "no-language") {
        QTextStream(stderr) << "Warning: --css-classes is deprecated, use --style classes instead\nWarning: No language found for path snapshot.cpp\n";
        return 0;
    }
    if (mode == "mismatched-output") { QTextStream(stdout) << "<pre>different text</pre>"; return 0; }
    if (mode == "slow") QThread::msleep(800);
    if (mode == "timeout") QThread::msleep(10000);
    if (mode == "oversize") { QFile out; out.open(stdout, QIODevice::WriteOnly); out.write(QByteArray(33 * 1024 * 1024, 'x')); return 0; }
    QFile source(arguments.last());
    if (!source.open(QIODevice::ReadOnly)) return 2;
    const auto text = QString::fromUtf8(source.readAll());
    QTextStream(stdout) << "<pre><code><span class=\"string\">" << text.toHtmlEscaped() << "</span></code></pre>";
    return 0;
}

class TreeSitterTests : public QObject {
    Q_OBJECT
    LanguageSettings configured(const QString &mode = {}) const
    {
        return {true, QCoreApplication::applicationFilePath(), mode};
    }
private slots:
    void initTestCase() { qRegisterMetaType<QVector<nya::SyntaxSpan>>(); }
    void htmlFormatsAndUnicode()
    {
        const auto source = QStringLiteral("  中文😀\u00a0<&\n\"x\"\n");
        const auto firstLine = QStringLiteral("  中文😀\u00a0&lt;&amp;\n");
        const auto html = QStringLiteral("<head>ignored</head><table><tr><td class=line-number>1</td><td class=line><span class='comment'>%1</span></td></tr>\n"
                                         "<tr><td class=line-number>2</td><td class=\"line\"><span class=\"string special\">&quot;x&quot;\n</span></td></tr></table>").arg(firstLine).toUtf8();
        QVector<SyntaxSpan> spans;
        QVERIFY(TreeSitter::parseHtml(html, source, &spans)); QCOMPARE(spans.size(), 2);
        QCOMPARE(spans[0].start, 0); QCOMPARE(spans[0].length, source.indexOf('\n') + 1);
        QCOMPARE(spans[1].start, source.indexOf('\n') + 1); QCOMPARE(spans[1].capture, QString("string.special"));
        const auto pre = "<pre><code><span class='string'>" + source.toHtmlEscaped().toUtf8() + "</span></code></pre>";
        QVERIFY(TreeSitter::parseHtml(pre, source, &spans)); QCOMPARE(spans[0].length, source.size());
        QVERIFY(!TreeSitter::parseHtml(pre, source + "changed", &spans));
        QVERIFY(!TreeSitter::parseHtml("<pre><span>broken</pre>", "broken", &spans));
        QVERIFY(TreeSitter::parseHtml("<pre>x\n</pre>", "x", &spans));
        // CLI 0.27 pre/code output omits exactly one final source newline.
        QVERIFY(TreeSitter::parseHtml("<pre><code><span class='number'>42</span></code></pre>", "42\n", &spans));
        QCOMPARE(spans.size(), 1); QCOMPARE(spans[0].start, 0); QCOMPARE(spans[0].length, 2);
        QVERIFY(TreeSitter::parseHtml("<pre>x\n</pre>", "x\n\n", &spans));
        QVERIFY(TreeSitter::parseHtml("<pre>x  </pre>", "x  \n", &spans));
        QVERIFY(!TreeSitter::parseHtml("<pre>x</pre>", "x\n\n", &spans));
        QVERIFY(!TreeSitter::parseHtml("<pre>x</pre>", "x \n", &spans));
        QVERIFY(!TreeSitter::parseHtml("<pre>xy</pre>", "x\ny\n", &spans));
        QVERIFY(TreeSitter::parseHtml("<pre><span class='string'>a<span class='constant'>b</span>c</span></pre>", "abc", &spans));
        QCOMPARE(spans.size(), 3); QCOMPARE(spans[1].capture, QString("constant")); QCOMPARE(spans[2].capture, QString("string"));
    }
    void processReadsUnsavedUnicodeSnapshot()
    {
        TreeSitter runner; QSignalSpy ready(&runner, &TreeSitter::highlighted), errors(&runner, &TreeSitter::failed);
        const auto source = QStringLiteral("未保存😀\u00a0<&\n  next");
        runner.highlight(source, "cpp", configured());
        QTRY_COMPARE(ready.count(), 1); QCOMPARE(errors.count(), 0);
        const auto spans = qvariant_cast<QVector<SyntaxSpan>>(ready.at(0).at(0));
        QVERIFY(!spans.isEmpty()); QCOMPARE(spans.last().start + spans.last().length, source.size());
        runner.highlight("x", "py", configured()); QTRY_COMPARE(ready.count(), 2);
    }
    void staleRequestsAndDisabling()
    {
        TreeSitter runner; QSignalSpy ready(&runner, &TreeSitter::highlighted), errors(&runner, &TreeSitter::failed);
        runner.highlight("old", "cpp", configured("slow")); QTest::qWait(80);
        runner.highlight("new", "json", configured()); QTRY_COMPARE(ready.count(), 1);
        runner.highlight("cancelled", "py", configured("slow")); QTest::qWait(80); runner.cancel();
        QTest::qWait(900); QCOMPARE(ready.count(), 1); QCOMPARE(errors.count(), 0);
        auto disabled = configured(); disabled.treeSitterEnabled = false;
        runner.highlight("disabled", "cpp", disabled); QTest::qWait(80); QCOMPARE(ready.count(), 1);
    }
    void errorsAndSizeLimits()
    {
        TreeSitter runner; QSignalSpy ready(&runner, &TreeSitter::highlighted), errors(&runner, &TreeSitter::failed);
        runner.highlight("x", "cpp", {true, "/nyaedit-missing/tree-sitter", {}}); QCOMPARE(errors.count(), 1);
        runner.highlight("x", "cpp", configured("fail")); QTRY_COMPARE(errors.count(), 2);
        runner.highlight(QString(2 * 1024 * 1024 + 1, 'a'), "cpp", configured()); QCOMPARE(errors.count(), 3);
        runner.highlight("x", "cpp", configured("oversize")); QTRY_COMPARE(errors.count(), 4);
        runner.highlight("x", "cpp", configured("timeout")); QTRY_COMPARE_WITH_TIMEOUT(errors.count(), 5, 6500);
        QCOMPARE(ready.count(), 0);
    }
    void destroyingWhileRunning()
    {
        auto runner = std::make_unique<TreeSitter>();
        runner->highlight("closing", "cpp", configured("slow")); QTest::qWait(50); runner.reset();
    }
    void reportsMissingGrammarSeparatelyFromInvalidOutput()
    {
        TreeSitter runner; QSignalSpy ready(&runner, &TreeSitter::highlighted), errors(&runner, &TreeSitter::failed);
        runner.highlight("int value;", "cpp", configured("no-language")); QTRY_COMPARE(errors.count(), 1);
        const auto missingGrammar = errors.first().first().toString();
        QVERIFY(missingGrammar.contains(".cpp")); QVERIFY(missingGrammar.contains("parser-directories"));
        QVERIFY(missingGrammar.contains("No language found")); QVERIFY(!missingGrammar.contains("deprecated"));
        runner.highlight("int value;", "cpp", configured("mismatched-output")); QTRY_COMPARE(errors.count(), 2);
        QVERIFY(errors.last().first().toString().contains(QStringLiteral("高亮内容无法与文档匹配")));
        QCOMPARE(ready.count(), 0);
    }
    void realCliWhenConfigured_data()
    {
        QTest::addColumn<QString>("ending");
        QTest::newRow("no-final-newline") << QString();
        QTest::newRow("final-newline") << QString("\n");
        QTest::newRow("multiple-final-newlines") << QString("\n\n\n");
        QTest::newRow("trailing-spaces") << QString("  \n");
    }
    void realCliWhenConfigured()
    {
        QFETCH(QString, ending);
        const auto executable = qEnvironmentVariable("NYA_TEST_TREE_SITTER_EXECUTABLE");
        if (executable.isEmpty()) QSKIP("Set NYA_TEST_TREE_SITTER_EXECUTABLE and NYA_TEST_TREE_SITTER_CONFIG for a real CLI integration check.");
        TreeSitter runner; QSignalSpy ready(&runner, &TreeSitter::highlighted), errors(&runner, &TreeSitter::failed);
        const auto suffix = qEnvironmentVariable("NYA_TEST_TREE_SITTER_SUFFIX", "json");
        const auto text = (suffix == "c" ? QStringLiteral("// 中文😀\u00a0<&\nint main(void) { return 42; }")
                                          : QStringLiteral("{\"中文😀\": \"text\u00a0<&\", \"value\": 42, \"ok\": true}")) + ending;
        runner.highlight(text, suffix, {true, executable, qEnvironmentVariable("NYA_TEST_TREE_SITTER_CONFIG")});
        QTRY_VERIFY_WITH_TIMEOUT(!ready.isEmpty() || !errors.isEmpty(), 6500);
        QVERIFY2(errors.isEmpty(), errors.isEmpty() ? "" : qPrintable(errors.first().first().toString()));
        const auto spans = qvariant_cast<QVector<SyntaxSpan>>(ready.first().first());
        QVERIFY(!spans.isEmpty());
        bool numberFound = false;
        for (const auto &span : spans) {
            if (span.capture == "number") {
                QCOMPARE(text.mid(span.start, span.length), QString("42")); numberFound = true;
            }
        }
        QVERIFY(numberFound);
    }
};

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (app.arguments().value(1) == "highlight") return fixture(app.arguments());
    TreeSitterTests tests; return QTest::qExec(&tests, argc, argv);
}
#include "TreeSitterTests.moc"
