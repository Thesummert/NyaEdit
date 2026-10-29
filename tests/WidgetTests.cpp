#include "Editor.h"
#include "FontSettingsDialog.h"
#include "AnimationSettingsDialog.h"
#include "Theme.h"
#include "FileTreeView.h"
#include "MainWindow.h"
#include "WelcomePage.h"
#include <QAction>
#include <QAbstractButton>
#include <QApplication>
#include <QContextMenuEvent>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileSystemModel>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QLineEdit>
#include <QListWidget>
#include <QLabel>
#include <QMessageBox>
#include <QMenu>
#include <QPushButton>
#include <QSettings>
#include <QScrollBar>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardItemModel>
#include <QTabWidget>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
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

class WidgetTests : public QObject {
    Q_OBJECT
    QTemporaryDir m_settings;
    StoragePaths testPaths() const { return StoragePaths::underDirectory(m_settings.filePath(QString::fromLatin1(QTest::currentTestFunction()))); }
    struct ContextAction {
        bool shown = false;
        bool enabled = false;
        QString path;
        QString tooltip;
    };
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
