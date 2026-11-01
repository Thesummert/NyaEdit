#include "Editor.h"
#include "FontSettingsDialog.h"
#include "AnimationSettingsDialog.h"
#include "BackgroundSettingsDialog.h"
#include "LanguageSettingsDialog.h"
#include "Theme.h"
#include "FileTreeView.h"
#include "MainWindow.h"
#include "WelcomePage.h"
#include "TaskConfiguration.h"
#include "TaskPanel.h"
#include "TaskRunner.h"
#include <QAction>
#include <QAbstractButton>
#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QFile>
#include <QFileSystemModel>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QLineEdit>
#include <QInputDialog>
#include <QMimeData>
#include <QListWidget>
#include <QLabel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMessageBox>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QPainter>
#include <QSettings>
#include <QScrollBar>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardItemModel>
#include <QStatusBar>
#include <QTabWidget>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QTextBlock>
#include <QTextLayout>
#include <QTextBrowser>
#include <QTextTable>
#include <QToolButton>
#include <QToolBar>
#include <QTreeView>
#include <QtTest>
#include <cmath>

using namespace nya;

class RevealTestWindow : public MainWindow {
public:
    using MainWindow::MainWindow;
    QStringList revealedPaths;
protected:
    void revealInFileManager(const QString &path) override { revealedPaths.append(path); }
};

class TerminalTestWindow : public MainWindow {
public:
    using MainWindow::MainWindow;
    QStringList terminalDirectories;
protected:
    void openInTerminal(const QString &directory) override { terminalDirectories.append(directory); }
};

class WidgetTests : public QObject {
    Q_OBJECT
    QTemporaryDir m_settings;
    StoragePaths testPaths() const { return StoragePaths::underDirectory(m_settings.filePath(QString::fromLatin1(QTest::currentTestFunction()))); }
    static QJsonObject fixtureTask(const QString &label, const QString &mode, const QStringList &args = {})
    {
        QString program = QCoreApplication::applicationDirPath() + "/nya_task_tests";
#ifdef Q_OS_WIN
        program += ".exe";
#endif
        return {{"label", label}, {"type", "process"}, {"command", program},
            {"args", QJsonArray::fromStringList(QStringList{"--task-fixture", mode} + args)},
            {"group", QJsonObject{{"kind", "build"}, {"isDefault", true}}}};
    }
    static bool writeTaskConfig(const QString &root, const QJsonArray &tasks)
    {
        const auto path = TaskConfiguration::filePath(root);
        if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
        QFile file(path); if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
        const auto json = QJsonDocument(QJsonObject{{"version", "2.0.0"}, {"tasks", tasks}}).toJson();
        return file.write(json) == json.size();
    }
    struct ContextAction {
        bool shown = false;
        bool enabled = false;
        QString path;
        QString tooltip;
    };
    static bool fileAction(MainWindow &window, const QString &path, const char *name, const std::function<void()> &dialog = {})
    {
        auto *tree = window.findChild<FileTreeView *>("fileTree");
        auto *model = qobject_cast<QFileSystemModel *>(tree->model());
        const auto index = model->index(path);
        tree->scrollTo(index);
        bool enabled = false;
        QTimer::singleShot(0, &window, [&] {
            auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            if (!menu) return;
            auto *action = menu->findChild<QAction *>(name);
            enabled = action && action->isEnabled();
            menu->close();
            if (enabled) {
                if (dialog) QTimer::singleShot(0, &window, dialog);
                action->trigger();
            }
        });
        const QPoint position = path == model->rootPath() ? QPoint(10, tree->viewport()->height() - 5)
            : tree->visualRect(index).center();
        tree->customContextMenuRequested(position);
        return enabled;
    }
    static std::function<void()> enterName(const QString &name)
    {
        return [name] {
            if (auto *dialog = qobject_cast<QInputDialog *>(QApplication::activeModalWidget())) {
                dialog->setTextValue(name); dialog->accept();
            }
        };
    }
    static ContextAction activateReveal(MainWindow &window, QWidget *target, const QPoint &position)
    {
        ContextAction result;
        QTimer timer; timer.setSingleShot(true);
        connect(&timer, &QTimer::timeout, &window, [&] {
            auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            if (!menu) return;
            if (auto *action = menu->findChild<QAction *>("actionRevealInFileManager")) {
                result.shown = true; result.enabled = action->isEnabled();
                result.path = action->data().toString(); result.tooltip = action->toolTip();
                if (action->isEnabled()) action->trigger();
            }
            menu->close();
        });
        timer.start(0);
        QContextMenuEvent event(QContextMenuEvent::Mouse, position, target->mapToGlobal(position));
        QApplication::sendEvent(target, &event); timer.stop();
        return result;
    }
private slots:
    void shiftDeleteConfirmsAndProtectsUnsavedDocuments()
    {
        QTemporaryDir files; QString error;
        const auto path = files.filePath("delete.txt");
        QVERIFY(saveFile(path, "original", {}, LineEnding::LF, &error));
        MainWindow window(nullptr, testPaths()); window.show(); window.activateWindow();
        window.openFolder(files.path()); window.openPath(path);
        auto *tree = window.findChild<FileTreeView *>("fileTree");
        auto *model = qobject_cast<QFileSystemModel *>(tree->model());
        auto *tabs = window.findChild<QTabWidget *>();
        QTRY_VERIFY(tabs->count() == 1 && !qobject_cast<Editor *>(tabs->currentWidget())->loading);
        QTRY_COMPARE(model->rowCount(tree->rootIndex()), 1);
        auto *editor = qobject_cast<Editor *>(tabs->currentWidget()); editor->insertPlainText("unsaved");
        tree->setCurrentIndex(model->index(path)); tree->setFocus(); QTRY_VERIFY(tree->hasFocus());
        bool confirmed = false;
        QTimer::singleShot(0, [&] {
            auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); QVERIFY(box);
            confirmed = box->windowTitle() == QStringLiteral("永久删除") && box->text().contains(QStringLiteral("无法恢复"));
            QCOMPARE(box->defaultButton(), box->button(QMessageBox::No)); box->button(QMessageBox::No)->click();
        });
        QTest::keyClick(tree, Qt::Key_Delete, Qt::ShiftModifier);
        QVERIFY(confirmed); QVERIFY(QFileInfo::exists(path)); QVERIFY(editor->dirty());
        window.activateWindow(); tree->setFocus(); QTRY_VERIFY(tree->hasFocus());
        QTimer::singleShot(0, [&] {
            auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); QVERIFY(box);
            QTimer::singleShot(0, [] {
                auto *save = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); QVERIFY(save);
                QCOMPARE(save->windowTitle(), QStringLiteral("保存更改")); save->button(QMessageBox::Cancel)->click();
            });
            box->button(QMessageBox::Yes)->click();
        });
        QTest::keyClick(tree, Qt::Key_Delete, Qt::ShiftModifier);
        QVERIFY(QFileInfo::exists(path)); QCOMPARE(tabs->count(), 1); QVERIFY(editor->dirty());
        window.activateWindow(); tree->setFocus(); QTRY_VERIFY(tree->hasFocus());
        QTimer::singleShot(0, [&] {
            auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); QVERIFY(box);
            QTimer::singleShot(0, [] {
                auto *save = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); QVERIFY(save);
                save->button(QMessageBox::Discard)->click();
            });
            box->button(QMessageBox::Yes)->click();
        });
        QTest::keyClick(tree, Qt::Key_Delete, Qt::ShiftModifier);
        QVERIFY(!QFileInfo::exists(path)); QCOMPARE(tabs->count(), 0);
        window.close();
    }

    void shiftMenuPermanentlyDeletesDirectoryWithoutFollowingSymlinks()
    {
        QTemporaryDir files; QString error;
        const auto folder = files.filePath("folder"), keep = files.filePath("keep");
        QVERIFY(QDir().mkpath(folder + "/nested")); QVERIFY(QDir().mkdir(keep));
        QVERIFY(saveFile(folder + "/nested/file.txt", "delete", {}, LineEnding::LF, &error));
        QVERIFY(saveFile(keep + "/file.txt", "preserve", {}, LineEnding::LF, &error));
#ifndef Q_OS_WIN
        QVERIFY(QFile::link(keep, folder + "/link"));
#endif
        MainWindow window(nullptr, testPaths()); window.show(); window.activateWindow(); window.openFolder(files.path());
        auto *tree = window.findChild<FileTreeView *>("fileTree");
        auto *model = qobject_cast<QFileSystemModel *>(tree->model());
        QTRY_COMPARE(model->rowCount(tree->rootIndex()), 2);
        tree->setFocus(); QTRY_VERIFY(tree->hasFocus());
        bool permanent = false;
        QTimer::singleShot(0, [&] {
            auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget()); QVERIFY(menu);
            auto *remove = menu->findChild<QAction *>("actionTrashFile"); QVERIFY(remove);
            QTimer::singleShot(0, [&] {
                auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); QVERIFY(box);
                permanent = box->windowTitle() == QStringLiteral("永久删除"); box->button(QMessageBox::Yes)->click();
            });
            QTest::mouseClick(menu, Qt::LeftButton, Qt::ShiftModifier, menu->actionGeometry(remove).center());
        });
        tree->customContextMenuRequested(tree->visualRect(model->index(folder)).center());
        QVERIFY(permanent); QVERIFY(!QFileInfo::exists(folder));
        QCOMPARE(loadFile(keep + "/file.txt").text, QStringLiteral("preserve"));
        window.close();
    }

    void fileTreeCreatesFilesFoldersAndProtectsExistingFiles()
    {
        QTemporaryDir files;
        MainWindow window(nullptr, testPaths()); window.show(); window.openFolder(files.path());
        auto *tree = window.findChild<FileTreeView *>("fileTree");
        auto *model = qobject_cast<QFileSystemModel *>(tree->model());
        QTRY_COMPARE(model->rootPath(), files.path());
        QVERIFY(fileAction(window, files.path(), "actionNewFolder", enterName(QStringLiteral("新目录"))));
        const auto folder = files.filePath(QStringLiteral("新目录")); QVERIFY(QFileInfo(folder).isDir());
        QTRY_COMPARE(model->rowCount(tree->rootIndex()), 1);
        QVERIFY(fileAction(window, folder, "actionNewFile", enterName(QStringLiteral("笔记.md"))));
        const auto path = QDir(folder).filePath(QStringLiteral("笔记.md")); QVERIFY(QFileInfo(path).isFile());
        auto *tabs = window.findChild<QTabWidget *>();
        QTRY_VERIFY(tabs->count() == 1 && !qobject_cast<Editor *>(tabs->currentWidget())->loading);
        QCOMPARE(qobject_cast<Editor *>(tabs->currentWidget())->path, path);
        QVERIFY(fileAction(window, folder, "actionNewFile", [=] {
            QTimer::singleShot(0, [] { if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) box->accept(); });
            enterName(QStringLiteral("笔记.md"))();
        }));
        QCOMPARE(QFileInfo(path).size(), 0);
        QVERIFY(fileAction(window, path, "actionCopyFilePath")); QCOMPARE(qApp->clipboard()->text(), QDir::toNativeSeparators(path));
        QVERIFY(!fileAction(window, files.path(), "actionRenameFile"));
        QVERIFY(!fileAction(window, files.path(), "actionTrashFile"));
        window.close();
    }

    void fileTreeRenameAndMoveKeepDirtyEditorsAndUpdatePaths()
    {
        QTemporaryDir files; QString error;
        const auto folder = files.filePath("source"), target = files.filePath("target");
        QVERIFY(QDir().mkdir(folder)); QVERIFY(QDir().mkdir(target));
        const auto original = QDir(folder).filePath("notes.txt");
        QVERIFY(saveFile(original, "disk content", {}, LineEnding::LF, &error));
        MainWindow window(nullptr, testPaths()); window.show(); window.openFolder(files.path()); window.openPath(original);
        auto *tabs = window.findChild<QTabWidget *>();
        QTRY_VERIFY(tabs->count() == 1 && !qobject_cast<Editor *>(tabs->currentWidget())->loading);
        auto *editor = qobject_cast<Editor *>(tabs->currentWidget()); editor->insertPlainText("unsaved ");
        const auto contents = editor->toPlainText(); QVERIFY(editor->dirty());
        auto *tree = window.findChild<FileTreeView *>("fileTree"); auto *model = qobject_cast<QFileSystemModel *>(tree->model());
        QTRY_COMPARE(model->rowCount(tree->rootIndex()), 2);
        QVERIFY(fileAction(window, folder, "actionRenameFile", enterName(QStringLiteral("重命名"))));
        const auto renamed = files.filePath(QStringLiteral("重命名"));
        QCOMPARE(editor->path, QDir(renamed).filePath("notes.txt")); QVERIFY(!QFileInfo::exists(folder));
        QVERIFY(editor->dirty()); QCOMPARE(editor->toPlainText(), contents);
        QTRY_VERIFY(model->index(renamed).isValid());
        QVERIFY(fileAction(window, renamed, "actionCutFile"));
        QVERIFY(fileAction(window, target, "actionPasteFile"));
        const auto moved = QDir(target).filePath(QStringLiteral("重命名/notes.txt"));
        QCOMPARE(editor->path, moved); QVERIFY(QFileInfo::exists(moved)); QVERIFY(!QFileInfo::exists(renamed));
        QVERIFY(editor->dirty()); QCOMPARE(editor->toPlainText(), contents);
        QVERIFY(!qApp->clipboard()->mimeData() || qApp->clipboard()->mimeData()->urls().isEmpty());
        window.findChild<QAction *>("actionSave")->trigger();
        QVERIFY(!editor->dirty()); QCOMPARE(loadFile(moved).text, contents);
        window.close();
    }

    void fileTreeCopiesDirectoriesHiddenFilesAndAvoidsOverwrite()
    {
        QTemporaryDir files; QString error;
        const auto folder = files.filePath("source"), target = files.filePath("target");
        QVERIFY(QDir().mkpath(folder + "/nested")); QVERIFY(QDir().mkdir(target));
        QVERIFY(saveFile(folder + "/nested/file.txt", "hello", {}, LineEnding::LF, &error));
        QVERIFY(saveFile(folder + "/.hidden", "hidden", {}, LineEnding::LF, &error));
        MainWindow window(nullptr, testPaths()); window.show(); window.openFolder(files.path());
        auto *tree = window.findChild<FileTreeView *>("fileTree"); auto *model = qobject_cast<QFileSystemModel *>(tree->model());
        QTRY_COMPARE(model->rowCount(tree->rootIndex()), 2);
        QVERIFY(fileAction(window, folder, "actionCopyFile"));
        QCOMPARE(qApp->clipboard()->mimeData()->urls().first(), QUrl::fromLocalFile(folder));
        QVERIFY(fileAction(window, target, "actionPasteFile"));
        QCOMPARE(loadFile(target + "/source/nested/file.txt").text, QStringLiteral("hello"));
        QCOMPARE(loadFile(target + "/source/.hidden").text, QStringLiteral("hidden"));
        QVERIFY(fileAction(window, target, "actionPasteFile"));
        QCOMPARE(loadFile(target + QStringLiteral("/source - 副本/.hidden")).text, QStringLiteral("hidden"));
        QVERIFY(QFileInfo::exists(folder + "/.hidden"));
        QVERIFY(fileAction(window, folder, "actionPasteFile", [] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) box->accept();
        }));
        QVERIFY(!QFileInfo::exists(folder + "/source"));
        QCOMPARE(QDir(target).entryList({".nya-copy-*"}, QDir::Dirs | QDir::Hidden).size(), 0);
        QVERIFY(fileAction(window, folder, "actionTrashFile", [] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) box->done(QMessageBox::No);
        }));
        QVERIFY(QFileInfo::exists(folder));
        window.close();
    }

    void fileTreeRejectsInvalidNamesAndCleansFailedCopies()
    {
        QTemporaryDir files; QString error;
        const auto folder = files.filePath("source"), target = files.filePath("target");
        QVERIFY(QDir().mkdir(folder)); QVERIFY(QDir().mkdir(target));
        QVERIFY(saveFile(folder + "/a.txt", "preserve", {}, LineEnding::LF, &error));
        MainWindow window(nullptr, testPaths()); window.show(); window.openFolder(files.path());
        auto *tree = window.findChild<FileTreeView *>("fileTree"); auto *model = qobject_cast<QFileSystemModel *>(tree->model());
        QTRY_COMPARE(model->rowCount(tree->rootIndex()), 2);
        const auto dismissWarning = [] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) box->accept();
        };
        QVERIFY(fileAction(window, folder, "actionNewFile", [=] {
            QTimer::singleShot(0, dismissWarning); enterName("../outside.txt")();
        }));
        QVERIFY(!QFileInfo::exists(files.filePath("outside.txt")));
        QVERIFY(fileAction(window, folder, "actionRenameFile", [=] {
            QTimer::singleShot(0, dismissWarning); enterName("target")();
        }));
        QVERIFY(QFileInfo::exists(folder + "/a.txt"));
#ifndef Q_OS_WIN
        QVERIFY(QFile::link(folder + "/a.txt", folder + "/z-link"));
        // The standard file URL clipboard also works without our copy action.
        auto *data = new QMimeData; data->setUrls({QUrl::fromLocalFile(folder)}); qApp->clipboard()->setMimeData(data);
        QVERIFY(fileAction(window, target, "actionPasteFile", dismissWarning));
        QVERIFY(!QFileInfo::exists(target + "/source"));
        QCOMPARE(QDir(target).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden).size(), 0);
        QCOMPARE(loadFile(folder + "/a.txt").text, QStringLiteral("preserve"));
#endif
        window.close();
    }

    void markdownPreviewRendersAndFollowsCurrentDocument()
    {
        MainWindow window(nullptr, testPaths()); window.show();
        auto *action = window.findChild<QAction *>("actionMarkdownPreview"); QVERIFY(action);
        auto *preview = window.findChild<QTextBrowser *>("markdownPreview"); QVERIFY(preview);
        auto *dock = window.findChild<QDockWidget *>("markdownDock"); QVERIFY(dock);
        QVERIFY(!dock->isVisible());
        action->trigger(); QVERIFY(dock->isVisible()); QVERIFY(preview->isReadOnly());
        QVERIFY(preview->toPlainText().contains(QStringLiteral("打开或新建")));
        window.newDocument();
        auto *tabs = window.findChild<QTabWidget *>(); QVERIFY(tabs);
        auto *first = qobject_cast<Editor *>(tabs->currentWidget()); QVERIFY(first);
        const QString source = QStringLiteral("# 标题\n\n**粗体** 和 *斜体*\n\n- 列表项\n\n> 引用\n\n```cpp\nint n = 1;\n```\n\n| A | B |\n| --- | --- |\n| 1 | 2 |\n");
        first->setPlainText(source);
        QTRY_VERIFY(preview->toPlainText().contains(QStringLiteral("标题")));
        QCOMPARE(preview->document()->firstBlock().blockFormat().headingLevel(), 1);
        QVERIFY(!preview->toPlainText().contains("**"));
        bool bold = false, table = false;
        for (auto block = preview->document()->begin(); block.isValid(); block = block.next())
            for (auto it = block.begin(); !it.atEnd(); ++it)
                if (it.fragment().text() == QStringLiteral("粗体")) bold = it.fragment().charFormat().fontWeight() >= QFont::Bold;
        for (auto *frame : preview->document()->rootFrame()->childFrames())
            if (qobject_cast<QTextTable *>(frame)) table = true;
        QVERIFY(bold); QVERIFY(table); QCOMPARE(first->toPlainText(), source);
        first->insertPlainText(QStringLiteral("更新"));
        QTRY_VERIFY(preview->toPlainText().contains(QStringLiteral("更新")));
        window.newDocument();
        auto *second = qobject_cast<Editor *>(tabs->currentWidget()); QVERIFY(second);
        second->setPlainText(QStringLiteral("# 第二份"));
        QTRY_COMPARE(preview->toPlainText().trimmed(), QStringLiteral("第二份"));
        tabs->setCurrentWidget(first);
        QTRY_VERIFY(preview->toPlainText().contains(QStringLiteral("标题")));
        window.findChild<QAction *>("actionTheme")->trigger();
        QCOMPARE(preview->palette().color(QPalette::Text), qApp->palette().color(QPalette::Text));
        action->trigger(); QVERIFY(!dock->isVisible());
        first->setPlainText(QStringLiteral("# 隐藏后修改"));
        action->trigger(); QCOMPARE(preview->toPlainText().trimmed(), QStringLiteral("隐藏后修改"));
        first->document()->setModified(false); second->document()->setModified(false);
        window.close();
    }

    void markdownPreviewUsesFileDirectoryAndClearsOnWelcome()
    {
        QTemporaryDir files; QVERIFY(files.isValid());
        QImage image(12, 9, QImage::Format_RGB32); image.fill(Qt::red);
        QVERIFY(image.save(files.filePath("image.png")));
        QFile file(files.filePath("sample.md")); QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("# Saved\n\n![example](image.png)\n"); file.close();
        MainWindow window(nullptr, testPaths()); window.show(); window.openPath(file.fileName());
        auto *tabs = window.findChild<QTabWidget *>();
        QTRY_VERIFY(tabs->count() == 1 && !qobject_cast<Editor *>(tabs->currentWidget())->loading);
        window.findChild<QAction *>("actionMarkdownPreview")->trigger();
        auto *preview = window.findChild<QTextBrowser *>("markdownPreview");
        QCOMPARE(preview->document()->baseUrl(), QUrl::fromLocalFile(files.path() + QDir::separator()));
        const auto resource = preview->document()->resource(QTextDocument::ImageResource, QUrl("image.png"));
        QVERIFY(!qvariant_cast<QImage>(resource).isNull() || !qvariant_cast<QPixmap>(resource).isNull());
        window.findChild<QAction *>("actionWelcome")->trigger();
        QTRY_VERIFY(preview->toPlainText().contains(QStringLiteral("打开或新建")));
        QVERIFY(preview->document()->baseUrl().isEmpty());
        tabs->setCurrentIndex(0);
        window.newDocument();
        auto *editor = qobject_cast<Editor *>(tabs->currentWidget());
        editor->setPlainText(QString(2 * 1024 * 1024 + 1, QLatin1Char('x')));
        QTRY_VERIFY(preview->toPlainText().contains(QStringLiteral("文档过大")));
        editor->document()->setModified(false); window.close();
    }

    void runMenuOpensTerminalInProjectOrFileDirectory()
    {
        QTemporaryDir project, files;
        TerminalTestWindow window(nullptr, testPaths());
        auto *action = window.findChild<QAction *>("actionOpenInTerminal"); QVERIFY(action); QVERIFY(action->isEnabled());
        auto *run = window.findChild<QMenu *>("runMenu"); QVERIFY(run->actions().contains(action));
        action->trigger(); QCOMPARE(window.terminalDirectories.last(), QFileInfo(QDir::homePath()).canonicalFilePath());
        const auto path = files.filePath(QStringLiteral("文件 空格 ' $;.txt"));
        { QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("hello"); }
        window.openPath(path); action->trigger(); QCOMPARE(window.terminalDirectories.last(), QFileInfo(files.path()).canonicalFilePath());
        auto *editor = qobject_cast<Editor *>(window.findChild<QTabWidget *>()->currentWidget()); QTRY_VERIFY(!editor->loading);
        window.openFolder(project.path()); action->trigger(); QCOMPARE(window.terminalDirectories.last(), QFileInfo(project.path()).canonicalFilePath());
        QVERIFY(writeTaskConfig(project.path(), {fixtureTask("long", "slow")}));
        auto *runner = window.findChild<TaskRunner *>(); QSignalSpy done(runner, &TaskRunner::finished);
        window.findChild<QAction *>("actionBuildTask")->trigger(); QVERIFY(runner->isRunning());
        QVERIFY(action->isEnabled()); action->trigger(); QCOMPARE(window.terminalDirectories.last(), QFileInfo(project.path()).canonicalFilePath());
        runner->stop(); QTRY_COMPARE(done.count(), 1); QVERIFY(done.first().at(1).toBool());
    }
    void configureProjectTasksCreatesAndPreservesConfig()
    {
        QTemporaryDir project;
        MainWindow window(nullptr, testPaths()); window.openFolder(project.path());
        auto *configure = window.findChild<QAction *>("actionConfigureTasks"); QVERIFY(configure);
        configure->trigger();
        const auto path = TaskConfiguration::filePath(project.path());
        QVERIFY(QFileInfo::exists(path)); QVERIFY(TaskConfiguration::load(project.path()).ok());
        auto *tabs = window.findChild<QTabWidget *>(); QCOMPARE(tabs->count(), 1);
        auto *editor = qobject_cast<Editor *>(tabs->currentWidget()); QTRY_VERIFY(!editor->loading);
        QCOMPARE(editor->path, QFileInfo(path).canonicalFilePath()); QVERIFY(!editor->dirty());
        const auto original = editor->contents(); configure->trigger(); QCOMPARE(tabs->count(), 1);
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(QString::fromUtf8(file.readAll()), original);
    }
    void taskMenuRunsBuildReloadsAndShowsOutput()
    {
        QTemporaryDir project, other;
        auto task = fixtureTask("build", "echo", {QStringLiteral("中文 😀"), "first"});
        task.insert("options", QJsonObject{{"env", QJsonObject{{"NYA_TASK_TEST", "from-project"}}}});
        QVERIFY(writeTaskConfig(project.path(), {task}));
        MainWindow window(nullptr, testPaths()); window.openFolder(project.path()); window.show();
        auto *runner = window.findChild<TaskRunner *>(); auto *dock = window.findChild<QDockWidget *>("taskDock");
        auto *output = window.findChild<QPlainTextEdit *>("taskOutput"); QVERIFY(runner); QVERIFY(output->isReadOnly());
        auto *build = window.findChild<QAction *>("actionBuildTask"); auto *run = window.findChild<QAction *>("actionRunTask");
        auto *stop = window.findChild<QAction *>("actionStopTask"); auto *repeat = window.findChild<QAction *>("actionRepeatTask");
        QVERIFY(!repeat->isEnabled()); QVERIFY(!stop->isEnabled()); QVERIFY(dock->isHidden());
        QSignalSpy done(runner, &TaskRunner::finished); build->trigger();
        QVERIFY(runner->isRunning()); QVERIFY(!run->isEnabled()); QVERIFY(stop->isEnabled()); QVERIFY(!dock->isHidden());
        QTRY_COMPARE(done.count(), 1); QCOMPARE(done.first().at(0).toInt(), 0);
        QVERIFY(output->toPlainText().contains(QStringLiteral("中文 😀"))); QVERIFY(output->toPlainText().contains("from-project"));
        QVERIFY(output->toPlainText().contains("stderr-visible")); QVERIFY(run->isEnabled()); QVERIFY(repeat->isEnabled()); QVERIFY(!stop->isEnabled());
        const auto text = output->toPlainText(); window.findChild<QAction *>("actionTheme")->trigger(); QCOMPARE(output->toPlainText(), text);
        task.insert("args", QJsonArray{"--task-fixture", "echo", "reloaded"}); QVERIFY(writeTaskConfig(project.path(), {task}));
        repeat->trigger(); QTRY_COMPARE(done.count(), 2); QVERIFY(output->toPlainText().contains("reloaded")); QVERIFY(!output->toPlainText().contains("first"));
        auto *menu = window.findChild<QMenu *>("projectTasksMenu"); QMetaObject::invokeMethod(menu, "aboutToShow", Qt::DirectConnection);
        QCOMPARE(menu->actions().size(), 1); QCOMPARE(menu->actions().first()->data().toString(), QString("build"));
        menu->actions().first()->trigger(); QTRY_COMPARE(done.count(), 3);
        window.openFolder(other.path()); QVERIFY(!repeat->isEnabled());
    }
    void taskVariablesAndInvalidConfigDoNotRunPartialPlans()
    {
        QTemporaryDir project;
        const auto sentinel = project.filePath("sentinel");
        auto first = fixtureTask("prepare", "write", {sentinel}); first.remove("group");
        auto build = fixtureTask("build", "echo", {"${file}"}); build.insert("dependsOn", "prepare");
        QVERIFY(writeTaskConfig(project.path(), {first, build}));
        MainWindow window(nullptr, testPaths()); window.openFolder(project.path());
        auto *runner = window.findChild<TaskRunner *>(); QSignalSpy done(runner, &TaskRunner::finished);
        auto *output = window.findChild<QPlainTextEdit *>("taskOutput");
        window.findChild<QAction *>("actionBuildTask")->trigger();
        QTRY_COMPARE(done.count(), 1); QVERIFY(!runner->isRunning()); QVERIFY(!QFileInfo::exists(sentinel)); QVERIFY(output->toPlainText().contains("${file}"));
        const auto source = project.filePath(QStringLiteral("当前 文档.txt"));
        { QFile file(source); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("hello"); }
        window.openPath(source);
        auto *editor = qobject_cast<Editor *>(window.findChild<QTabWidget *>()->currentWidget()); QTRY_VERIFY(!editor->loading);
        window.findChild<QAction *>("actionBuildTask")->trigger(); QTRY_COMPARE(done.count(), 2);
        QCOMPARE(done.last().at(0).toInt(), 0); QVERIFY(QFileInfo::exists(sentinel)); QVERIFY(output->toPlainText().contains(source));
        { QFile file(TaskConfiguration::filePath(project.path())); QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate)); file.write("{\n broken"); }
        window.findChild<QAction *>("actionBuildTask")->trigger(); QVERIFY(!runner->isRunning()); QCOMPARE(done.count(), 2);
        QVERIFY(output->toPlainText().contains(QStringLiteral("第 2 行")));
        auto *menu = window.findChild<QMenu *>("projectTasksMenu"); QMetaObject::invokeMethod(menu, "aboutToShow", Qt::DirectConnection);
        QCOMPARE(menu->actions().size(), 1); QVERIFY(!menu->actions().first()->isEnabled());
    }
    void taskOutputCanHideAndStopAcrossProjectChanges()
    {
        QTemporaryDir project, other;
        QVERIFY(writeTaskConfig(project.path(), {fixtureTask("long", "slow")}));
        QVERIFY(writeTaskConfig(other.path(), {fixtureTask("long", "slow")}));
        MainWindow window(nullptr, testPaths()); window.openFolder(project.path()); window.show();
        auto *runner = window.findChild<TaskRunner *>(); QSignalSpy done(runner, &TaskRunner::finished);
        auto *output = window.findChild<QPlainTextEdit *>("taskOutput");
        window.findChild<QAction *>("actionBuildTask")->trigger(); QTRY_VERIFY(output->toPlainText().contains("ready\n"));
        auto *toggle = window.findChild<QAction *>("actionTaskOutput"); toggle->trigger();
        QVERIFY(window.findChild<QDockWidget *>("taskDock")->isHidden()); QVERIFY(runner->isRunning());
        window.openFolder(other.path()); QVERIFY(runner->isRunning());
        QVERIFY(window.findChild<QLabel *>("taskTitle")->text().contains(project.path()));
        toggle->trigger(); window.findChild<QToolButton *>("taskStop")->click(); QTRY_COMPARE(done.count(), 1);
        QVERIFY(done.first().at(1).toBool()); QVERIFY(!window.findChild<QAction *>("actionRepeatTask")->isEnabled());
        done.clear(); window.findChild<QAction *>("actionBuildTask")->trigger(); QTRY_VERIFY(output->toPlainText().contains("ready\n"));
        QVERIFY(window.close()); QTRY_COMPARE(done.count(), 1); QVERIFY(done.first().at(1).toBool());
    }
    void taskPanelBoundsOutputAndStripsStreamingControlSequences()
    {
        TaskPanel panel;
        auto *output = panel.findChild<QPlainTextEdit *>("taskOutput");
        panel.appendOutput("A\033[3"); panel.appendOutput("1mRED\033[0m\033]0;title\007Z\r"); panel.appendOutput("\nline\n"); panel.flush();
        QCOMPARE(output->toPlainText(), QString("AREDZ\nline\n"));
        panel.findChild<QToolButton *>("taskClearOutput")->click(); QVERIFY(output->toPlainText().isEmpty());
        panel.appendOutput(QString(2 * 1024 * 1024, 'x') + "tail"); panel.flush();
        QVERIFY(output->toPlainText().size() <= 1024 * 1024); QVERIFY(output->toPlainText().endsWith("tail"));
    }
    void backgroundGaussianAndOpacityArePainted()
    {
        QTemporaryDir fixture;
        const QString path = fixture.filePath(QStringLiteral("背景 图片.png"));
        QImage source(128, 128, QImage::Format_ARGB32_Premultiplied); source.fill(Qt::black);
        { QPainter painter(&source); painter.fillRect(64, 0, 64, 128, Qt::white); }
        QVERIFY(source.save(path));
        EditorBackground background;
        const auto render = [&] {
            QImage image(256, 128, QImage::Format_ARGB32_Premultiplied); image.fill(Qt::black);
            QPainter painter(&image); background.paint(painter, image.rect()); return image;
        };
        QVERIFY(background.setSettings({path, 100, 0}));
        const auto sharp = render();
        QCOMPARE(sharp.pixelColor(110, 64), QColor(Qt::black));
        QCOMPARE(sharp.pixelColor(145, 64), QColor(Qt::white));
        QVERIFY(background.setSettings({path, 100, 40}));
        const auto blurred = render();
        QVERIFY(blurred.pixelColor(120, 64).red() > sharp.pixelColor(120, 64).red() + 10);
        QVERIFY(blurred.pixelColor(135, 64).red() < sharp.pixelColor(135, 64).red() - 10);
        QVERIFY(background.setSettings({path, 25, 0}));
        QVERIFY(qAbs(render().pixelColor(200, 64).red() - 64) <= 2);
        QVERIFY(background.setSettings({path, 0, 0}));
        QVERIFY(!background.isActive()); QCOMPARE(render().pixelColor(200, 64), QColor(Qt::black));
    }
    void backgroundStaysFixedWhileEditingAndSwitchingThemes()
    {
        QTemporaryDir fixture;
        const QString path = fixture.filePath("gradient.png");
        QImage source(256, 256, QImage::Format_RGB32);
        for (int y = 0; y < source.height(); ++y)
            for (int x = 0; x < source.width(); ++x) source.setPixelColor(x, y, QColor(y, x, 100));
        QVERIFY(source.save(path));
        EditorBackground background; QVERIFY(background.setSettings({path, 100, 0}));
        for (bool dark : {true, false}) {
            Theme::apply(*qApp, dark);
            Editor editor; editor.setDark(dark); editor.setBackground(&background); editor.resize(600, 320);
            auto animations = editor.animationSettings(); animations.cursorEnabled = false; editor.setAnimationSettings(animations);
            editor.setPlainText(QString("short text\n").repeated(200)); editor.document()->setModified(false); editor.show();
            const auto sample = [&] {
                const QImage image = editor.viewport()->grab().toImage();
                return image.pixelColor(image.width() - 50, image.height() / 2);
            };
            const QColor initial = sample();
            QVERIFY(initial != QColor(dark ? "#141820" : "#f5f7fa"));
            QVERIFY(initial.red() > 30 && initial.red() < 220);
            editor.verticalScrollBar()->setValue(80); QApplication::processEvents();
            QCOMPARE(sample(), initial);
            auto cursor = editor.textCursor(); cursor.setPosition(0); cursor.setPosition(5, QTextCursor::KeepAnchor); editor.setTextCursor(cursor);
            const auto selected = editor.textCursor().selectedText();
            QVERIFY(background.setSettings({path, 100, 20}));
            QCOMPARE(editor.textCursor().selectedText(), selected); QVERIFY(!editor.dirty());
            Theme::apply(*qApp, !dark); editor.setDark(!dark);
            QCOMPARE(editor.textCursor().selectedText(), selected); QVERIFY(!editor.dirty());
            const auto image = editor.viewport()->grab().toImage();
            int highlighted = 0;
            for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x)
                if (image.pixelColor(x, y).rgb() == editor.palette().color(QPalette::Highlight).rgb()) ++highlighted;
            QVERIFY(highlighted > 100);
            QVERIFY(background.setSettings({{}, 20, 0}));
            QCOMPARE(sample(), QColor(!dark ? "#141820" : "#f5f7fa"));
            QVERIFY(background.setSettings({path, 100, 0}));
        }
    }
    void globalBackgroundCoversWindowWithContinuousCoordinates()
    {
        QTemporaryDir fixture;
        QVERIFY(QDir().mkpath(fixture.filePath("folder")));
        { QFile file(fixture.filePath("folder/child.txt")); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("hello"); }
        const QString path = fixture.filePath("gradient.png");
        QImage source(256, 256, QImage::Format_RGB32);
        for (int y = 0; y < source.height(); ++y)
            for (int x = 0; x < source.width(); ++x) source.setPixelColor(x, y, QColor(x, y, 180));
        QVERIFY(source.save(path));
        AppStorage storage(testPaths()); QVERIFY(storage.initialize(false));
        QVERIFY(storage.setValue("animation/interface", false));
        QVERIFY(storage.setValue("animation/cursor", false));
        QVERIFY(storage.setValue("appearance/dark", true));
        QVERIFY(storage.record(path, RecentKind::File));
        MainWindow window(nullptr, testPaths()); window.show();
        auto *background = window.findChild<EditorBackground *>(); QVERIFY(background);
        QVERIFY(background->setSettings({path, 65, 0}));
        auto *sidebar = window.findChild<QFrame *>("sidebar");
        auto *welcome = window.findChild<WelcomePage *>();
        auto *history = window.findChild<QListWidget *>("welcomeHistory");
        bool dark = true;
        // Compare UI pixels against a single image rendered across the entire window.
        // This catches opaque panels, repeated pictures and local coordinate offsets.
        const auto matchesWindow = [&](QWidget *widget, QPoint point) {
            QApplication::processEvents();
            const auto actual = window.grab();
            QImage expected(actual.size(), QImage::Format_ARGB32_Premultiplied);
            expected.setDevicePixelRatio(actual.devicePixelRatio());
            expected.fill(QColor(dark ? "#141820" : "#f5f7fa"));
            { QPainter painter(&expected); background->paint(painter, window.rect()); }
            const QPoint pixel = widget->mapTo(&window, point) * actual.devicePixelRatio();
            if (!actual.rect().contains(pixel)) return false;
            const auto a = actual.toImage().pixelColor(pixel), b = expected.pixelColor(pixel);
            return qAbs(a.red() - b.red()) <= 2 && qAbs(a.green() - b.green()) <= 2 && qAbs(a.blue() - b.blue()) <= 2;
        };
        for (int theme = 0; theme < 2; ++theme) {
            QVERIFY(matchesWindow(window.menuBar(), QPoint(window.menuBar()->width() - 30, 10)));
            auto *toolbar = window.findChild<QToolBar *>();
            QVERIFY(matchesWindow(toolbar, QPoint(toolbar->width() / 2, toolbar->height() / 2)));
            QVERIFY(matchesWindow(sidebar, QPoint(sidebar->width() / 2, sidebar->height() - 80)));
            QVERIFY(matchesWindow(welcome, QPoint(welcome->width() - 25, welcome->height() - 30)));
            QVERIFY(matchesWindow(history->viewport(), QPoint(history->viewport()->width() - 15, history->viewport()->height() - 15)));
            QVERIFY(matchesWindow(window.statusBar(), QPoint(window.statusBar()->width() / 2, window.statusBar()->height() / 2)));
            if (theme == 0) { window.findChild<QAction *>("actionTheme")->trigger(); dark = false; }
        }
        window.newDocument(); window.newDocument();
        auto *tabs = window.findChild<QTabWidget *>();
        auto *editor = qobject_cast<Editor *>(tabs->currentWidget());
        editor->setPlainText(QString("short text\n").repeated(200)); editor->document()->setModified(false);
        const auto editorMatches = [&] {
            QApplication::processEvents();
            return matchesWindow(editor->viewport(), QPoint(editor->viewport()->width() - 50, editor->viewport()->height() / 2));
        };
        QVERIFY(editorMatches());
        editor->verticalScrollBar()->setValue(60); QVERIFY(editorMatches());
        tabs->setCurrentIndex(0); tabs->setCurrentIndex(1); QVERIFY(editorMatches());
        auto *bar = tabs->tabBar();
        QVERIFY(matchesWindow(bar, QPoint(bar->tabRect(1).center().x(), 3)));
        window.openFolder(fixture.path());
        auto *tree = window.findChild<FileTreeView *>("fileTree");
        QTRY_VERIFY(tree->model()->rowCount(tree->rootIndex()) > 0);
        QVERIFY(matchesWindow(tree->viewport(), QPoint(tree->viewport()->width() - 15, tree->viewport()->height() - 15)));
        // At the beginning of expansion, children must still be visually hidden.
        // Transparent text snapshots must not leave the final logical rows underneath.
        auto *files = qobject_cast<QFileSystemModel *>(tree->model());
        const auto folderIndex = files->index(fixture.filePath("folder"));
        QVERIFY(folderIndex.isValid());
        auto animations = AnimationSettings{}; animations.speed = 0.25; tree->setAnimationSettings(animations);
        const auto treeFrame = [&] {
            const auto frame = window.grab();
            return frame.copy(QRect(tree->viewport()->mapTo(&window, QPoint()) * frame.devicePixelRatio(),
                                    tree->viewport()->size() * frame.devicePixelRatio()));
        };
        const auto collapsed = treeFrame();
        const int belowFolder = tree->visualRect(folderIndex).bottom() + 1;
        tree->expand(folderIndex); QTRY_COMPARE(files->rowCount(folderIndex), 1); tree->viewport()->grab();
        auto *animation = tree->findChild<QVariantAnimation *>("treeTransition");
        QCOMPARE(animation->state(), QAbstractAnimation::Running); animation->setCurrentTime(0);
        const auto start = treeFrame();
        const QRect area(0, qCeil(belowFolder * start.devicePixelRatio()), start.width(),
                         start.height() - qCeil(belowFolder * start.devicePixelRatio()));
        const auto a = start.toImage(), b = collapsed.toImage();
        int maxDifference = 0;
        for (int y = area.top(); y <= area.bottom(); ++y) for (int x = area.left(); x <= area.right(); ++x) {
            const auto first = a.pixelColor(x, y), second = b.pixelColor(x, y);
            const int difference = qMax(qAbs(first.red() - second.red()), qMax(qAbs(first.green() - second.green()), qAbs(first.blue() - second.blue())));
            maxDifference = qMax(maxDifference, difference);
        }
        QVERIFY2(maxDifference <= 2, qPrintable(QString::number(maxDifference)));
        QVERIFY(matchesWindow(tree->viewport(), QPoint(tree->viewport()->width() - 15, tree->viewport()->height() - 15)));
        animation->setCurrentTime(animation->duration());
        window.findChild<QAction *>("actionFind")->trigger();
        auto *search = window.findChild<QFrame *>("searchPanel");
        QVERIFY(matchesWindow(search, QPoint(search->width() - 5, search->height() / 2)));
        window.resize(990, 640); QVERIFY(editorMatches());
        window.findChild<QAction *>("actionSidebar")->trigger(); QVERIFY(editorMatches());
        window.findChild<QAction *>("actionSidebar")->trigger(); QVERIFY(editorMatches());
        // Zero opacity and missing files must both restore all original panel surfaces.
        QVERIFY(background->setSettings({path, 0, 0}));
        QVERIFY(window.styleSheet().isEmpty());
        const auto sidebarColor = [&] {
            const auto image = sidebar->grab().toImage(); return image.pixelColor(image.width() / 2, image.height() - 80);
        };
        QCOMPARE(sidebarColor(), QColor("#ffffff"));
        QVERIFY(background->setSettings({path, 65, 0})); QVERIFY(editorMatches());
        QVERIFY(QFile::remove(path));
        QTRY_VERIFY(background->settings().path.isEmpty());
        QVERIFY(window.styleSheet().isEmpty()); QCOMPARE(sidebarColor(), QColor("#ffffff"));
        QVERIFY(window.findChild<AppStorage *>()->value("background/path").toString().isEmpty());
    }
    void backgroundSettingsApplyCancelAndPersist()
    {
        QTemporaryDir fixture;
        const QString path = fixture.filePath(QStringLiteral("背景 图片.png"));
        QImage image(80, 60, QImage::Format_RGB32); image.fill(Qt::blue); QVERIFY(image.save(path));
        {
            MainWindow window(nullptr, testPaths()); window.newDocument(); window.newDocument();
            auto *tabs = window.findChild<QTabWidget *>();
            auto *first = qobject_cast<Editor *>(tabs->widget(0)); first->setPlainText("hello world"); first->document()->setModified(false);
            auto cursor = first->textCursor(); cursor.setPosition(1); cursor.setPosition(4, QTextCursor::KeepAnchor); first->setTextCursor(cursor);
            auto *action = window.findChild<QAction *>("actionBackgroundSettings"); QVERIFY(action);
            bool handled = false;
            QTimer::singleShot(0, &window, [&] {
                auto *dialog = qobject_cast<BackgroundSettingsDialog *>(QApplication::activeModalWidget());
                if (!dialog) return;
                dialog->findChild<QLineEdit *>("backgroundPath")->setText(path);
                dialog->findChild<QSpinBox *>("backgroundOpacity")->setValue(35);
                dialog->findChild<QSpinBox *>("backgroundBlur")->setValue(12);
                dialog->accept(); handled = true;
            });
            action->trigger(); QVERIFY(handled);
            for (int i = 0; i < tabs->count(); ++i) {
                auto *editor = qobject_cast<Editor *>(tabs->widget(i));
                QVERIFY(editor->background()->isActive()); QCOMPARE(editor->background()->settings().path, path);
                QCOMPARE(editor->background()->settings().opacity, 35); QCOMPARE(editor->background()->settings().blur, 12);
                QVERIFY(!editor->dirty());
            }
            QCOMPARE(first->textCursor().selectedText(), QString("ell"));
            window.newDocument(); QVERIFY(qobject_cast<Editor *>(tabs->currentWidget())->background()->isActive());
            window.findChild<QAction *>("actionClearHistory")->trigger();
            window.findChild<QAction *>("actionTheme")->trigger();
            QCOMPARE(first->background()->settings().path, path);
            handled = false;
            QTimer::singleShot(0, &window, [&] {
                auto *dialog = qobject_cast<BackgroundSettingsDialog *>(QApplication::activeModalWidget());
                if (!dialog) return;
                dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Reset)->click();
                const auto settings = dialog->selectedSettings();
                handled = settings.path.isEmpty() && settings.opacity == 20 && settings.blur == 0;
                dialog->reject();
            });
            action->trigger(); QVERIFY(handled); QCOMPARE(first->background()->settings().path, path);
            QCOMPARE(first->background()->settings().opacity, 35); QCOMPARE(first->background()->settings().blur, 12);
        }
        {
            MainWindow reloaded(nullptr, testPaths()); reloaded.newDocument();
            auto *editor = qobject_cast<Editor *>(reloaded.findChild<QTabWidget *>()->currentWidget());
            const auto settings = editor->background()->settings();
            QCOMPARE(settings.path, path); QCOMPARE(settings.opacity, 35); QCOMPARE(settings.blur, 12);
            // Moving a live background clears both the cached image and its persisted path.
            QVERIFY(QFile::rename(path, fixture.filePath("moved.png")));
            QTRY_VERIFY(editor->background()->settings().path.isEmpty()); QVERIFY(!editor->background()->isActive());
            QVERIFY(reloaded.findChild<AppStorage *>()->value("background/path").toString().isEmpty());
            QVERIFY(image.save(path)); QTest::qWait(150); QVERIFY(!editor->background()->isActive());
        }
        MainWindow cleared(nullptr, testPaths()); cleared.newDocument();
        QVERIFY(!qobject_cast<Editor *>(cleared.findChild<QTabWidget *>()->currentWidget())->background()->isActive());
    }
    void backgroundMissingOrCorruptOnStartupAndInDialog()
    {
        QTemporaryDir fixture;
        const QString path = fixture.filePath("invalid.png");
        for (bool corrupt : {false, true}) {
            if (corrupt) { QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("not an image"); }
            {
                AppStorage storage(testPaths()); QVERIFY(storage.initialize(false));
                QVERIFY(storage.setValue("background/path", path)); QVERIFY(storage.setValue("background/opacity", 70));
            }
            MainWindow window(nullptr, testPaths()); window.newDocument(); window.newDocument();
            for (auto *editor : window.findChildren<Editor *>()) {
                QVERIFY(!editor->background()->isActive()); QVERIFY(editor->background()->settings().path.isEmpty());
            }
            QVERIFY(window.findChild<AppStorage *>()->value("background/path").toString().isEmpty());
            BackgroundSettingsDialog dialog({path, 70, 20}, true);
            dialog.accept(); QVERIFY(dialog.result() != QDialog::Accepted);
            QVERIFY(!dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->isEnabled());
            dialog.findChild<QLineEdit *>("backgroundPath")->clear(); dialog.accept();
            QCOMPARE(dialog.result(), int(QDialog::Accepted));
        }
    }
    void backgroundReplacementAndDirectoryLoss()
    {
        QTemporaryDir fixture;
        const QString folder = fixture.filePath("images"); QVERIFY(QDir().mkpath(folder));
        const QString path = folder + "/background.png";
        QImage image(32, 32, QImage::Format_RGB32); image.fill(Qt::red); QVERIFY(image.save(path));
        EditorBackground background; QVERIFY(background.setSettings({path, 100, 0}));
        QSignalSpy changes(&background, &EditorBackground::changed);
        QTest::qWait(30);
        image.fill(Qt::blue); QVERIFY(image.save(path));
        QTRY_VERIFY(changes.count() > 0);
        QImage rendered(32, 32, QImage::Format_RGB32); rendered.fill(Qt::black);
        { QPainter painter(&rendered); background.paint(painter, rendered.rect()); }
        QCOMPARE(rendered.pixelColor(16, 16), QColor(Qt::blue));
        QVERIFY(QDir().rename(folder, fixture.filePath("renamed")));
        QTRY_VERIFY(background.settings().path.isEmpty()); QVERIFY(!background.isActive());
        QVERIFY(QDir().rename(fixture.filePath("renamed"), folder));
        QVERIFY(background.setSettings({path, 100, 0}));
        { QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate)); file.write("broken image"); }
        QTRY_VERIFY(background.settings().path.isEmpty()); QVERIFY(!background.isActive());
    }
    void treeSitterIndicatorFollowsCurrentDocumentAndStaysVisible()
    {
        QString executable = QCoreApplication::applicationDirPath() + "/nya_tree_sitter_tests";
#ifdef Q_OS_WIN
        executable += ".exe";
#endif
        AppStorage storage(testPaths()); QVERIFY(storage.initialize(false));
        QVERIFY(storage.setValue("language/treeSitterEnabled", true));
        QVERIFY(storage.setValue("language/treeSitterExecutable", executable));
        MainWindow window(nullptr, testPaths()); window.show();
        auto *indicator = window.findChild<QToolButton *>("treeSitterStatus"); QVERIFY(indicator);
        QCOMPARE(indicator->text(), QStringLiteral("Tree-sitter · 待命"));
        window.newDocument();
        auto *tabs = window.findChild<QTabWidget *>(); auto *editor = qobject_cast<Editor *>(tabs->currentWidget());
        editor->setLanguage("cpp"); editor->setPlainText("int value = 42;");
        QCOMPARE(indicator->text(), QStringLiteral("Tree-sitter · 解析中"));
        QTRY_COMPARE(indicator->text(), QStringLiteral("Tree-sitter · 正常"));
        QVERIFY(indicator->toolTip().contains(QStringLiteral("成功解析")));
        QTest::mouseClick(indicator, Qt::LeftButton);
        QVERIFY(!window.findChild<QMessageBox *>("treeSitterErrorDialog"));
        window.resize(760, 480); QApplication::processEvents();
        QVERIFY(indicator->mapTo(&window, QPoint()).x() < 30);
        window.statusBar()->showMessage(QStringLiteral("文件已保存"));
        QVERIFY(indicator->isVisible()); QCOMPARE(indicator->text(), QStringLiteral("Tree-sitter · 正常"));
        auto *message = window.findChild<QLabel *>("statusMessage"); QVERIFY(message->isVisible());
        QCOMPARE(message->text(), QStringLiteral("文件已保存"));
        window.statusBar()->clearMessage(); QVERIFY(indicator->isVisible()); QVERIFY(!message->isVisible());

        auto settings = editor->languageSettings(); settings.configPath = "fail";
        editor->setLanguageSettings(settings); window.newDocument();
        QCOMPARE(indicator->text(), QStringLiteral("Tree-sitter · 待命"));
        QTRY_VERIFY(editor->treeSitterStatus().state == TreeSitterState::Error);
        QCOMPARE(indicator->text(), QStringLiteral("Tree-sitter · 待命")); // Background errors belong to their document.
        tabs->setCurrentIndex(0); QCOMPARE(indicator->text(), QStringLiteral("Tree-sitter · 异常"));
        QVERIFY(indicator->toolTip().contains(QStringLiteral("基础着色")));
        QCOMPARE(indicator->cursor().shape(), Qt::PointingHandCursor);
        QTest::mouseClick(indicator, Qt::LeftButton);
        auto *errorDialog = window.findChild<QMessageBox *>("treeSitterErrorDialog"); QVERIFY(errorDialog);
        QVERIFY(errorDialog->isVisible()); QCOMPARE(errorDialog->textFormat(), Qt::PlainText);
        QVERIFY(errorDialog->informativeText().contains("No language found"));
        QVERIFY(errorDialog->informativeText().contains(QStringLiteral("设置 → 语言支持")));
        errorDialog->close(); QTRY_VERIFY(!window.findChild<QMessageBox *>("treeSitterErrorDialog"));

        settings.configPath = "slow"; editor->setLanguageSettings(settings); QTest::qWait(350);
        settings.treeSitterEnabled = false; editor->setLanguageSettings(settings);
        QCOMPARE(indicator->text(), QStringLiteral("Tree-sitter · 已关闭"));
        QTest::qWait(850); QCOMPARE(indicator->text(), QStringLiteral("Tree-sitter · 已关闭"));
        QTest::mouseClick(indicator, Qt::LeftButton); QVERIFY(!window.findChild<QMessageBox *>("treeSitterErrorDialog"));
        settings.treeSitterEnabled = true; settings.executable = "/nyaedit-missing/tree-sitter";
        editor->setLanguageSettings(settings); QTRY_COMPARE(indicator->text(), QStringLiteral("Tree-sitter · 异常"));
        QVERIFY(indicator->toolTip().contains(QStringLiteral("未找到")));
        indicator->setFocus(); QTest::keyClick(indicator, Qt::Key_Space);
        errorDialog = window.findChild<QMessageBox *>("treeSitterErrorDialog"); QVERIFY(errorDialog);
        QVERIFY(errorDialog->informativeText().contains(QStringLiteral("未找到")));
        QVERIFY(!errorDialog->informativeText().contains("No language found"));
        errorDialog->close(); QTRY_VERIFY(!window.findChild<QMessageBox *>("treeSitterErrorDialog"));
        editor->clear(); QTRY_COMPARE(indicator->text(), QStringLiteral("Tree-sitter · 待命"));
        QVERIFY(indicator->toolTip().contains(QStringLiteral("为空")));
        editor->largeFile = true; editor->setLanguage("cpp");
        QCOMPARE(indicator->text(), QStringLiteral("Tree-sitter · 待命"));
        QVERIFY(indicator->toolTip().contains(QStringLiteral("大文件")));
        editor->document()->setModified(false);
    }
    void languageSettingsApplyCancelAndPersist()
    {
        QString executable = QCoreApplication::applicationDirPath() + "/nya_tree_sitter_tests";
#ifdef Q_OS_WIN
        executable += ".exe";
#endif
        {
            MainWindow window(nullptr, testPaths()); window.newDocument(); window.newDocument();
            auto *tabs = window.findChild<QTabWidget *>();
            auto *action = window.findChild<QAction *>("actionLanguageSettings"); QVERIFY(action);
            bool handled = false;
            QTimer::singleShot(0, [&] {
                auto *dialog = qobject_cast<LanguageSettingsDialog *>(QApplication::activeModalWidget());
                if (!dialog) return;
                dialog->findChild<QCheckBox *>("treeSitterEnabled")->setChecked(true);
                dialog->reject(); handled = true;
            });
            action->trigger(); QVERIFY(handled);
            QVERIFY(!qobject_cast<Editor *>(tabs->currentWidget())->languageSettings().treeSitterEnabled);
            handled = false;
            QTimer::singleShot(0, [&] {
                auto *dialog = qobject_cast<LanguageSettingsDialog *>(QApplication::activeModalWidget());
                if (!dialog) return;
                dialog->findChild<QCheckBox *>("treeSitterEnabled")->setChecked(true);
                dialog->findChild<QLineEdit *>("treeSitterExecutable")->setText(executable);
                dialog->findChild<QLineEdit *>("treeSitterConfigPath")->setText("custom config.json");
                dialog->accept(); handled = true;
            });
            action->trigger(); QVERIFY(handled);
            for (int i = 0; i < tabs->count(); ++i) {
                auto *editor = qobject_cast<Editor *>(tabs->widget(i));
                QVERIFY(editor->languageSettings().treeSitterEnabled); QVERIFY(!editor->dirty());
                QCOMPARE(editor->languageSettings().executable, executable);
            }
            window.newDocument(); QVERIFY(qobject_cast<Editor *>(tabs->currentWidget())->languageSettings().treeSitterEnabled);
        }
        MainWindow reloaded(nullptr, testPaths()); reloaded.newDocument();
        auto *editor = qobject_cast<Editor *>(reloaded.findChild<QTabWidget *>()->currentWidget());
        QVERIFY(editor->languageSettings().treeSitterEnabled); QCOMPARE(editor->languageSettings().executable, executable);
        QCOMPARE(editor->languageSettings().configPath, QString("custom config.json"));
    }
    void treeSitterHighlightRefreshFallbackAndLargeFile()
    {
        QString executable = QCoreApplication::applicationDirPath() + "/nya_tree_sitter_tests";
#ifdef Q_OS_WIN
        executable += ".exe";
#endif
        Editor editor; editor.setDark(true); editor.setLanguage("cpp");
        editor.setPlainText(QStringLiteral("int 中文😀 = 3;\nreturn 0;")); editor.document()->setModified(false);
        auto firstColor = [&] {
            const auto formats = editor.document()->firstBlock().layout()->formats();
            for (const auto &format : formats) if (format.start == 0) return format.format.foreground().color();
            return QColor();
        };
        QSignalSpy status(&editor, &Editor::languageStatusChanged);
        LanguageSettings settings{true, executable, {}}; editor.setLanguageSettings(settings);
        QTRY_COMPARE(firstColor(), QColor("#a4d7a0")); QVERIFY(!editor.dirty()); QVERIFY(status.count() >= 1);
        const auto count = status.count(); QTest::qWait(650); QCOMPARE(status.count(), count); // Formatting must not re-launch the CLI.
        editor.setDark(false); QCOMPARE(firstColor(), QColor("#368650"));
        editor.moveCursor(QTextCursor::End); editor.insertPlainText("\n// unsaved");
        QTRY_VERIFY(editor.treeSitterStatus().state == TreeSitterState::Working); QCOMPARE(firstColor(), QColor("#368650"));
        settings.configPath = "slow"; editor.setLanguageSettings(settings); QTest::qWait(350);
        settings.treeSitterEnabled = false; editor.setLanguageSettings(settings);
        QCOMPARE(firstColor(), QColor("#8253bb")); QTest::qWait(900); QCOMPARE(firstColor(), QColor("#8253bb"));
        settings.treeSitterEnabled = true; settings.configPath = "fail"; editor.setLanguageSettings(settings);
        const auto beforeFailure = status.count(); QTRY_VERIFY(status.count() > beforeFailure);
        QCOMPARE(firstColor(), QColor("#8253bb"));
        editor.largeFile = true; editor.setLanguage("cpp"); settings.configPath.clear(); editor.setLanguageSettings(settings);
        const auto beforeLarge = status.count(); QTest::qWait(600); QCOMPARE(status.count(), beforeLarge);
        QVERIFY(editor.document()->firstBlock().layout()->formats().isEmpty());
    }
    void roundedMenusHaveTransparentCornersAndWorkingActions()
    {
        for (bool dark : {true, false}) {
            Theme::apply(*qApp, dark);
            QMenu menu; auto *action = menu.addAction(QStringLiteral("在文件管理器中显示"));
            menu.addSeparator(); auto *disabled = menu.addAction(QStringLiteral("未保存的文档")); disabled->setEnabled(false);
            QSignalSpy triggered(action, &QAction::triggered);
            menu.popup(QPoint(100, 100)); QTRY_VERIFY(menu.isVisible());
            QVERIFY(menu.testAttribute(Qt::WA_TranslucentBackground)); QVERIFY(menu.windowFlags().testFlag(Qt::FramelessWindowHint));
            const auto image = menu.grab().toImage();
            QVERIFY(image.pixelColor(0, 0).alpha() < 10);
            QVERIFY(image.pixelColor(image.width() / 2, image.height() / 2).alpha() > 240);
            menu.setActiveAction(action); QTest::keyClick(&menu, Qt::Key_Return); QCOMPARE(triggered.count(), 1);
            QLineEdit input; input.setText("hello");
            std::unique_ptr<QMenu> standard(input.createStandardContextMenu());
            standard->popup(QPoint(100, 100)); QTRY_VERIFY(standard->isVisible());
            QVERIFY(standard->testAttribute(Qt::WA_TranslucentBackground));
            QVERIFY(standard->grab().toImage().pixelColor(0, 0).alpha() < 10); standard->close();
        }
    }
    void fileTreeAnimationSurvivesScrollbarChanges()
    {
        QStandardItemModel model;
        auto *folder = new QStandardItem("folder"); model.appendRow(folder);
        for (int n = 0; n < 60; ++n) folder->appendRow(new QStandardItem(QString("file-%1.txt").arg(n)));
        FileTreeView tree; tree.setModel(&model); tree.setHeaderHidden(true); tree.resize(340, 240); tree.show();
        auto settings = AnimationSettings{}; settings.speed = 0.25; tree.setAnimationSettings(settings);
        tree.viewport()->grab(); auto *animation = tree.findChild<QVariantAnimation *>("treeTransition");
        tree.expand(folder->index()); tree.viewport()->grab(); QTest::qWait(30);
        QVERIFY(tree.verticalScrollBar()->isVisible()); QCOMPARE(animation->state(), QAbstractAnimation::Running);
        animation->setCurrentTime(animation->duration()); tree.viewport()->grab();
        tree.collapse(folder->index()); tree.viewport()->grab(); QTest::qWait(30);
        QVERIFY(!tree.verticalScrollBar()->isVisible()); QCOMPARE(animation->state(), QAbstractAnimation::Running);
    }
    void fileTreeAnimationCanRestartBeforeNextPaint()
    {
        QStandardItemModel model;
        auto *folder = new QStandardItem("folder"); model.appendRow(folder);
        folder->appendRow(new QStandardItem("child.txt"));
        FileTreeView tree; tree.setModel(&model); tree.setHeaderHidden(true); tree.resize(340, 240); tree.show();
        tree.viewport()->grab(); auto *animation = tree.findChild<QVariantAnimation *>("treeTransition");
        tree.expand(folder->index()); tree.viewport()->grab();
        QCOMPARE(animation->state(), QAbstractAnimation::Running);
        // Repeatedly reverse before Qt delivers the previous final repaint.
        for (int repeat = 0; repeat < 8; ++repeat) {
            animation->setCurrentTime(animation->duration());
            tree.collapse(folder->index()); tree.viewport()->grab();
            QCOMPARE(animation->state(), QAbstractAnimation::Running);
            animation->setCurrentTime(animation->duration());
            tree.expand(folder->index()); tree.viewport()->grab();
            QCOMPARE(animation->state(), QAbstractAnimation::Running);
        }
    }
    void fileTreeLazyLoadingAndScrollbarChangesAnimateTogether()
    {
        QTemporaryDir fixture; QString error;
        const auto folder = fixture.filePath("large-folder"); QVERIFY(QDir().mkpath(folder));
        for (int n = 0; n < 40; ++n)
            QVERIFY(saveFile(folder + QString("/file-%1.txt").arg(n), "hello", {}, LineEnding::LF, &error));
        MainWindow window(nullptr, testPaths()); window.show(); window.openFolder(fixture.path());
        auto *tree = window.findChild<FileTreeView *>("fileTree"); auto *model = qobject_cast<QFileSystemModel *>(tree->model());
        QTRY_COMPARE(model->rowCount(tree->rootIndex()), 1);
        const QPersistentModelIndex index = model->index(QFileInfo(folder).canonicalFilePath());
        auto settings = AnimationSettings{}; settings.speed = 0.25; tree->setAnimationSettings(settings);
        tree->viewport()->grab(); auto *animation = tree->findChild<QVariantAnimation *>("treeTransition");
        for (int repeat = 0; repeat < 4; ++repeat) {
            tree->expand(index); QTRY_COMPARE(model->rowCount(index), 40); tree->viewport()->grab(); QTest::qWait(20);
            QVERIFY(tree->verticalScrollBar()->isVisible()); QCOMPARE(animation->state(), QAbstractAnimation::Running);
            animation->setCurrentTime(animation->duration() / 2); const auto middle = tree->viewport()->grab().toImage();
            animation->setCurrentTime(animation->duration()); const auto expanded = tree->viewport()->grab().toImage();
            QVERIFY(middle != expanded);
            tree->collapse(index); tree->viewport()->grab(); QTest::qWait(20);
            QVERIFY(!tree->verticalScrollBar()->isVisible()); QCOMPARE(animation->state(), QAbstractAnimation::Running);
            animation->setCurrentTime(animation->duration());
        }
    }
    void fileTreeAndSearchResultContextMenusRevealClickedPaths()
    {
        QTemporaryDir fixture; QString error;
        const auto file = fixture.filePath(QStringLiteral("笔记 ' 空格 $.txt")), folder = fixture.filePath(QStringLiteral("子 目录"));
        QVERIFY(saveFile(file, "hello", {}, LineEnding::LF, &error)); QVERIFY(QDir().mkpath(folder));
        const auto actualFile = QFileInfo(file).canonicalFilePath(), actualFolder = QFileInfo(folder).canonicalFilePath();
        RevealTestWindow window(nullptr, testPaths()); window.show(); window.openFolder(fixture.path());
        auto *tree = window.findChild<FileTreeView *>("fileTree"); auto *model = qobject_cast<QFileSystemModel *>(tree->model());
        QTRY_COMPARE(model->rowCount(tree->rootIndex()), 2);
        const QPersistentModelIndex fileIndex = model->index(actualFile), folderIndex = model->index(actualFolder);
        tree->setCurrentIndex(fileIndex);
        QTRY_VERIFY(!tree->visualRect(fileIndex).isEmpty());
        auto result = activateReveal(window, tree->viewport(), tree->visualRect(fileIndex).center());
        QVERIFY(result.shown); QVERIFY(result.enabled); QCOMPARE(result.path, actualFile);
        QCOMPARE(window.revealedPaths, QStringList{actualFile});
        result = activateReveal(window, tree->viewport(), tree->visualRect(folderIndex).center());
        QVERIFY(result.shown); QVERIFY(result.enabled); QCOMPARE(result.path, actualFolder);
        QCOMPARE(window.revealedPaths.last(), actualFolder);
        const QPoint blank(10, tree->viewport()->height() - 5); QVERIFY(!tree->indexAt(blank).isValid());
        result = activateReveal(window, tree->viewport(), blank); QVERIFY(!result.shown); QCOMPARE(window.revealedPaths.size(), 2);
        auto *input = window.findChild<QLineEdit *>("fileSearchInput"); input->setText(QStringLiteral("笔记"));
        auto *results = window.findChild<QListWidget *>("fileSearchResults"); QTRY_COMPARE(results->count(), 1);
        result = activateReveal(window, results->viewport(), results->visualItemRect(results->item(0)).center());
        QVERIFY(result.shown); QVERIFY(result.enabled); QCOMPARE(result.path, actualFile); QCOMPARE(window.revealedPaths.last(), actualFile);
        QCOMPARE(window.findChild<AppStorage *>()->history().size(), 1);
    }
    void tabContextMenusUseClickedTabAndDisableUnsavedOrMissingFiles()
    {
        QTemporaryDir fixture; QString error;
        const auto firstPath = fixture.filePath(QStringLiteral("第一份 空格.txt")), secondPath = fixture.filePath("second.txt");
        for (const auto &path : {firstPath, secondPath}) QVERIFY(saveFile(path, "hello", {}, LineEnding::LF, &error));
        const auto first = QFileInfo(firstPath).canonicalFilePath(), second = QFileInfo(secondPath).canonicalFilePath();
        RevealTestWindow window(nullptr, testPaths()); window.show(); window.openPath(firstPath);
        auto *tabs = window.findChild<QTabWidget *>();
        QTRY_VERIFY(tabs->count() == 1 && !qobject_cast<Editor *>(tabs->currentWidget())->loading);
        window.openPath(secondPath); QTRY_VERIFY(tabs->count() == 2 && !qobject_cast<Editor *>(tabs->currentWidget())->loading);
        window.newDocument(); QWidget *unsaved = tabs->currentWidget(); auto *bar = tabs->tabBar();
        auto result = activateReveal(window, bar, bar->tabRect(0).center());
        QVERIFY(result.shown); QVERIFY(result.enabled); QCOMPARE(result.path, first);
        QCOMPARE(window.revealedPaths, QStringList{first}); QCOMPARE(tabs->currentWidget(), unsaved);
        bar->moveTab(0, 1);
        result = activateReveal(window, bar, bar->tabRect(0).center());
        QVERIFY(result.enabled); QCOMPARE(result.path, second); QCOMPARE(window.revealedPaths.last(), second);
        auto *documents = window.findChild<QListWidget *>("openDocuments");
        result = activateReveal(window, documents->viewport(), documents->visualItemRect(documents->item(1)).center());
        QVERIFY(result.shown); QVERIFY(result.enabled); QCOMPARE(result.path, first); QCOMPARE(window.revealedPaths.last(), first);
        const int revealed = window.revealedPaths.size();
        result = activateReveal(window, bar, bar->tabRect(tabs->indexOf(unsaved)).center());
        QVERIFY(result.shown); QVERIFY(!result.enabled); QVERIFY(result.path.isEmpty());
        QVERIFY(result.tooltip.contains(QStringLiteral("保存"))); QCOMPARE(window.revealedPaths.size(), revealed);
        QVERIFY(QFile::remove(firstPath));
        result = activateReveal(window, bar, bar->tabRect(1).center());
        QVERIFY(result.shown); QVERIFY(!result.enabled); QCOMPARE(result.path, first);
        QCOMPARE(window.revealedPaths.size(), revealed); QCOMPARE(tabs->currentWidget(), unsaved);
    }
    void fileTreeTransitionsExpandCollapseAndFollowSettings()
    {
        QStandardItemModel model;
        auto *folder = new QStandardItem(QStringLiteral("文件夹")); model.appendRow(folder);
        folder->appendRow(new QStandardItem(QStringLiteral("内部文件.txt")));
        auto *nested = new QStandardItem(QStringLiteral("子目录")); folder->appendRow(nested);
        nested->appendRow(new QStandardItem("nested.txt"));
        model.appendRow(new QStandardItem("outside.txt"));
        FileTreeView tree; tree.setModel(&model); tree.setHeaderHidden(true); tree.resize(400, 320); tree.show();
        auto settings = AnimationSettings{}; settings.speed = 0.25; tree.setAnimationSettings(settings);
        tree.viewport()->grab(); auto *animation = tree.findChild<QVariantAnimation *>("treeTransition"); QVERIFY(animation);
        const auto index = folder->index();
        tree.expand(index); QVERIFY(tree.isExpanded(index)); tree.viewport()->grab();
        QCOMPARE(animation->state(), QAbstractAnimation::Running); QCOMPARE(animation->duration(), 720);
        animation->setCurrentTime(360); const auto midway = tree.viewport()->grab().toImage();
        animation->setCurrentTime(720); const auto expanded = tree.viewport()->grab().toImage();
        QVERIFY(midway != expanded);
        tree.collapse(index); QVERIFY(!tree.isExpanded(index)); tree.viewport()->grab(); QCOMPARE(animation->state(), QAbstractAnimation::Running);
        animation->setCurrentTime(720); const auto collapsed = tree.viewport()->grab().toImage();
        QVERIFY(collapsed != expanded);
        // A reversal takes effect immediately and continues from the current height.
        tree.expand(index); tree.viewport()->grab(); animation->setCurrentTime(200); tree.viewport()->grab();
        tree.collapse(index); QVERIFY(!tree.isExpanded(index)); tree.viewport()->grab(); QCOMPARE(animation->state(), QAbstractAnimation::Running);
        settings.interfaceEnabled = false; tree.setAnimationSettings(settings);
        QCOMPARE(animation->state(), QAbstractAnimation::Stopped); tree.viewport()->grab();
        tree.expand(index); QVERIFY(tree.isExpanded(index)); QCOMPARE(animation->state(), QAbstractAnimation::Stopped);
        settings.interfaceEnabled = true; settings.speed = 2; tree.setAnimationSettings(settings); tree.viewport()->grab();
        tree.collapse(index); tree.viewport()->grab(); QCOMPARE(animation->duration(), 90);
        tree.hide(); QCOMPARE(animation->state(), QAbstractAnimation::Stopped);
    }
    void fileTreeFirstDirectoryLoadAnimatesAndThemeChangesKeepSettings()
    {
        QTemporaryDir fixture; QString error;
        const auto folder = fixture.filePath(QStringLiteral("目录")); QVERIFY(QDir().mkpath(folder));
        QVERIFY(saveFile(folder + "/child.txt", "hello", {}, LineEnding::LF, &error));
        MainWindow window(nullptr, testPaths()); window.show(); window.openFolder(fixture.path());
        auto *tree = window.findChild<FileTreeView *>("fileTree"); QVERIFY(tree);
        auto *model = qobject_cast<QFileSystemModel *>(tree->model()); QVERIFY(model);
        QTRY_VERIFY(model->rowCount(tree->rootIndex()) > 0);
        const auto index = model->index(folder); QVERIFY(index.isValid());
        tree->viewport()->grab(); auto *animation = tree->findChild<QVariantAnimation *>("treeTransition"); QVERIFY(animation);
        tree->expand(index);
        QTRY_VERIFY(model->rowCount(index) > 0);
        tree->viewport()->grab();
        QCOMPARE(animation->state(), QAbstractAnimation::Running); QCOMPARE(animation->duration(), 180);
        animation->setCurrentTime(180); tree->viewport()->grab();
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = qobject_cast<AnimationSettingsDialog *>(QApplication::activeModalWidget());
            if (!dialog) return;
            dialog->findChild<QDoubleSpinBox *>("animationSpeed")->setValue(0.5); dialog->accept();
        });
        window.findChild<QAction *>("actionAnimationSettings")->trigger();
        window.findChild<QAction *>("actionTheme")->trigger(); tree->viewport()->grab();
        tree->collapse(index); tree->viewport()->grab(); QCOMPARE(animation->duration(), 360); QCOMPARE(animation->state(), QAbstractAnimation::Running);
        // Switching roots during a transition must discard pictures of the old folder.
        window.openFolder(folder); QCOMPARE(animation->state(), QAbstractAnimation::Stopped);
        QCOMPARE(tree->rootIndex(), index);
    }
    void animationSettingsApplyToAllDocumentsAndPersist()
    {
        {
            MainWindow window(nullptr, testPaths()); window.newDocument(); window.show();
            auto *tabs = window.findChild<QTabWidget *>(); auto *first = qobject_cast<Editor *>(tabs->currentWidget());
            first->setPlainText(QStringLiteral("hello 世界\ntext")); first->document()->setModified(false);
            auto cursor = first->textCursor(); cursor.setPosition(1); cursor.setPosition(4, QTextCursor::KeepAnchor); first->setTextCursor(cursor);
            window.newDocument(); auto *second = qobject_cast<Editor *>(tabs->currentWidget());
            QTimer::singleShot(0, &window, [&] {
                auto *dialog = qobject_cast<AnimationSettingsDialog *>(QApplication::activeModalWidget());
                if (!dialog) return;
                dialog->findChild<QCheckBox *>("interfaceAnimation")->setChecked(false);
                dialog->findChild<QCheckBox *>("cursorAnimation")->setChecked(false);
                dialog->findChild<QDoubleSpinBox *>("animationSpeed")->setValue(2.0);
                dialog->accept();
            });
            window.findChild<QAction *>("actionAnimationSettings")->trigger();
            for (auto *editor : {first, second}) {
                QVERIFY(!editor->animationSettings().interfaceEnabled); QVERIFY(!editor->animationSettings().cursorEnabled);
                QVERIFY(editor->animationSettings().selectionEnabled); QCOMPARE(editor->animationSettings().speed, 2.0);
                QCOMPARE(editor->cursorWidth(), 1); QVERIFY(!editor->dirty());
            }
            QCOMPARE(first->textCursor().selectedText(), QStringLiteral("ell"));
            QCOMPARE(first->contents(), QStringLiteral("hello 世界\ntext"));
            window.newDocument();
            QCOMPARE(qobject_cast<Editor *>(tabs->currentWidget())->animationSettings().speed, 2.0);
            bool resetWorked = false;
            QTimer::singleShot(0, &window, [&] {
                auto *dialog = qobject_cast<AnimationSettingsDialog *>(QApplication::activeModalWidget());
                if (!dialog) return;
                dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Reset)->click();
                const auto defaults = dialog->selectedSettings();
                resetWorked = defaults.interfaceEnabled && defaults.cursorEnabled && defaults.selectionEnabled && defaults.speed == 1.0;
                dialog->reject();
            });
            window.findChild<QAction *>("actionAnimationSettings")->trigger(); QVERIFY(resetWorked);
            QVERIFY(!first->animationSettings().cursorEnabled); QCOMPARE(first->animationSettings().speed, 2.0);
            QVERIFY(window.close());
        }
        MainWindow reloaded(nullptr, testPaths()); reloaded.newDocument();
        const auto settings = qobject_cast<Editor *>(reloaded.findChild<QTabWidget *>()->currentWidget())->animationSettings();
        QVERIFY(!settings.interfaceEnabled); QVERIFY(!settings.cursorEnabled); QVERIFY(settings.selectionEnabled); QCOMPARE(settings.speed, 2.0);
    }
    void panelAnimationsReverseAndRestoreLayout()
    {
        QSplitter splitter; splitter.resize(900, 500); splitter.setChildrenCollapsible(false);
        auto *panel = new QFrame(&splitter); panel->setMinimumWidth(170);
        auto *workspace = new QWidget(&splitter); workspace->setMinimumWidth(300);
        splitter.setSizes({280, 620}); splitter.show(); QTest::qWait(20);
        const int width = panel->width(); PanelTransition transition(panel, Qt::Horizontal);
        transition.setExpanded(false, 200); QTest::qWait(40);
        QVERIFY(panel->width() < width); QVERIFY(!panel->isHidden());
        transition.setExpanded(true, 100);
        QTRY_COMPARE(panel->minimumWidth(), 170); QTRY_COMPARE(panel->width(), width);
        QCOMPARE(panel->maximumWidth(), QWIDGETSIZE_MAX);
        transition.setExpanded(false, 100); QTRY_VERIFY(panel->isHidden());
        transition.setExpanded(true, 0); QTRY_COMPARE(panel->width(), width); QVERIFY(panel->isVisible());
        transition.setExpanded(false, 100); transition.finish(); QVERIFY(panel->isHidden());
        transition.setExpanded(true, 100); transition.finish(); QTRY_COMPARE(panel->width(), width);
        splitter.setSizes({350, 550}); const int resized = panel->width();
        transition.finish(); QCOMPARE(panel->width(), resized);
    }
    void cursorAndSelectionAnimationPreserveEditing()
    {
        Theme::apply(*qApp, true);
        Editor editor; editor.resize(640, 360); editor.setPlainText(QStringLiteral("hello 世界\nsecond line\nthird line"));
        editor.document()->setModified(false); editor.show(); editor.activateWindow(); editor.setFocus(); QTRY_VERIFY(editor.hasFocus());
        auto *motion = editor.findChild<QVariantAnimation *>("cursorMotion");
        auto *fade = editor.findChild<QVariantAnimation *>("selectionFade");
        auto *blink = editor.findChild<QVariantAnimation *>("cursorBlink");
        QVERIFY(motion); QVERIFY(fade); QVERIFY(blink);
        QTest::keyClick(&editor, Qt::Key_Right);
        QCOMPARE(editor.textCursor().position(), 1); QCOMPARE(motion->state(), QAbstractAnimation::Running);
        QCOMPARE(motion->duration(), 90);
        motion->setCurrentTime(motion->duration());
        QCOMPARE(motion->currentValue().toRectF().topLeft(), QPointF(editor.cursorRect().topLeft()));
        QTest::keyClick(&editor, Qt::Key_Right, Qt::ShiftModifier);
        QCOMPARE(editor.textCursor().selectedText(), QStringLiteral("e")); QCOMPARE(fade->state(), QAbstractAnimation::Running);
        fade->setCurrentTime(fade->duration()); QCOMPARE(editor.palette().color(QPalette::Highlight), QColor("#307baa"));
        auto settings = editor.animationSettings(); settings.selectionEnabled = false; settings.cursorEnabled = false;
        editor.setAnimationSettings(settings);
        QCOMPARE(motion->state(), QAbstractAnimation::Stopped); QCOMPARE(blink->state(), QAbstractAnimation::Stopped);
        QCOMPARE(editor.cursorWidth(), 1); QCOMPARE(editor.textCursor().selectedText(), QStringLiteral("e"));
        QTest::keyClick(&editor, Qt::Key_Right, Qt::ShiftModifier);
        QCOMPARE(editor.textCursor().selectedText(), QStringLiteral("el")); QCOMPARE(fade->state(), QAbstractAnimation::Stopped);
        QVERIFY(!editor.dirty());
        QTest::keyClicks(&editor, "X"); QCOMPARE(editor.contents(), QStringLiteral("hXlo 世界\nsecond line\nthird line"));
        editor.undo(); QCOMPARE(editor.contents(), QStringLiteral("hello 世界\nsecond line\nthird line"));
        settings.cursorEnabled = true; settings.speed = 2.0; editor.setAnimationSettings(settings);
        editor.setFontSize(24); QTest::keyClick(&editor, Qt::Key_Right);
        QCOMPARE(motion->duration(), 45);
        motion->setCurrentTime(motion->duration());
        QCOMPARE(motion->currentValue().toRectF().height(), qreal(editor.cursorRect().height()));
        editor.hide(); QCOMPARE(blink->state(), QAbstractAnimation::Stopped);
    }
    void sidebarAnimationPreservesHiddenWidthAcrossRestart()
    {
        int width = 0;
        {
            MainWindow window(nullptr, testPaths()); window.show(); QTest::qWait(20);
            auto *sidebar = window.findChild<QFrame *>("sidebar");
            window.findChild<QSplitter *>()->setSizes({340, 820}); width = sidebar->width();
            window.findChild<QAction *>("actionSidebar")->trigger(); QTRY_VERIFY(sidebar->isHidden());
            QVERIFY(window.close());
        }
        MainWindow window(nullptr, testPaths()); window.show();
        auto *sidebar = window.findChild<QFrame *>("sidebar"); QVERIFY(sidebar->isHidden());
        window.findChild<QAction *>("actionSidebar")->trigger();
        QTRY_COMPARE(sidebar->maximumWidth(), QWIDGETSIZE_MAX); QTRY_COMPARE(sidebar->width(), width);
    }
    void selectionContrastIsPaintedInBothThemes()
    {
        const auto luminance = [](QColor color) {
            const auto linear = [](double c) { return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); };
            return 0.2126 * linear(color.redF()) + 0.7152 * linear(color.greenF()) + 0.0722 * linear(color.blueF());
        };
        for (bool dark : {true, false}) {
            Theme::apply(*qApp, dark);
            Editor editor; editor.setDark(dark); auto settings = editor.animationSettings(); settings.selectionEnabled = false;
            editor.setAnimationSettings(settings); editor.resize(600, 300);
            editor.setPlainText(QStringLiteral("selected text       \n下一行也被选择\nlast line")); editor.show(); editor.selectAll();
            const QColor color = editor.palette().color(QPalette::Highlight), background(dark ? "#141820" : "#f5f7fa");
            const double selected = luminance(color), base = luminance(background);
            QVERIFY((qMax(selected, base) + 0.05) / (qMin(selected, base) + 0.05) > 3.0);
            QVERIFY(1.05 / (selected + 0.05) > 4.5);
            const auto image = editor.viewport()->grab().toImage(); int selectedPixels = 0;
            for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x)
                if (image.pixelColor(x, y).rgb() == color.rgb()) ++selectedPixels;
            QVERIFY2(selectedPixels > 500, "The actual text selection must use the high-contrast palette, including without focus.");
        }
    }
    void searchPanelAnimationCanBeInterrupted()
    {
        MainWindow window(nullptr, testPaths()); window.newDocument(); window.show();
        auto *panel = window.findChild<QFrame *>("searchPanel"); auto *find = window.findChild<QAction *>("actionFind");
        auto *editor = qobject_cast<Editor *>(window.findChild<QTabWidget *>()->currentWidget());
        editor->setPlainText("one two one"); editor->document()->setModified(false);
        find->trigger(); window.findChild<QLineEdit *>("findInput")->setText("one");
        QTest::keyClick(window.findChild<QLineEdit *>("findInput"), Qt::Key_Escape); find->trigger();
        QTRY_COMPARE(panel->maximumHeight(), QWIDGETSIZE_MAX); QVERIFY(panel->isVisible());
        QTRY_COMPARE(window.findChild<QLabel *>("matchCount")->text(), QStringLiteral("2 个匹配"));
        QTest::keyClick(window.findChild<QLineEdit *>("findInput"), Qt::Key_Escape);
        QTRY_VERIFY(panel->isHidden()); QVERIFY(editor->extraSelections().size() == 1);
    }
    void fontSettingsApplyToAllDocumentsAndPersist()
    {
        QString family = Editor::defaultEditorFont().family();
        for (const auto &candidate : QFontDatabase().families()) {
            if (!candidate.startsWith('.') && candidate != family) { family = candidate; break; }
        }
        {
            MainWindow window(nullptr, testPaths()); window.newDocument(); window.show();
            auto *tabs = window.findChild<QTabWidget *>();
            auto *first = qobject_cast<Editor *>(tabs->currentWidget());
            first->setPlainText(QStringLiteral("hello 世界\ntext")); first->document()->setModified(false);
            auto cursor = first->textCursor(); cursor.setPosition(1); cursor.setPosition(4, QTextCursor::KeepAnchor);
            first->setTextCursor(cursor);
            window.newDocument();
            auto *second = qobject_cast<Editor *>(tabs->currentWidget());
            bool previewCorrect = false;
            QTimer::singleShot(0, &window, [&] {
                auto *dialog = qobject_cast<FontSettingsDialog *>(QApplication::activeModalWidget());
                if (!dialog) return;
                dialog->findChild<QFontComboBox *>("editorFontFamily")->setCurrentFont(QFont(family));
                dialog->findChild<QSpinBox *>("editorFontSize")->setValue(48);
                const auto previewFont = dialog->findChild<QPlainTextEdit *>("fontPreview")->font();
                previewCorrect = previewFont.family() == family && previewFont.pointSize() == 48;
                dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
            });
            window.findChild<QAction *>("actionFontSettings")->trigger();
            QVERIFY(previewCorrect);
            for (auto *editor : {first, second}) {
                QCOMPARE(editor->editorFont().family(), family); QCOMPARE(editor->fontSize(), 48);
                QVERIFY(!editor->dirty());
                QCOMPARE(editor->tabStopDistance(), QFontMetricsF(editor->font(), editor->viewport()).horizontalAdvance(' ') * 4);
            }
            QCOMPARE(first->contents(), QStringLiteral("hello 世界\ntext"));
            QCOMPARE(first->textCursor().selectionStart(), 1); QCOMPARE(first->textCursor().selectionEnd(), 4);
            window.findChild<QAction *>("actionTheme")->trigger();
            for (auto *editor : {first, second}) {
                QCOMPARE(editor->font().family(), family); QCOMPARE(editor->font().pointSize(), 48);
            }
            window.newDocument();
            QCOMPARE(qobject_cast<Editor *>(tabs->currentWidget())->editorFont().family(), family);
            QCOMPARE(qobject_cast<Editor *>(tabs->currentWidget())->fontSize(), 48);
            QVERIFY(window.close());
        }
        MainWindow window(nullptr, testPaths()); window.newDocument();
        auto *editor = qobject_cast<Editor *>(window.findChild<QTabWidget *>()->currentWidget());
        QCOMPARE(editor->editorFont().family(), family); QCOMPARE(editor->fontSize(), 48);
        editor->setFontSize(49);
        QCOMPARE(editor->editorFont().family(), family);
        QCOMPARE(window.findChild<AppStorage *>()->value("editor/fontSize").toInt(), 49);
    }
    void fontSettingsFromWelcomeCancelAndReset()
    {
        MainWindow window(nullptr, testPaths()); window.show();
        auto *storage = window.findChild<AppStorage *>();
        bool resetWorked = false;
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = qobject_cast<FontSettingsDialog *>(QApplication::activeModalWidget());
            if (!dialog) return;
            auto *size = dialog->findChild<QSpinBox *>("editorFontSize");
            auto *buttons = dialog->findChild<QDialogButtonBox *>();
            size->setValue(64);
            buttons->button(QDialogButtonBox::Reset)->click();
            resetWorked = dialog->selectedFont().family() == Editor::defaultEditorFont().family()
                && dialog->selectedFont().pointSize() == 13;
            size->setValue(36); buttons->button(QDialogButtonBox::Cancel)->click();
        });
        window.findChild<QAction *>("actionFontSettings")->trigger();
        QVERIFY(resetWorked); QCOMPARE(storage->value("editor/fontSize").toInt(), 13);
        QVERIFY(storage->value("editor/fontFamily").toString().isEmpty());
        QCOMPARE(window.findChild<QTabWidget *>()->count(), 0);
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = qobject_cast<FontSettingsDialog *>(QApplication::activeModalWidget());
            if (!dialog) return;
            dialog->findChild<QSpinBox *>("editorFontSize")->setValue(24);
            dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        });
        window.findChild<QAction *>("actionFontSettings")->trigger();
        window.newDocument();
        QCOMPARE(qobject_cast<Editor *>(window.findChild<QTabWidget *>()->currentWidget())->fontSize(), 24);
    }
    void fuzzyFileSearchContextMenuAndPersistence()
    {
        QTemporaryDir fixture; QString error;
        QVERIFY(saveFile(fixture.filePath("MainWindow.cpp"), "fixture", {}, LineEnding::LF, &error));
        {
            MainWindow window(nullptr, testPaths()); window.show(); window.openFolder(fixture.path());
            auto *input = window.findChild<QLineEdit *>("fileSearchInput");
            auto *results = window.findChild<QListWidget *>("fileSearchResults");
            auto *status = window.findChild<QLabel *>("fileSearchStatus");
            auto *toggle = window.findChild<QAction *>("actionFileSearchFuzzy");
            auto *hint = window.findChild<QLabel *>("fileSearchHint");
            QVERIFY(toggle->isChecked()); QVERIFY(hint->isVisible());
            QVERIFY(hint->text().contains(QStringLiteral("右键")));
            input->setText("mw"); QTRY_COMPARE(results->count(), 1);
            bool menuHasToggle = false;
            QTimer::singleShot(0, &window, [&] {
                auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
                if (!menu) return;
                menuHasToggle = menu->actions().contains(toggle);
                if (menuHasToggle) toggle->trigger();
                menu->close();
            });
            QContextMenuEvent event(QContextMenuEvent::Mouse, input->rect().center(),
                                    input->mapToGlobal(input->rect().center()));
            QApplication::sendEvent(input, &event);
            QVERIFY(menuHasToggle); QVERIFY(!toggle->isChecked());
            QTRY_COMPARE(status->text(), QStringLiteral("没有匹配的文件"));
            QCOMPARE(results->count(), 0);
            QVERIFY(hint->text().contains(QStringLiteral("连续匹配")));
            input->setText("window"); QTRY_COMPARE(results->count(), 1);
            // Changing modes with a search pending must discard results from the old mode.
            input->setText("mw"); toggle->setChecked(true); toggle->setChecked(false);
            QTRY_COMPARE(status->text(), QStringLiteral("没有匹配的文件"));
            QCOMPARE(results->count(), 0);
        }
        MainWindow reloaded(nullptr, testPaths());
        auto *toggle = reloaded.findChild<QAction *>("actionFileSearchFuzzy");
        QVERIFY(!toggle->isChecked());
        toggle->setChecked(true);
        QVERIFY(reloaded.findChild<AppStorage *>()->value("search/fuzzy").toBool());
    }
    void startsOnWelcomeAndLastTabReturnsThere()
    {
        MainWindow window(nullptr, testPaths()); window.show();
        auto *tabs = window.findChild<QTabWidget *>(); auto *welcome = window.findChild<WelcomePage *>();
        QCOMPARE(tabs->count(), 0); QVERIFY(welcome->isVisible());
        QVERIFY(!window.findChild<QAction *>("actionSave")->isEnabled());
        window.findChild<QPushButton *>("primary")->click();
        QCOMPARE(tabs->count(), 1); QVERIFY(welcome->isHidden());
        window.findChild<QAction *>("actionWelcome")->trigger();
        QVERIFY(welcome->isVisible()); QVERIFY(!window.findChild<QAction *>("actionSave")->isEnabled());
        // Selecting the already-current document must leave the welcome page too.
        auto *documents = window.findChild<QListWidget *>("openDocuments");
        QCOMPARE(documents->currentRow(), 0);
        QTest::mouseClick(documents->viewport(), Qt::LeftButton, Qt::NoModifier,
                          documents->visualItemRect(documents->item(0)).center());
        QVERIFY(welcome->isHidden());
        QVERIFY(QMetaObject::invokeMethod(tabs, "tabCloseRequested", Qt::DirectConnection, Q_ARG(int, 0)));
        QCOMPARE(tabs->count(), 0); QVERIFY(welcome->isVisible());
    }
    void welcomeRestoresSeparateFilesAndFolders()
    {
        QTemporaryDir fixture; QString error; const auto file = fixture.filePath(QStringLiteral("独立文件.txt"));
        QVERIFY(saveFile(file, "hello", {}, LineEnding::LF, &error));
        {
            MainWindow window(nullptr, testPaths()); window.openPath(file);
            auto *tabs = window.findChild<QTabWidget *>();
            QTRY_VERIFY(tabs->count() == 1 && !qobject_cast<Editor *>(tabs->currentWidget())->loading);
            window.openFolder(fixture.path());
            QCOMPARE(window.findChild<AppStorage *>()->history().size(), 2);
        }
        MainWindow window(nullptr, testPaths()); window.show();
        auto *history = window.findChild<QListWidget *>("welcomeHistory");
        auto *tabs = window.findChild<QTabWidget *>();
        QCOMPARE(tabs->count(), 0); QCOMPARE(history->count(), 2);
        QVERIFY(history->item(0)->data(Qt::UserRole + 1).toBool());
        history->setCurrentRow(0); QTest::keyClick(history, Qt::Key_Return);
        QVERIFY(window.findChild<QLineEdit *>("fileSearchInput")->isEnabled());
        history->setCurrentRow(1); QTest::keyClick(history, Qt::Key_Return);
        QTRY_VERIFY(tabs->count() == 1 && !qobject_cast<Editor *>(tabs->currentWidget())->loading);
        QCOMPARE(qobject_cast<Editor *>(tabs->currentWidget())->contents(), QString("hello"));
    }
    void oneClickClearHistoryPersistsAndKeepsEditorPreferences()
    {
        QTemporaryDir fixture; QString error;
        const auto path = fixture.filePath("saved.txt");
        QVERIFY(saveFile(path, "original", {}, LineEnding::LF, &error));
        {
            MainWindow window(nullptr, testPaths()); window.newDocument();
            qobject_cast<Editor *>(window.findChild<QTabWidget *>()->currentWidget())->setFontSize(17);
            window.findChild<QAction *>("actionWordWrap")->setChecked(true);
            window.findChild<QAction *>("actionTheme")->trigger();
            window.openPath(path);
            auto *tabs = window.findChild<QTabWidget *>();
            QTRY_VERIFY(tabs->count() == 1 && !qobject_cast<Editor *>(tabs->currentWidget())->loading);
            window.openFolder(fixture.path());
            QVERIFY(!window.findChild<AppStorage *>()->history().isEmpty());
            window.findChild<QAction *>("actionClearHistory")->trigger();
            auto *storage = window.findChild<AppStorage *>();
            QVERIFY(storage->history().isEmpty()); QVERIFY(storage->lastDirectory().isEmpty());
            QCOMPARE(window.findChild<QListWidget *>("welcomeHistory")->count(), 0);
            auto *editor = qobject_cast<Editor *>(tabs->currentWidget());
            editor->insertPlainText("saved after clearing ");
            window.findChild<QAction *>("actionSave")->trigger();
            QVERIFY(!editor->dirty()); QVERIFY(storage->history().isEmpty());
            QVERIFY(window.close());
        }
        MainWindow window(nullptr, testPaths());
        QVERIFY(window.findChild<AppStorage *>()->history().isEmpty());
        QVERIFY(!window.findChild<AppStorage *>()->value("appearance/dark").toBool());
        window.newDocument();
        auto *editor = qobject_cast<Editor *>(window.findChild<QTabWidget *>()->currentWidget());
        QCOMPARE(editor->fontSize(), 17); QCOMPARE(editor->lineWrapMode(), QPlainTextEdit::WidgetWidth);
    }
    void filesInOpenFolderDoNotEnterHistory()
    {
        QTemporaryDir fixture; QString error;
        const auto root = fixture.filePath("workspace"), sibling = fixture.filePath("workspace-other");
        QVERIFY(QDir().mkpath(root + QStringLiteral("/子目录"))); QVERIFY(QDir().mkpath(sibling));
        const auto direct = root + "/direct.txt", nested = root + QStringLiteral("/子目录/内部.txt"), outside = sibling + "/outside.txt";
        for (const auto &file : {direct, nested, outside}) QVERIFY(saveFile(file, "hello", {}, LineEnding::LF, &error));
        {
            MainWindow window(nullptr, testPaths()); window.show(); window.openFolder(root);
            auto *tabs = window.findChild<QTabWidget *>(); auto *storage = window.findChild<AppStorage *>();
            QCOMPARE(storage->history().size(), 1); QCOMPARE(storage->history().first().kind, RecentKind::Folder);
            window.openPath(direct);
            QTRY_VERIFY(tabs->count() == 1 && !qobject_cast<Editor *>(tabs->currentWidget())->loading);
            QCOMPARE(storage->history().size(), 1);
            window.openPath(direct); QCOMPARE(storage->history().size(), 1);
            // File search uses the same rule, including nested and Unicode paths.
            auto *input = window.findChild<QLineEdit *>("fileSearchInput"); input->setText(QStringLiteral("内部"));
            QTRY_COMPARE(window.findChild<QListWidget *>("fileSearchResults")->count(), 1);
            QTest::keyClick(input, Qt::Key_Return);
            QTRY_VERIFY(tabs->count() == 2 && !qobject_cast<Editor *>(tabs->currentWidget())->loading);
            QCOMPARE(qobject_cast<Editor *>(tabs->currentWidget())->path, QFileInfo(nested).canonicalFilePath());
            QCOMPARE(storage->history().size(), 1);
            QCOMPARE(window.findChild<QListWidget *>("welcomeHistory")->count(), 1);
            // A sibling whose name starts with the root name is still outside the folder.
            window.openPath(outside);
            QTRY_VERIFY(tabs->count() == 3 && !qobject_cast<Editor *>(tabs->currentWidget())->loading);
            QCOMPARE(storage->history().size(), 2); QCOMPARE(storage->history().first().path, QFileInfo(outside).canonicalFilePath());
            window.openPath(nested); QCOMPARE(storage->history().size(), 2);
            QVERIFY(window.close());
        }
        MainWindow reloaded(nullptr, testPaths()); auto *storage = reloaded.findChild<AppStorage *>();
        QCOMPARE(storage->history().size(), 2); QCOMPARE(storage->history().first().path, QFileInfo(outside).canonicalFilePath());
        // A folder in history alone does not suppress standalone file opens.
        reloaded.openPath(direct); auto *tabs = reloaded.findChild<QTabWidget *>();
        QTRY_VERIFY(tabs->count() == 1 && !qobject_cast<Editor *>(tabs->currentWidget())->loading);
        QCOMPARE(storage->history().size(), 3); QCOMPARE(storage->history().first().path, QFileInfo(direct).canonicalFilePath());
    }
    void fileHistoryUsesFolderAtStartOfLoading()
    {
        QTemporaryDir first, second; QString error;
        const auto inside = first.filePath("inside.txt"), standalone = first.filePath("standalone.txt");
        for (const auto &file : {inside, standalone}) QVERIFY(saveFile(file, "hello", {}, LineEnding::LF, &error));
        MainWindow window(nullptr, testPaths()); window.openFolder(first.path()); window.openPath(inside);
        window.openFolder(second.path()); auto *tabs = window.findChild<QTabWidget *>(); auto *storage = window.findChild<AppStorage *>();
        QTRY_VERIFY(tabs->count() == 1 && !qobject_cast<Editor *>(tabs->currentWidget())->loading);
        QCOMPARE(storage->history().size(), 2);
        for (const auto &entry : storage->history()) QCOMPARE(entry.kind, RecentKind::Folder);
        window.openPath(standalone); window.openFolder(first.path());
        QTRY_VERIFY(tabs->count() == 2 && !qobject_cast<Editor *>(tabs->currentWidget())->loading);
        QCOMPARE(storage->history().size(), 3); QCOMPARE(storage->history().first().path, QFileInfo(standalone).canonicalFilePath());
    }
    void clearingWhileFileLoadsDoesNotRestoreHistory()
    {
        QTemporaryDir fixture, folder; QString error; const auto file = fixture.filePath("loading.txt");
        QVERIFY(saveFile(file, QString(1000000, 'a'), {}, LineEnding::LF, &error));
        MainWindow window(nullptr, testPaths()); window.openFolder(folder.path()); window.openPath(file);
        window.findChild<QAction *>("actionClearHistory")->trigger();
        auto *tabs = window.findChild<QTabWidget *>();
        QTRY_VERIFY(tabs->count() == 1 && !qobject_cast<Editor *>(tabs->currentWidget())->loading);
        QVERIFY(window.findChild<AppStorage *>()->history().isEmpty());
    }
    void initTestCase()
    {
        QCoreApplication::setOrganizationName("NyaEditTests");
        QCoreApplication::setApplicationName("NyaEditTests");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings.path());
    }
    void preserveNbspAndUnicode()
    {
        Editor editor;
        const QString text = QString::fromUtf8("hello\xc2\xa0world\n你好 🌙\n");
        editor.setPlainText(text); QCOMPARE(editor.contents(), text);
    }
    void indentationUndo()
    {
        Editor editor; editor.setPlainText("one\ntwo\n"); editor.selectAll();
        QTest::keyClick(&editor, Qt::Key_Tab);
        QCOMPARE(editor.contents(), QString("    one\n    two\n"));
        editor.undo(); QCOMPARE(editor.contents(), QString("one\ntwo\n"));
        editor.selectAll(); QTest::keyClick(&editor, Qt::Key_Tab);
        QTest::keyClick(&editor, Qt::Key_Backtab);
        QCOMPARE(editor.contents(), QString("one\ntwo\n"));
    }
    void autoIndentAndReadOnly()
    {
        Editor editor; editor.setPlainText("    line"); editor.moveCursor(QTextCursor::End);
        QTest::keyClick(&editor, Qt::Key_Return);
        QCOMPARE(editor.contents(), QString("    line\n    "));
        editor.setReadOnly(true);
        QTest::keyClick(&editor, Qt::Key_Tab); QTest::keyClick(&editor, Qt::Key_Return);
        QCOMPARE(editor.contents(), QString("    line\n    "));
    }
    void asyncOpenAndDeduplicate()
    {
        QTemporaryDir dir; const auto path = dir.filePath("sample.txt");
        QString error; QVERIFY(saveFile(path, QStringLiteral("中文\ntext"), {}, LineEnding::CRLF, &error));
        MainWindow window(nullptr, testPaths()); window.newDocument(); window.openPath(path);
        auto *tabs = window.findChild<QTabWidget *>(); QVERIFY(tabs);
        QTRY_COMPARE(tabs->count(), 1);
        auto *editor = qobject_cast<Editor *>(tabs->currentWidget()); QVERIFY(editor);
        QTRY_VERIFY(!editor->loading);
        QCOMPARE(editor->contents(), QStringLiteral("中文\ntext")); QCOMPARE(editor->ending, LineEnding::CRLF);
        window.openPath(path); QCOMPARE(tabs->count(), 1);
        QVERIFY(!editor->dirty());
    }
    void searchReplaceUndoAndSave()
    {
        QTemporaryDir dir; const auto path = dir.filePath("sample.txt"); QString error;
        QVERIFY(saveFile(path, "cat cat\nCAT", {}, LineEnding::LF, &error));
        MainWindow window(nullptr, testPaths()); window.newDocument(); window.show(); window.openPath(path);
        auto *tabs = window.findChild<QTabWidget *>();
        QTRY_COMPARE(tabs->count(), 1);
        auto *editor = qobject_cast<Editor *>(tabs->currentWidget());
        QTRY_VERIFY(!editor->loading);
        // Open the search panel via the same shortcut users use.
        QTest::keyClick(&window, Qt::Key_F, Qt::ControlModifier);
        auto *find = window.findChild<QLineEdit *>("findInput");
        auto *replacement = window.findChild<QLineEdit *>("replaceInput");
        QVERIFY(find); QVERIFY(replacement);
        find->setText("cat"); replacement->setText("dog");
        window.findChild<QToolButton *>("replaceAll")->click();
        QCOMPARE(editor->contents(), QString("dog dog\ndog")); QVERIFY(editor->dirty());
        editor->undo(); QCOMPARE(editor->contents(), QString("cat cat\nCAT"));
        editor->redo(); QCOMPARE(editor->contents(), QString("dog dog\ndog"));
        window.findChild<QAction *>("actionSave")->trigger();
        QVERIFY(!editor->dirty()); QCOMPARE(loadFile(path).text, QString("dog dog\ndog"));
    }
    void cancelCloseProtectsEdits()
    {
        MainWindow window(nullptr, testPaths()); window.newDocument();
        auto *tabs = window.findChild<QTabWidget *>();
        auto *editor = qobject_cast<Editor *>(tabs->currentWidget());
        editor->insertPlainText("unsaved"); QVERIFY(editor->dirty());
        QTimer dismiss;
        dismiss.setInterval(10);
        connect(&dismiss, &QTimer::timeout, &window, [&dismiss] {
            for (QWidget *widget : QApplication::topLevelWidgets()) {
                if (auto *box = qobject_cast<QMessageBox *>(widget); box && box->isVisible()) {
                    dismiss.stop(); box->button(QMessageBox::Cancel)->click(); return;
                }
            }
        });
        dismiss.start();
        window.show(); window.findChild<QAction *>("actionWelcome")->trigger();
        QVERIFY(!window.close());
        QCOMPARE(tabs->count(), 1); QCOMPARE(editor->contents(), QString("unsaved"));
    }
    void zoomSurvivesThemeChange()
    {
        MainWindow window(nullptr, testPaths()); window.newDocument();
        auto *editor = qobject_cast<Editor *>(window.findChild<QTabWidget *>()->currentWidget());
        QCOMPARE(editor->fontSize(), 13);
        editor->setFontSize(17); QCOMPARE(editor->fontSize(), 17);
        window.findChild<QAction *>("actionTheme")->trigger();
        QCOMPARE(editor->fontSize(), 17);
        QCOMPARE(editor->font().pointSize(), 17);
        editor->setFontSize(13);
    }
    void closeLoadingTab()
    {
        QTemporaryDir dir; const auto path = dir.filePath("large.txt"); QString error;
        QVERIFY(saveFile(path, QString(1000000, 'a'), {}, LineEnding::LF, &error));
        MainWindow window(nullptr, testPaths()); window.newDocument(); window.openPath(path);
        auto *tabs = window.findChild<QTabWidget *>();
        const int loadingIndex = tabs->currentIndex();
        QVERIFY(QMetaObject::invokeMethod(tabs, "tabCloseRequested", Qt::DirectConnection, Q_ARG(int, loadingIndex)));
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QTest::qWait(200); QCOMPARE(tabs->count(), 1);
        QVERIFY(qobject_cast<Editor *>(tabs->currentWidget())->path.isEmpty());
    }
    void folderSearchOpensNestedFilesAndClears()
    {
        QTemporaryDir dir;
        QVERIFY(QDir().mkpath(dir.filePath("a/deep")));
        QVERIFY(QDir().mkpath(dir.filePath("b")));
        QString error;
        QVERIFY(saveFile(dir.filePath("a/deep/settings.ini"), "first", {}, LineEnding::LF, &error));
        QVERIFY(saveFile(dir.filePath("b/settings.ini"), "second", {}, LineEnding::LF, &error));
        MainWindow window(nullptr, testPaths()); window.newDocument(); window.show();
        auto *input = window.findChild<QLineEdit *>("fileSearchInput");
        auto *results = window.findChild<QListWidget *>("fileSearchResults");
        auto *tree = window.findChild<QTreeView *>();
        QVERIFY(input); QVERIFY(results); QVERIFY(!input->isEnabled());
        window.openFolder(dir.path()); QVERIFY(input->isEnabled());
        window.findChild<QAction *>("actionFileSearch")->trigger();
        QTRY_VERIFY(input->hasFocus());
        input->setText("SETTINGS");
        QTRY_COMPARE(results->count(), 2);
        QVERIFY(tree->isHidden());
        QCOMPARE(results->item(0)->text(), QString("a/deep/settings.ini"));
        QTest::keyClick(input, Qt::Key_Down);
        QTRY_VERIFY(results->hasFocus());
        results->setCurrentRow(1);
        QTest::keyClick(results, Qt::Key_Return);
        auto *tabs = window.findChild<QTabWidget *>();
        QTRY_VERIFY(!qobject_cast<Editor *>(tabs->currentWidget())->path.isEmpty());
        QTRY_COMPARE(tabs->count(), 1);
        auto *editor = qobject_cast<Editor *>(tabs->currentWidget());
        QTRY_VERIFY(!editor->loading);
        QCOMPARE(editor->contents(), QString("second"));
        input->setFocus(); QTest::keyClick(input, Qt::Key_Escape);
        QVERIFY(input->text().isEmpty()); QVERIFY(results->isHidden()); QVERIFY(!tree->isHidden());
    }
    void folderSearchInputChangesAndFolderSwitch()
    {
        QTemporaryDir first, second; QString error;
        QVERIFY(saveFile(first.filePath("target-old.txt"), "first", {}, LineEnding::LF, &error));
        QVERIFY(saveFile(second.filePath("target-new.txt"), "second", {}, LineEnding::LF, &error));
        MainWindow window(nullptr, testPaths()); window.newDocument(); window.openFolder(first.path());
        auto *input = window.findChild<QLineEdit *>("fileSearchInput");
        auto *results = window.findChild<QListWidget *>("fileSearchResults");
        auto *status = window.findChild<QLabel *>("fileSearchStatus");
        input->setText("target"); QTRY_COMPARE(results->count(), 1);
        QCOMPARE(results->item(0)->text(), QString("target-old.txt"));
        // Switch roots while a newly scheduled search still refers to the old root.
        input->setText("target-"); window.openFolder(second.path());
        QTRY_COMPARE(results->count(), 1);
        QCOMPARE(results->item(0)->text(), QString("target-new.txt"));
        input->setText("missing"); input->setText("new");
        QTRY_COMPARE(results->count(), 1);
        QCOMPARE(results->item(0)->text(), QString("target-new.txt"));
        QTest::keyClick(input, Qt::Key_Return);
        auto *tabs = window.findChild<QTabWidget *>(); QTRY_COMPARE(tabs->count(), 1);
        auto *editor = qobject_cast<Editor *>(tabs->currentWidget()); QTRY_VERIFY(!editor->loading);
        QCOMPARE(editor->contents(), QString("second"));
        input->setText("missing");
        QTRY_COMPARE(status->text(), QStringLiteral("没有匹配的文件"));
        QCOMPARE(results->count(), 0);
    }
};
QTEST_MAIN(WidgetTests)
#include "WidgetTests.moc"
