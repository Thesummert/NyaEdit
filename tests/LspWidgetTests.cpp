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
#include <QPushButton>
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
    static LspServerSettings server(const QString &name, const QString &extensions, const QString &mode = "ui")
    {
        return {name, fixtureProgram(), "--lsp-fixture " + mode, extensions, true};
    }
    static QJsonObject history(LspClient *client)
    {
        QSignalSpy replies(client, &LspClient::responseReceived);
        if (client->request("fixture/history") < 0 || !replies.wait(3000)) return {};
        return qvariant_cast<QJsonValue>(replies.last().at(1)).toObject();
    }
    static QJsonObject lastDocument(const QJsonObject &snapshot)
    {
        return snapshot.value("history").toArray().last().toObject().value("params").toObject().value("textDocument").toObject();
    }
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
        QVERIFY(!settings.lspEnabled); QCOMPARE(settings.lspServers.first().executable, fixtureProgram());
        QCOMPARE(settings.lspServers.first().arguments, QString("--lsp-fixture ui")); QCOMPARE(settings.lspServers.first().extensions, QString("cpp,h"));
    }
    void multipleServersRouteDocumentsAndRequests()
    {
        QTemporaryDir project;
        Editor cpp, python;
        cpp.path = project.filePath("main.CPP"); cpp.setPlainText("pri"); end(&cpp);
        python.path = project.filePath("main.py"); python.setPlainText("pri"); end(&python);
        EditorLsp lsp; lsp.addEditor(&cpp); lsp.addEditor(&python);
        LanguageSettings settings; settings.lspEnabled = true;
        settings.lspServers = {server("C++", "CPP; h", "ui-delay"), server("Python", "py"), server("Rust", "rs")};
        lsp.configure(settings, project.path()); lsp.setCurrentEditor(&cpp);
        const auto clients = lsp.findChildren<LspClient *>(); QCOMPARE(clients.size(), 3);
        auto *cppClient = clients.at(0), *pythonClient = clients.at(1);
        QTRY_VERIFY(cppClient->state() == LspClient::State::Ready);
        QTRY_VERIFY(pythonClient->state() == LspClient::State::Ready);
        QCOMPARE(clients.at(2)->state(), LspClient::State::Stopped); // Unused languages start on demand.
        const auto cppHistory = history(cppClient), pythonHistory = history(pythonClient);
        QCOMPARE(cppHistory.value("history").toArray().size(), 1);
        QCOMPARE(pythonHistory.value("history").toArray().size(), 1);
        QCOMPARE(lastDocument(cppHistory).value("languageId").toString(), QString("cpp"));
        QCOMPARE(lastDocument(pythonHistory).value("languageId").toString(), QString("python"));
        QCOMPARE(lastDocument(cppHistory).value("uri").toString(), QUrl::fromLocalFile(cpp.path).toString());
        QCOMPARE(lastDocument(pythonHistory).value("uri").toString(), QUrl::fromLocalFile(python.path).toString());
        QCOMPARE(cppHistory.value("initialize").toObject().value("rootUri"), QJsonValue(QUrl::fromLocalFile(project.path()).toString()));

        cpp.show(); cpp.activateWindow(); cpp.setFocus(); QTRY_VERIFY(cpp.hasFocus());
        QSignalSpy cppReplies(cppClient, &LspClient::responseReceived);
        cpp.requestCompletion();
        // Both clients issue the same numeric ID. An unrelated reply from Python
        // must not consume the pending C++ completion.
        const auto pythonId = pythonClient->request("fixture/null"); QVERIFY(pythonId >= 0);
        QTRY_VERIFY(cpp.findChild<QListWidget *>("lspCompletionPopup") && cpp.findChild<QListWidget *>("lspCompletionPopup")->isVisible());
        QCOMPARE(cppReplies.first().first().toLongLong(), pythonId);
        QTest::keyClick(&cpp, Qt::Key_Return); QCOMPARE(cpp.contents(), QString("printValue"));
        lsp.refreshEditor(&cpp, true);
        const auto saved = history(cppClient);
        QCOMPARE(saved.value("history").toArray().last().toObject().value("method"), QJsonValue("textDocument/didSave"));
        QCOMPARE(history(pythonClient).value("history").toArray().size(), 1);

        lsp.setCurrentEditor(&python);
        QSignalSpy definitions(&lsp, &EditorLsp::definitionsFound);
        python.requestDefinition(); QTRY_COMPARE(definitions.count(), 1);
        QCOMPARE(qvariant_cast<Editor *>(definitions.first().first()), &python);
        QCOMPARE(lsp.statusText(), QStringLiteral("LSP · 就绪")); QVERIFY(lsp.statusDetail().contains("Python"));

        lsp.removeEditor(&python);
        QCOMPARE(history(pythonClient).value("history").toArray().last().toObject().value("method"), QJsonValue("textDocument/didClose"));
        // Saving under another language transfers document ownership.
        cpp.path = project.filePath("renamed.py"); lsp.refreshEditor(&cpp);
        QCOMPARE(history(cppClient).value("history").toArray().last().toObject().value("method"), QJsonValue("textDocument/didClose"));
        const auto transferred = lastDocument(history(pythonClient));
        QCOMPARE(transferred.value("languageId"), QJsonValue("python"));
        QCOMPARE(transferred.value("text"), QJsonValue("printValue"));
    }
    void serverFailuresAreIsolatedAndSettingsCanRemoveAllServers()
    {
        QTemporaryDir project;
        Editor cpp, python, unmatched;
        cpp.path = project.filePath("main.cpp"); cpp.setPlainText("pri");
        python.path = project.filePath("main.py"); python.setPlainText("pri"); end(&python);
        unmatched.path = project.filePath("notes.txt");
        EditorLsp lsp; lsp.addEditor(&cpp); lsp.addEditor(&python); lsp.addEditor(&unmatched);
        LanguageSettings settings; settings.lspEnabled = true;
        settings.lspServers = {server("C++", "cpp"), server("Python", "py", "ui-delay")};
        lsp.configure(settings, project.path()); lsp.setCurrentEditor(&python);
        const auto clients = lsp.findChildren<LspClient *>();
        auto *cppClient = clients.at(0), *pythonClient = clients.at(1);
        QTRY_VERIFY(cppClient->state() == LspClient::State::Ready);
        QTRY_VERIFY(pythonClient->state() == LspClient::State::Ready);
        python.show(); python.activateWindow(); python.setFocus(); QTRY_VERIFY(python.hasFocus());
        python.requestCompletion(); cppClient->request("fixture/crash");
        QTRY_VERIFY(cppClient->state() == LspClient::State::Stopped);
        QTRY_VERIFY(python.findChild<QListWidget *>("lspCompletionPopup") && python.findChild<QListWidget *>("lspCompletionPopup")->isVisible());
        QCOMPARE(lsp.statusText(), QStringLiteral("LSP · 就绪"));
        lsp.setCurrentEditor(&cpp); QCOMPARE(lsp.statusText(), QStringLiteral("LSP · 异常"));
        lsp.setCurrentEditor(&unmatched); QCOMPARE(lsp.statusText(), QStringLiteral("LSP · 等待"));
        QVERIFY(lsp.statusDetail().contains(QStringLiteral("未匹配")));

        // A disabled earlier match is skipped; among enabled matches the first wins.
        settings.lspServers = {server("disabled", "py"), server("Python", "py"), server("duplicate", "py")};
        settings.lspServers[0].enabled = false;
        lsp.configure(settings, project.path()); lsp.setCurrentEditor(&python);
        const auto reconfigured = lsp.findChildren<LspClient *>(); QCOMPARE(reconfigured.size(), 3);
        QCOMPARE(reconfigured[0]->state(), LspClient::State::Stopped);
        QTRY_VERIFY(reconfigured[1]->state() == LspClient::State::Ready);
        QCOMPARE(reconfigured[2]->state(), LspClient::State::Stopped);
        QVERIFY(lsp.statusDetail().contains("Python"));
        settings.lspEnabled = false; lsp.configure(settings, project.path());
        for (auto *client : reconfigured) QCOMPARE(client->state(), LspClient::State::Stopped);
        settings.lspEnabled = true; settings.lspServers.clear(); lsp.configure(settings, project.path());
        QCOMPARE(lsp.findChildren<LspClient *>().size(), 0);
        QCOMPARE(lsp.statusText(), QStringLiteral("LSP · 等待"));
    }
    void multipleServerSettingsApplyCancelAndPersist()
    {
        QVERIFY(configure());
        {
            MainWindow window(nullptr, paths()); window.newDocument();
            auto *action = window.findChild<QAction *>("actionLanguageSettings");
            QTimer::singleShot(0, [&] {
                auto *dialog = qobject_cast<LanguageSettingsDialog *>(QApplication::activeModalWidget());
                dialog->findChild<QLineEdit *>("lspServerName")->setText("C++");
                dialog->findChild<QPushButton *>("lspAddServer")->click();
                dialog->findChild<QLineEdit *>("lspServerName")->setText("Python");
                dialog->findChild<QLineEdit *>("lspExecutable")->setText(fixtureProgram());
                dialog->findChild<QLineEdit *>("lspArguments")->setText("--lsp-fixture ui-delay");
                dialog->findChild<QLineEdit *>("lspExtensions")->setText("py,pyw");
                dialog->findChild<QCheckBox *>("lspServerEnabled")->setChecked(false);
                auto *list = dialog->findChild<QListWidget *>("lspServers"); list->setCurrentRow(0);
                QCOMPARE(dialog->findChild<QLineEdit *>("lspServerName")->text(), QString("C++"));
                list->setCurrentRow(1);
                QCOMPARE(dialog->findChild<QLineEdit *>("lspExtensions")->text(), QString("py,pyw"));
                dialog->accept();
            }); action->trigger();
            const auto settings = current(window)->languageSettings(); QCOMPARE(settings.lspServers.size(), 2);
            QCOMPARE(settings.lspServers[0].name, QString("C++"));
            QCOMPARE(settings.lspServers[1].name, QString("Python")); QVERIFY(!settings.lspServers[1].enabled);
            QTimer::singleShot(0, [&] {
                auto *dialog = qobject_cast<LanguageSettingsDialog *>(QApplication::activeModalWidget());
                dialog->findChild<QPushButton *>("lspRemoveServer")->click(); dialog->reject();
            }); action->trigger();
            QCOMPARE(current(window)->languageSettings().lspServers.size(), 2);
        }
        {
            MainWindow reloaded(nullptr, paths()); reloaded.newDocument();
            const auto servers = current(reloaded)->languageSettings().lspServers; QCOMPARE(servers.size(), 2);
            QCOMPARE(servers[0].extensions, QString("cpp,h")); QCOMPARE(servers[1].executable, fixtureProgram());
            QCOMPARE(servers[1].arguments, QString("--lsp-fixture ui-delay"));
            QCOMPARE(servers[1].extensions, QString("py,pyw")); QVERIFY(!servers[1].enabled);
            QTimer::singleShot(0, [&] {
                auto *dialog = qobject_cast<LanguageSettingsDialog *>(QApplication::activeModalWidget());
                auto *remove = dialog->findChild<QPushButton *>("lspRemoveServer"); remove->click(); remove->click();
                QCOMPARE(dialog->findChild<QListWidget *>("lspServers")->count(), 0); QVERIFY(!remove->isEnabled());
                dialog->accept();
            }); reloaded.findChild<QAction *>("actionLanguageSettings")->trigger();
            QVERIFY(current(reloaded)->languageSettings().lspServers.isEmpty());
        }
        MainWindow empty(nullptr, paths()); empty.newDocument();
        QVERIFY(current(empty)->languageSettings().lspServers.isEmpty()); // Do not resurrect the legacy entry.
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
