#include "Editor.h"
#include "EditorLsp.h"
#include "Highlighter.h"
#include "LanguageSettingsDialog.h"
#include "LspClient.h"
#include "MainWindow.h"
#include <QAction>
#include <QCheckBox>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QStatusBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QToolButton>
#include <QtTest>

using namespace nya;

class LspWidgetTests : public QObject {
    Q_OBJECT
    QTemporaryDir m_storage;
    StoragePaths paths() const { return StoragePaths::underDirectory(m_storage.filePath(QTest::currentTestFunction())); }
    static QString fixtureProgram()
    {
        QString program = QCoreApplication::applicationDirPath() + "/nya_lsp_tests";
#ifdef Q_OS_WIN
        program += ".exe";
#endif
        return program;
    }
    bool configure(const QString &mode = "ui", const QString &program = {})
    {
        AppStorage storage(paths());
        return storage.initialize() && storage.setValue("language/lspEnabled", true)
            && storage.setValue("language/lspExecutable", program.isEmpty() ? fixtureProgram() : program)
            && storage.setValue("language/lspArguments", program.isEmpty() ? "--lsp-fixture " + mode : QString())
            && storage.setValue("language/lspExtensions", "cpp,h");
    }
    static bool writeFile(const QString &path, const QString &text)
    {
        QString error; return saveFile(path, text, {}, LineEnding::LF, &error);
    }
    static Editor *current(MainWindow &window) { return qobject_cast<Editor *>(window.findChild<QTabWidget *>()->currentWidget()); }
    static void focus(MainWindow &window, Editor *editor)
    {
        window.show(); window.activateWindow(); QApplication::setActiveWindow(&window); editor->setFocus();
    }
    static void end(Editor *editor)
    {
        auto cursor = editor->textCursor(); cursor.movePosition(QTextCursor::End); editor->setTextCursor(cursor);
    }
    static QJsonObject pos(int line, int character) { return {{"line", line}, {"character", character}}; }
private slots:
    void initTestCase()
    {
        QVERIFY(m_storage.isValid());
        qRegisterMetaType<QJsonValue>(); qRegisterMetaType<QJsonObject>();
    }
    void completionEditsAreAtomicAndValidated()
    {
        Editor editor; editor.show(); editor.setFocus();
        const auto original = QStringLiteral("//😀\npri\n");
        editor.setPlainText(original);
        auto cursor = editor.textCursor(); cursor.setPosition(editor.lspOffset(pos(1, 3))); editor.setTextCursor(cursor);
        const QJsonObject item{{"label", "printValue"}, {"textEdit", QJsonObject{
            {"range", QJsonObject{{"start", pos(1, 0)}, {"end", pos(1, 3)}}}, {"newText", "printValue"}}},
            {"additionalTextEdits", QJsonArray{QJsonObject{
                {"range", QJsonObject{{"start", pos(0, 0)}, {"end", pos(0, 0)}}}, {"newText", "#include <x>\n"}}}}};
        QVERIFY(editor.showCompletions(QJsonArray{item}));
        QTest::keyClick(&editor, Qt::Key_Tab);
        QCOMPARE(editor.contents(), QStringLiteral("#include <x>\n//😀\nprintValue\n"));
        QCOMPARE(editor.textCursor().blockNumber(), 2);
        QCOMPARE(editor.textCursor().positionInBlock(), 10);
        editor.undo(); QCOMPARE(editor.contents(), original);
        cursor = editor.textCursor(); cursor.setPosition(editor.lspOffset(pos(1, 3))); editor.setTextCursor(cursor);
        auto invalid = item;
        invalid.insert("additionalTextEdits", QJsonArray{QJsonObject{
            {"range", QJsonObject{{"start", pos(1, 0)}, {"end", pos(1, 2)}}}, {"newText", "overlap"}}});
        QVERIFY(editor.showCompletions(QJsonArray{invalid})); QTest::keyClick(&editor, Qt::Key_Return);
        QCOMPARE(editor.contents(), original);
        QCOMPARE(editor.lspOffset(pos(0, 3)), -1); // Between the emoji's two UTF-16 units.
        QCOMPARE(editor.lspOffset(pos(20, 0)), -1);
        QVERIFY(!editor.showCompletions(QJsonArray{QJsonObject{{"label", "print"}, {"insertTextFormat", 2}, {"insertText", "print($1)"}}}));
        QVERIFY(editor.showCompletions(QJsonArray{item}));
        editor.insertPlainText("x"); QVERIFY(!editor.findChild<QListWidget *>("lspCompletionPopup")->isVisible());
    }
    void completionKeyboardMouseAndAutomatic()
    {
        QVERIFY(configure()); QTemporaryDir project;
        const auto path = project.filePath("main.cpp"); QVERIFY(writeFile(path, "pri"));
        MainWindow window(nullptr, paths()); window.openFolder(project.path()); window.openPath(path);
        auto *editor = current(window); QTRY_VERIFY(!editor->loading); focus(window, editor); end(editor);
        auto *client = window.findChild<LspClient *>(); QVERIFY(client);
        QTRY_VERIFY(client->state() == LspClient::State::Ready); QTRY_VERIFY(editor->hasFocus());
        window.findChild<QAction *>("actionCompletion")->trigger();
        QTRY_VERIFY(editor->findChild<QListWidget *>("lspCompletionPopup") && editor->findChild<QListWidget *>("lspCompletionPopup")->isVisible());
        auto *popup = editor->findChild<QListWidget *>("lspCompletionPopup"); QCOMPARE(popup->count(), 2);
        QTest::keyClick(editor, Qt::Key_Down); QTest::keyClick(editor, Qt::Key_Return);
        QCOMPARE(editor->contents(), QString("printOther")); QVERIFY(!popup->isVisible());
        editor->undo(); QCOMPARE(editor->contents(), QString("pri"));
        editor->setPlainText(""); end(editor); QTest::keyClicks(editor, "pr");
        QTRY_VERIFY(popup->isVisible());
        QTest::keyClick(editor, Qt::Key_Escape); QVERIFY(!popup->isVisible());
        QTest::keyClick(editor, Qt::Key_Slash, Qt::AltModifier);
        QTRY_VERIFY(popup->isVisible());
        QTest::mouseClick(popup->viewport(), Qt::LeftButton, Qt::NoModifier, popup->visualItemRect(popup->item(0)).center());
        QCOMPARE(editor->contents(), QString("printValue"));
        QCOMPARE(window.findChild<QToolButton *>("lspStatus")->text(), QStringLiteral("LSP · 就绪"));
    }
    void completionSurvivesHighlightRefresh_data()
    {
        QTest::addColumn<bool>("pending");
        QTest::newRow("while-waiting-for-server") << true;
        QTest::newRow("while-list-is-open") << false;
    }
    void completionSurvivesHighlightRefresh()
    {
        QFETCH(bool, pending);
        QVERIFY(configure(pending ? "ui-delay" : "ui")); QTemporaryDir project;
        const auto path = project.filePath("main.cpp"); QVERIFY(writeFile(path, "pri"));
        MainWindow window(nullptr, paths()); window.openPath(path);
        auto *editor = current(window); QTRY_VERIFY(!editor->loading); focus(window, editor); end(editor);
        auto *client = window.findChild<LspClient *>(); QTRY_VERIFY(client->state() == LspClient::State::Ready);
        auto *highlighter = editor->document()->findChild<Highlighter *>(); QVERIFY(highlighter);
        QTRY_VERIFY(editor->hasFocus()); editor->requestCompletion();
        if (!pending)
            QTRY_VERIFY(editor->findChild<QListWidget *>("lspCompletionPopup") && editor->findChild<QListWidget *>("lspCompletionPopup")->isVisible());
        // Tree-sitter results and theme changes reformat the document without
        // changing its source text. They must preserve pending/visible completions.
        QSignalSpy textChanges(editor, &Editor::textChanged);
        highlighter->rehighlight();
        QVERIFY(!textChanges.isEmpty()); QCOMPARE(editor->contents(), QString("pri"));
        QTRY_VERIFY(editor->findChild<QListWidget *>("lspCompletionPopup") && editor->findChild<QListWidget *>("lspCompletionPopup")->isVisible());
        QTest::qWait(350);
        QVERIFY(editor->findChild<QListWidget *>("lspCompletionPopup")->isVisible());
        QTest::keyClick(editor, Qt::Key_Return);
        QCOMPARE(editor->contents(), QString("printValue"));
    }
    void definitionOpensAndSelects_data()
    {
        QTest::addColumn<QString>("mode");
        for (const auto &mode : {"ui", "ui-link", "ui-multiple"}) QTest::newRow(mode) << QString(mode);
    }
    void definitionOpensAndSelects()
    {
        QFETCH(QString, mode);
        QVERIFY(configure(mode)); QTemporaryDir project;
        const auto path = project.filePath("main.cpp"), target = project.filePath("definition.cpp");
        QVERIFY(writeFile(path, "target")); QVERIFY(writeFile(target, QStringLiteral("//😀\nint target = 1;\n")));
        MainWindow window(nullptr, paths()); window.openFolder(project.path()); window.openPath(path);
        auto *source = current(window); QTRY_VERIFY(!source->loading); focus(window, source); end(source);
        auto *client = window.findChild<LspClient *>(); QTRY_VERIFY(client->state() == LspClient::State::Ready);
        QTest::keyClick(source, Qt::Key_F12);
        if (mode == "ui-multiple") {
            QTRY_VERIFY(window.findChild<QMenu *>("definitionLocations"));
            auto *menu = window.findChild<QMenu *>("definitionLocations");
            QCOMPARE(menu->actions().size(), 2); menu->actions().first()->trigger(); menu->close();
        }
        QTRY_VERIFY(current(window) != source);
        auto *destination = current(window); QTRY_VERIFY(!destination->loading);
        QCOMPARE(destination->path, QFileInfo(target).canonicalFilePath());
        QTRY_COMPARE(destination->textCursor().selectedText(), QString("target"));
        QCOMPARE(destination->textCursor().blockNumber(), 1);
        // Ctrl/Command-click uses the same definition path.
        auto *tabs = window.findChild<QTabWidget *>(); tabs->setCurrentWidget(source); focus(window, source);
        auto cursor = source->textCursor(); cursor.setPosition(2); source->setTextCursor(cursor);
        QTest::mouseClick(source->viewport(), Qt::LeftButton, Qt::ControlModifier, source->cursorRect().center());
        if (mode == "ui-multiple") {
            QTRY_VERIFY(window.findChild<QMenu *>("definitionLocations"));
            auto *menu = window.findChild<QMenu *>("definitionLocations"); menu->actions().first()->trigger(); menu->close();
        }
        QTRY_VERIFY(current(window) == destination);
    }
    void staleResultsSyncSaveAndClose()
    {
        QVERIFY(configure("ui-delay")); QTemporaryDir project;
        const auto path = project.filePath("main.cpp"); QVERIFY(writeFile(path, "pri"));
        MainWindow window(nullptr, paths()); window.openFolder(project.path()); window.openPath(path);
        auto *editor = current(window); QTRY_VERIFY(!editor->loading); focus(window, editor); end(editor);
        auto *client = window.findChild<LspClient *>(); QTRY_VERIFY(client->state() == LspClient::State::Ready);
        editor->requestCompletion(); QTest::qWait(50); QTest::keyClick(editor, Qt::Key_Left); QTest::qWait(450);
        auto *popup = editor->findChild<QListWidget *>("lspCompletionPopup"); QVERIFY(!popup || !popup->isVisible());
        editor->requestDefinition(); QTest::qWait(50); window.newDocument(); QTest::qWait(450);
        QCOMPARE(window.findChild<QTabWidget *>()->count(), 2); QVERIFY(current(window)->path.isEmpty());
        window.findChild<QTabWidget *>()->setCurrentWidget(editor);
        editor->setPlainText(QStringLiteral("未保存😀\nprinted"));
        window.findChild<QAction *>("actionSave")->trigger(); QVERIFY(!editor->dirty());
        QSignalSpy replies(client, &LspClient::responseReceived);
        client->request("fixture/history"); QTRY_COMPARE(replies.count(), 1);
        auto history = qvariant_cast<QJsonValue>(replies.last().at(1)).toObject().value("history").toArray();
        bool changed = false, saved = false;
        for (const auto &value : history) {
            const auto object = value.toObject();
            if (object.value("method") == QJsonValue("textDocument/didChange")) {
                const auto change = object.value("params").toObject().value("contentChanges").toArray().first().toObject();
                QCOMPARE(change.value("text").toString(), editor->contents()); changed = true;
            }
            if (object.value("method") == QJsonValue("textDocument/didSave")) {
                QCOMPARE(object.value("params").toObject().value("text").toString(), editor->contents()); saved = true;
            }
        }
        QVERIFY(changed); QVERIFY(saved);
        window.findChild<QAction *>("actionCloseTab")->trigger();
        QTRY_COMPARE(window.findChild<QTabWidget *>()->count(), 1);
        client->request("fixture/history"); QTRY_COMPARE(replies.count(), 2);
        history = qvariant_cast<QJsonValue>(replies.last().at(1)).toObject().value("history").toArray();
        QCOMPARE(history.last().toObject().value("method").toString(), QString("textDocument/didClose"));
    }
    void settingsToggleCancelPersistAndFailure()
    {
        QTemporaryDir project; const auto path = project.filePath("main.cpp"); QVERIFY(writeFile(path, "pri"));
        {
            MainWindow window(nullptr, paths()); window.openPath(path); QTRY_VERIFY(!current(window)->loading);
            auto *client = window.findChild<LspClient *>(); auto *action = window.findChild<QAction *>("actionLanguageSettings");
            QVERIFY(client->state() == LspClient::State::Stopped);
            bool handled = false;
            QTimer::singleShot(0, [&] {
                auto *dialog = qobject_cast<LanguageSettingsDialog *>(QApplication::activeModalWidget());
                if (!dialog) return;
                dialog->findChild<QCheckBox *>("lspEnabled")->setChecked(true);
                dialog->findChild<QLineEdit *>("lspExecutable")->setText("/nyaedit-missing/lsp");
                dialog->accept(); handled = true;
            });
            action->trigger(); QVERIFY(handled);
            auto *indicator = window.findChild<QToolButton *>("lspStatus"); QTRY_COMPARE(indicator->text(), QStringLiteral("LSP · 异常"));
            QVERIFY(indicator->toolTip().contains(QStringLiteral("进程错误")));
            QVERIFY(!current(window)->isReadOnly()); QVERIFY(!current(window)->dirty());
            QTimer::singleShot(0, [&] {
                auto *dialog = qobject_cast<LanguageSettingsDialog *>(QApplication::activeModalWidget());
                dialog->findChild<QLineEdit *>("lspExecutable")->setText(fixtureProgram());
                dialog->findChild<QLineEdit *>("lspArguments")->setText("--lsp-fixture ui");
                dialog->findChild<QLineEdit *>("lspExtensions")->setText("cpp,h"); dialog->accept();
            }); action->trigger(); QTRY_VERIFY(client->state() == LspClient::State::Ready);
            QTimer::singleShot(0, [&] {
                auto *dialog = qobject_cast<LanguageSettingsDialog *>(QApplication::activeModalWidget());
                dialog->findChild<QCheckBox *>("lspEnabled")->setChecked(false); dialog->reject();
            }); action->trigger(); QVERIFY(client->state() == LspClient::State::Ready);
            QTimer::singleShot(0, [&] {
                auto *dialog = qobject_cast<LanguageSettingsDialog *>(QApplication::activeModalWidget());
                dialog->findChild<QCheckBox *>("lspEnabled")->setChecked(false); dialog->accept();
            }); action->trigger(); QVERIFY(client->state() == LspClient::State::Stopped);
            QCOMPARE(indicator->text(), QStringLiteral("LSP · 关闭"));
        }
        MainWindow reloaded(nullptr, paths()); reloaded.newDocument();
        const auto settings = current(reloaded)->languageSettings();
        QVERIFY(!settings.lspEnabled); QCOMPARE(settings.lspExecutable, fixtureProgram());
        QCOMPARE(settings.lspArguments, QString("--lsp-fixture ui")); QCOMPARE(settings.lspExtensions, QString("cpp,h"));
    }
    void realClangdCompletionAndDefinition()
    {
        const auto executable = qEnvironmentVariable("NYA_TEST_CLANGD_EXECUTABLE");
        if (executable.isEmpty()) QSKIP("Set NYA_TEST_CLANGD_EXECUTABLE to test the editor with clangd.");
        QVERIFY(configure({}, executable)); QTemporaryDir project;
        const auto path = project.filePath("main.cpp");
        const QString text = "int answer = 42;\nint main() { return ans; }\n";
        QVERIFY(writeFile(path, text));
        MainWindow window(nullptr, paths()); window.openFolder(project.path()); window.openPath(path);
        auto *editor = current(window); QTRY_VERIFY(!editor->loading); focus(window, editor);
        auto *client = window.findChild<LspClient *>(); QTRY_VERIFY_WITH_TIMEOUT(client->state() == LspClient::State::Ready, 10000);
        auto cursor = editor->textCursor(); cursor.setPosition(text.indexOf("ans;") + 3); editor->setTextCursor(cursor);
        QTRY_VERIFY(editor->hasFocus()); editor->requestCompletion();
        QTRY_VERIFY_WITH_TIMEOUT(editor->findChild<QListWidget *>("lspCompletionPopup") && editor->findChild<QListWidget *>("lspCompletionPopup")->isVisible(), 10000);
        auto *popup = editor->findChild<QListWidget *>("lspCompletionPopup");
        int row = -1;
        for (int i = 0; i < popup->count(); ++i) if (popup->item(i)->data(Qt::UserRole).toJsonObject().value("filterText").toString(popup->item(i)->data(Qt::UserRole).toJsonObject().value("label").toString()).startsWith("answer")) row = i;
        QVERIFY(row >= 0); popup->setCurrentRow(row); QTest::keyClick(editor, Qt::Key_Return);
        QVERIFY(editor->contents().contains("return answer;")); QVERIFY(editor->dirty());
        editor->requestDefinition(); QTRY_COMPARE_WITH_TIMEOUT(editor->textCursor().selectedText(), QString("answer"), 10000);
        QCOMPARE(editor->textCursor().blockNumber(), 0); QVERIFY(current(window) == editor);
        QVERIFY(editor->contents().contains("return answer;"));
    }
};
QTEST_MAIN(LspWidgetTests)
#include "LspWidgetTests.moc"
