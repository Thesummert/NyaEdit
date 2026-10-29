#include "MainWindow.h"
#include "Editor.h"
#include "FileIO.h"
#include "FileSearch.h"
#include "FileTreeView.h"
#include "FontSettingsDialog.h"
#include "AnimationSettingsDialog.h"
#include "Theme.h"
#include "WelcomePage.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QFontDatabase>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPointer>
#include <QProcess>
#include <QScrollBar>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTextBlock>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <cmath>

namespace nya {
namespace {
QToolButton *button(const QString &text, const QString &tip, QWidget *parent, bool checkable = false)
{
    auto *b = new QToolButton(parent);
    b->setText(text); b->setToolTip(tip); b->setCheckable(checkable);
    b->setCursor(Qt::PointingHandCursor);
    return b;
}
QString canonicalPath(const QString &path)
{
    const QFileInfo info(path);
    return info.canonicalFilePath().isEmpty() ? info.absoluteFilePath() : info.canonicalFilePath();
}
}

MainWindow::MainWindow(QWidget *parent, StoragePaths storagePaths) : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("NyaEdit"));
    setWindowIcon(Theme::appIcon());
    resize(1180, 780); setMinimumSize(760, 480);
    setAcceptDrops(true);
    const bool importLegacy = storagePaths.configDirectory.isEmpty() && storagePaths.dataDirectory.isEmpty();
    m_storage = new AppStorage(std::move(storagePaths), this);
    const bool storageReady = m_storage->initialize(importLegacy);
    m_dark = m_storage->value("appearance/dark", true).toBool();
    m_wrap = m_storage->value("editor/wrap", false).toBool();
    m_animations.interfaceEnabled = m_storage->value("animation/interface", true).toBool();
    m_animations.cursorEnabled = m_storage->value("animation/cursor", true).toBool();
    m_animations.selectionEnabled = m_storage->value("animation/selection", true).toBool();
    const double speed = m_storage->value("animation/speed", 1.0).toDouble();
    m_animations.speed = std::isfinite(speed) && speed > 0 ? qBound(0.25, speed, 3.0) : 1.0;
    Theme::apply(*qApp, m_dark);
    m_searchTimer = new QTimer(this); m_searchTimer->setSingleShot(true); m_searchTimer->setInterval(180);
    connect(m_searchTimer, &QTimer::timeout, this, &MainWindow::refreshSearch);
    setupWorkspace(); setupMenus(); setupToolbar(); setupStatus();
    applyAnimationSettings();
    restoreGeometry(m_storage->value("window/geometry").toByteArray());
    const auto splitter = m_storage->value("window/splitter").toByteArray();
    if (!splitter.isEmpty()) m_splitter->restoreState(splitter);
    const int sidebarWidth = m_storage->value("window/sidebarWidth", 0).toInt();
    if (sidebarWidth >= m_sidebar->minimumWidth()) {
        const auto sizes = m_splitter->sizes();
        m_splitter->setSizes({sidebarWidth, qMax(0, sizes[0] + sizes[1] - sidebarWidth)});
    }
    m_sidebarAction->setChecked(m_storage->value("appearance/sidebar", true).toBool());
    if (sidebarWidth >= m_sidebar->minimumWidth()) m_sidebarTransition->restoreExpandedExtent(sidebarWidth);
    connect(m_storage, &AppStorage::historyChanged, this, [this] {
        rebuildRecentMenu(); m_welcome->setHistory(m_storage->history());
    });
    connect(m_storage, &AppStorage::errorOccurred, this, [this](const QString &message) {
        statusBar()->showMessage(message, 12000);
    });
    updateWorkspacePage(); updateStatus();
    if (!storageReady) QTimer::singleShot(0, this, [this] { statusBar()->showMessage(m_storage->lastError(), 15000); });
    connect(qApp, &QApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if (state == Qt::ApplicationActive) QTimer::singleShot(150, this, &MainWindow::checkExternalChanges);
    });
}

MainWindow::~MainWindow()
{
    if (m_fileSearchCancel) m_fileSearchCancel->store(true, std::memory_order_relaxed);
}

void MainWindow::setupWorkspace()
{
    m_splitter = new QSplitter(this);
    m_splitter->setChildrenCollapsible(false);
    m_sidebar = new QFrame(m_splitter); m_sidebar->setObjectName("sidebar"); m_sidebar->setMinimumWidth(170);
    auto *side = new QVBoxLayout(m_sidebar); side->setContentsMargins(0, 0, 0, 12); side->setSpacing(0);
    auto *heading = new QLabel(QStringLiteral("已打开的文档")); heading->setObjectName("sectionLabel"); side->addWidget(heading);
    m_documents = new QListWidget; m_documents->setObjectName("openDocuments");
    m_documents->setMaximumHeight(220); m_documents->setMinimumHeight(100);
    side->addWidget(m_documents);
    connect(m_documents, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0) { m_tabs->setCurrentIndex(row); updateWorkspacePage(); if (current()) current()->setFocus(); }
    });
    connect(m_documents, &QListWidget::itemClicked, this, [this] {
        updateWorkspacePage(); if (current()) current()->setFocus();
    });
    m_documents->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_documents, &QWidget::customContextMenuRequested, this, [this](const QPoint &position) {
        const auto *item = m_documents->itemAt(position);
        if (!item) return;
        if (const auto *editor = qobject_cast<Editor *>(m_tabs->widget(m_documents->row(item))))
            showFileContextMenu(editor->path, m_documents->viewport()->mapToGlobal(position));
    });
    auto *folderRow = new QHBoxLayout; folderRow->setContentsMargins(0, 10, 8, 0);
    m_folderLabel = new QLabel(QStringLiteral("文件夹")); m_folderLabel->setObjectName("sectionLabel");
    m_folderLabel->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_folderLabel, &QWidget::customContextMenuRequested, this, [this](const QPoint &position) {
        if (!m_folderRoot.isEmpty()) showFileContextMenu(m_folderRoot, m_folderLabel->mapToGlobal(position));
    });
    folderRow->addWidget(m_folderLabel, 1);
    auto *folderButton = button("+", QStringLiteral("打开文件夹"), m_sidebar);
    connect(folderButton, &QToolButton::clicked, this, [this] { openFolder(); }); folderRow->addWidget(folderButton);
    side->addLayout(folderRow);
    auto *fileSearchRow = new QHBoxLayout;
    fileSearchRow->setContentsMargins(12, 6, 12, 8);
    m_fileSearchInput = new QLineEdit;
    m_fileSearchInput->setObjectName("fileSearchInput");
    m_fileSearchInput->setPlaceholderText(QStringLiteral("搜索文件名或路径…"));
    m_fileSearchInput->setAccessibleName(QStringLiteral("搜索文件夹中的文件"));
    m_fileSearchInput->setClearButtonEnabled(true); m_fileSearchInput->setEnabled(false);
    m_fileSearchInput->installEventFilter(this);
    fileSearchRow->addWidget(m_fileSearchInput); side->addLayout(fileSearchRow);
    auto *fileSearchHint = new QLabel;
    fileSearchHint->setObjectName("fileSearchHint"); fileSearchHint->setWordWrap(true);
    side->addWidget(fileSearchHint);
    m_fileSearchFuzzyAction = new QAction(QStringLiteral("模糊搜索"), m_fileSearchInput);
    m_fileSearchFuzzyAction->setObjectName("actionFileSearchFuzzy");
    m_fileSearchFuzzyAction->setCheckable(true);
    m_fileSearchFuzzyAction->setChecked(m_storage->value("search/fuzzy", true).toBool());
    const auto updateSearchHint = [this, fileSearchHint] {
        const bool fuzzy = m_fileSearchFuzzyAction->isChecked();
        fileSearchHint->setText(fuzzy ? QStringLiteral("模糊搜索 · 右键关闭") : QStringLiteral("连续匹配 · 右键切换"));
        m_fileSearchInput->setToolTip((fuzzy
            ? QStringLiteral("模糊搜索已开启：字符按顺序匹配，可不连续。例如 mw 可找到 MainWindow.cpp。")
            : QStringLiteral("模糊搜索已关闭：连续匹配文件名和相对路径片段。"))
            + QStringLiteral("\n右键搜索框可切换模糊搜索，不区分大小写。Ctrl/⌘+P 聚焦，回车打开，Esc 清空。"));
    };
    updateSearchHint();
    connect(m_fileSearchFuzzyAction, &QAction::toggled, this, [this, updateSearchHint](bool enabled) {
        m_storage->setValue("search/fuzzy", enabled); updateSearchHint(); scheduleFileSearch();
    });
    m_fileSearchInput->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_fileSearchInput, &QWidget::customContextMenuRequested, this, [this](const QPoint &position) {
        std::unique_ptr<QMenu> menu(m_fileSearchInput->createStandardContextMenu());
        menu->addSeparator(); menu->addAction(m_fileSearchFuzzyAction);
        menu->exec(m_fileSearchInput->mapToGlobal(position));
    });
    m_fileSearchStatus = new QLabel;
    m_fileSearchStatus->setObjectName("fileSearchStatus"); m_fileSearchStatus->setWordWrap(true);
    side->addWidget(m_fileSearchStatus); m_fileSearchStatus->hide();
    m_fileSearchResults = new QListWidget;
    m_fileSearchResults->setObjectName("fileSearchResults");
    m_fileSearchResults->setTextElideMode(Qt::ElideMiddle);
    m_fileSearchResults->installEventFilter(this);
    m_fileSearchResults->hide();
    m_fileSearchTimer = new QTimer(this);
    m_fileSearchTimer->setSingleShot(true); m_fileSearchTimer->setInterval(180);
    connect(m_fileSearchTimer, &QTimer::timeout, this, &MainWindow::runFileSearch);
    connect(m_fileSearchInput, &QLineEdit::textChanged, this, &MainWindow::scheduleFileSearch);
    connect(m_fileSearchInput, &QLineEdit::returnPressed, this, &MainWindow::openFileSearchResult);
    connect(m_fileSearchResults, &QListWidget::itemActivated, this, [this] { openFileSearchResult(); });
    m_fileSearchResults->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_fileSearchResults, &QWidget::customContextMenuRequested, this, [this](const QPoint &position) {
        if (const auto *item = m_fileSearchResults->itemAt(position))
            showFileContextMenu(item->data(Qt::UserRole).toString(), m_fileSearchResults->viewport()->mapToGlobal(position));
    });
    m_files = new QFileSystemModel(this);
    m_files->setReadOnly(true); m_files->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot);
    m_tree = new FileTreeView; m_tree->setObjectName("fileTree"); m_tree->setModel(m_files); m_tree->setHeaderHidden(true); m_tree->setIndentation(14);
    m_tree->setUniformRowHeights(true); m_tree->hide();
    for (int i = 1; i < m_files->columnCount(); ++i) m_tree->hideColumn(i);
    side->addWidget(m_tree, 1);
    side->addWidget(m_fileSearchResults, 1);
    connect(m_tree, &QTreeView::doubleClicked, this, [this](const QModelIndex &index) {
        if (!m_files->isDir(index)) openPath(m_files->filePath(index));
    });
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tree, &QWidget::customContextMenuRequested, this, [this](const QPoint &position) {
        const auto index = m_tree->indexAt(position);
        if (index.isValid()) showFileContextMenu(m_files->filePath(index), m_tree->viewport()->mapToGlobal(position));
    });
    m_folderHint = new QLabel(QStringLiteral("打开文件夹，\n让文件触手可及。")); m_folderHint->setObjectName("hint");
    side->addWidget(m_folderHint); side->addStretch();
    auto *footer = new QLabel(QStringLiteral("N Y A E D I T   /   0.1")); footer->setObjectName("hint"); side->addWidget(footer);

    auto *workspace = new QWidget(m_splitter);
    auto *layout = new QVBoxLayout(workspace); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(0);
    setupSearch(); layout->addWidget(m_searchPanel);
    m_tabs = new QTabWidget; m_tabs->setDocumentMode(true); m_tabs->setTabsClosable(true); m_tabs->setMovable(true);
    m_tabs->tabBar()->setDrawBase(false);
    m_tabs->tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tabs->tabBar(), &QWidget::customContextMenuRequested, this, [this](const QPoint &position) {
        const int index = m_tabs->tabBar()->tabAt(position);
        if (index < 0) return;
        if (const auto *editor = qobject_cast<Editor *>(m_tabs->widget(index)))
            showFileContextMenu(editor->path, m_tabs->tabBar()->mapToGlobal(position));
    });
    m_tabs->setElideMode(Qt::ElideMiddle); m_tabs->setUsesScrollButtons(true);
    auto *newTab = button("+", QStringLiteral("新建文档"), m_tabs); m_tabs->setCornerWidget(newTab, Qt::TopRightCorner);
    connect(newTab, &QToolButton::clicked, this, &MainWindow::newDocument);
    m_pages = new QStackedWidget;
    m_welcome = new WelcomePage;
    m_welcome->setHistory(m_storage->history());
    connect(m_welcome, &WelcomePage::newDocumentRequested, this, &MainWindow::newDocument);
    connect(m_welcome, &WelcomePage::openFilesRequested, this, [this] { chooseFiles(); });
    connect(m_welcome, &WelcomePage::openFolderRequested, this, [this] { openFolder(); });
    connect(m_welcome, &WelcomePage::recentRequested, this, &MainWindow::openRecent);
    m_pages->addWidget(m_welcome); m_pages->addWidget(m_tabs);
    m_pageTransition = new PageTransition(m_pages);
    connect(m_pages, &QStackedWidget::currentChanged, this, [this] {
        m_pageTransition->start(m_animations.interfaceEnabled ? m_animations.duration(180) : 0);
    });
    layout->addWidget(m_pages, 1);
    connect(m_tabs, &QTabWidget::tabCloseRequested, this, [this](int index) { closeTab(index); });
    connect(m_tabs, &QTabWidget::currentChanged, this, [this] {
        updateWorkspacePage(); refreshDocumentList(); updateStatus(); refreshSearch();
        m_pageTransition->start(m_animations.interfaceEnabled ? m_animations.duration(180) : 0);
        if (current()) current()->setFocus();
    });
    connect(m_tabs->tabBar(), &QTabBar::tabMoved, this, [this] { refreshDocumentList(); });
    m_splitter->addWidget(m_sidebar); m_splitter->addWidget(workspace);
    m_splitter->setStretchFactor(0, 0); m_splitter->setStretchFactor(1, 1); m_splitter->setSizes({220, 960});
    setCentralWidget(m_splitter);
    m_sidebarTransition = new PanelTransition(m_sidebar, Qt::Horizontal);
    m_searchTransition = new PanelTransition(m_searchPanel, Qt::Vertical);
}

void MainWindow::setupMenus()
{
    auto *file = menuBar()->addMenu(QStringLiteral("文件(&F)"));
    auto *newAction = file->addAction(Theme::icon("new"), QStringLiteral("新建"), this, &MainWindow::newDocument, QKeySequence::New);
    newAction->setObjectName("actionNew");
    file->addAction(Theme::icon("folder"), QStringLiteral("打开…"), this, [this] { chooseFiles(); }, QKeySequence::Open);
    auto *openWith = file->addMenu(QStringLiteral("按指定编码打开"));
    for (const QString &name : EncodingService::supported()) openWith->addAction(name, this, [this, name] { chooseFiles(name); });
    file->addAction(QStringLiteral("打开文件夹…"), this, [this] { openFolder(); });
    m_recent = file->addMenu(QStringLiteral("最近打开")); rebuildRecentMenu();
    file->addSeparator();
    m_saveAction = file->addAction(Theme::icon("save"), QStringLiteral("保存"), this, [this] { save(current()); }, QKeySequence::Save);
    m_saveAction->setObjectName("actionSave");
    file->addAction(QStringLiteral("另存为…"), this, [this] { save(current(), true); }, QKeySequence::SaveAs);
    file->addAction(QStringLiteral("全部保存"), this, [this] {
        for (int i = 0; i < m_tabs->count(); ++i) {
            auto *editor = qobject_cast<Editor *>(m_tabs->widget(i));
            if (editor->dirty() && !save(editor)) break;
        }
    }, QKeySequence(QStringLiteral("Ctrl+Alt+S")));
    file->addSeparator();
    file->addAction(QStringLiteral("关闭标签页"), this, [this] { if (current()) closeTab(m_tabs->currentIndex()); }, QKeySequence::Close);
    file->addAction(QStringLiteral("退出"), this, &QWidget::close, QKeySequence::Quit);

    auto *edit = menuBar()->addMenu(QStringLiteral("编辑(&E)"));
    m_undoAction = edit->addAction(Theme::icon("undo"), QStringLiteral("撤销"), this, [this] { if (current()) current()->undo(); }, QKeySequence::Undo);
    m_redoAction = edit->addAction(Theme::icon("redo"), QStringLiteral("重做"), this, [this] { if (current()) current()->redo(); }, QKeySequence::Redo);
    edit->addSeparator();
    edit->addAction(QStringLiteral("剪切"), this, [this] { if (current()) current()->cut(); }, QKeySequence::Cut);
    edit->addAction(QStringLiteral("复制"), this, [this] { if (current()) current()->copy(); }, QKeySequence::Copy);
    edit->addAction(QStringLiteral("粘贴"), this, [this] { if (current()) current()->paste(); }, QKeySequence::Paste);
    edit->addAction(QStringLiteral("全选"), this, [this] { if (current()) current()->selectAll(); }, QKeySequence::SelectAll);

    auto *search = menuBar()->addMenu(QStringLiteral("搜索(&S)"));
    auto *fileSearchAction = search->addAction(QStringLiteral("搜索文件…"), this, &MainWindow::focusFileSearch, QKeySequence(QStringLiteral("Ctrl+P")));
    fileSearchAction->setObjectName("actionFileSearch");
    search->addSeparator();
    auto *findAction = search->addAction(Theme::icon("search"), QStringLiteral("查找"), this, [this] { showSearch(false); }, QKeySequence::Find);
    findAction->setObjectName("actionFind");
    search->addAction(QStringLiteral("替换"), this, [this] { showSearch(true); }, QKeySequence::Replace);
    search->addAction(QStringLiteral("下一个匹配"), this, [this] { findNext(); }, QKeySequence::FindNext);
    search->addAction(QStringLiteral("上一个匹配"), this, [this] { findNext(true); }, QKeySequence::FindPrevious);
    search->addSeparator();
    search->addAction(QStringLiteral("跳转到行…"), this, &MainWindow::gotoLine, QKeySequence(QStringLiteral("Ctrl+L")));

    auto *view = menuBar()->addMenu(QStringLiteral("视图(&V)"));
    m_wrapAction = view->addAction(QStringLiteral("自动换行")); m_wrapAction->setCheckable(true); m_wrapAction->setChecked(m_wrap);
    m_wrapAction->setObjectName("actionWordWrap");
    connect(m_wrapAction, &QAction::toggled, this, [this](bool on) {
        m_wrap = on;
        for (int i = 0; i < m_tabs->count(); ++i) qobject_cast<Editor *>(m_tabs->widget(i))->setLineWrapMode(on ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
        m_storage->setValue("editor/wrap", on);
    });
    m_sidebarAction = view->addAction(QStringLiteral("侧边栏")); m_sidebarAction->setCheckable(true); m_sidebarAction->setChecked(true);
    m_sidebarAction->setObjectName("actionSidebar");
    m_sidebarAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+B")));
    connect(m_sidebarAction, &QAction::toggled, this, [this](bool visible) {
        m_sidebarTransition->setExpanded(visible, m_animations.interfaceEnabled ? m_animations.duration(200) : 0);
    });
    connect(m_sidebarAction, &QAction::toggled, this, [this](bool visible) { m_storage->setValue("appearance/sidebar", visible); });
    auto *welcomeAction = view->addAction(QStringLiteral("欢迎页"), this, [this] {
        hideSearch(); m_welcome->setHistory(m_storage->history()); m_pages->setCurrentWidget(m_welcome);
        updateStatus();
    });
    welcomeAction->setObjectName("actionWelcome");
    auto *themeAction = view->addAction(QStringLiteral("切换深色 / 浅色主题"), this, [this] { setDark(!m_dark); });
    themeAction->setObjectName("actionTheme");
    view->addSeparator();
    view->addAction(QStringLiteral("放大字体"), this, [this] { if (current()) current()->setFontSize(current()->fontSize() + 1); }, QKeySequence::ZoomIn);
    view->addAction(QStringLiteral("缩小字体"), this, [this] { if (current()) current()->setFontSize(current()->fontSize() - 1); }, QKeySequence::ZoomOut);
    view->addAction(QStringLiteral("重置字体大小"), this, [this] { if (current()) current()->setFontSize(13); }, QKeySequence(QStringLiteral("Ctrl+0")));
    view->addAction(QStringLiteral("下一个标签页"), this, [this] { if (m_tabs->count()) { m_tabs->setCurrentIndex((m_tabs->currentIndex() + 1) % m_tabs->count()); updateWorkspacePage(); } }, QKeySequence(QStringLiteral("Ctrl+Tab")));
    view->addAction(QStringLiteral("上一个标签页"), this, [this] { if (m_tabs->count()) { m_tabs->setCurrentIndex((m_tabs->currentIndex() + m_tabs->count() - 1) % m_tabs->count()); updateWorkspacePage(); } }, QKeySequence(QStringLiteral("Ctrl+Shift+Tab")));

    auto *settings = menuBar()->addMenu(QStringLiteral("设置(&T)"));
    auto *fontSettings = settings->addAction(QStringLiteral("字体与字号…"), this, &MainWindow::chooseEditorFont);
    fontSettings->setObjectName("actionFontSettings");
    auto *animationSettings = settings->addAction(QStringLiteral("动画设置…"), this, &MainWindow::chooseAnimationSettings);
    animationSettings->setObjectName("actionAnimationSettings");
    settings->addSeparator();
    auto *configDirectory = settings->addAction(QStringLiteral("打开配置目录"), this, [this] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_storage->paths().configDirectory));
    });
    configDirectory->setToolTip(m_storage->paths().configDirectory);
    settings->addSeparator();
    auto *clear = settings->addAction(QStringLiteral("清除历史记录"), this, &MainWindow::clearHistory);
    clear->setObjectName("actionClearHistory");
    clear->setToolTip(QStringLiteral("清空文件、文件夹历史和上次打开目录，保留编辑器设置"));

    auto *help = menuBar()->addMenu(QStringLiteral("帮助(&H)"));
    help->addAction(QStringLiteral("关于 NyaEdit"), this, [this] {
        QMessageBox::about(this, QStringLiteral("关于 NyaEdit"), QStringLiteral(
            "<h2>NyaEdit <small>0.1.0</small></h2><p>轻盈落笔，专注文本。</p>"
            "<p>C++17 · Qt Widgets · ICU<br>独立核心库 + 原生界面</p>"
            "<p>编码菜单：点击状态栏编码。<br>正则替换支持 $1、${name} 和 $$。</p>"
            "<p>编码自动检测是启发式判断，短文本请核对。<br>单文件上限 128 MiB，超过 2 MiB 自动关闭语法着色。</p>"));
    });
    auto *escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(escape, &QShortcut::activated, this, &MainWindow::hideSearch);
}

void MainWindow::setupToolbar()
{
    auto *toolbar = addToolBar(QStringLiteral("主工具栏")); toolbar->setMovable(false); toolbar->setIconSize({20, 20});
    auto *logo = new QLabel; logo->setPixmap(Theme::appIcon().pixmap(30, 30)); toolbar->addWidget(logo);
    auto *brand = new QLabel(QStringLiteral("NyaEdit")); brand->setObjectName("brand"); toolbar->addWidget(brand);
    toolbar->addSeparator();
    toolbar->addAction(Theme::icon("new"), QStringLiteral("新建"), this, &MainWindow::newDocument);
    toolbar->addAction(Theme::icon("folder"), QStringLiteral("打开"), this, [this] { chooseFiles(); });
    toolbar->addAction(m_saveAction); toolbar->addSeparator();
    toolbar->addAction(m_undoAction); toolbar->addAction(m_redoAction);
    auto *spacer = new QWidget; spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred); toolbar->addWidget(spacer);
    auto *tagline = new QLabel(QStringLiteral("轻盈落笔 · 专注文本")); tagline->setObjectName("tagline"); toolbar->addWidget(tagline);
    toolbar->addSeparator();
    toolbar->addAction(Theme::icon("search"), QStringLiteral("查找 / 替换"), this, [this] { showSearch(true); });
    toolbar->addAction(Theme::icon("sun"), QStringLiteral("切换主题"), this, [this] { setDark(!m_dark); });
    auto *sideAction = toolbar->addAction(Theme::icon("sidebar"), QStringLiteral("侧边栏"));
    connect(sideAction, &QAction::triggered, this, [this] { m_sidebarAction->toggle(); });
}

void MainWindow::setupSearch()
{
    m_searchPanel = new QFrame; m_searchPanel->setObjectName("searchPanel");
    auto *layout = new QVBoxLayout(m_searchPanel); layout->setContentsMargins(14, 10, 14, 10); layout->setSpacing(7);
    auto *row = new QHBoxLayout; row->setSpacing(5);
    m_find = new QLineEdit; m_find->setObjectName("findInput"); m_find->setPlaceholderText(QStringLiteral("查找文本…"));
    m_find->setClearButtonEnabled(true); row->addWidget(m_find, 1);
    m_case = button("Aa", QStringLiteral("区分大小写"), m_searchPanel, true);
    m_word = button("ab", QStringLiteral("全词匹配（Unicode 字母、数字和下划线）"), m_searchPanel, true);
    m_regex = button(".*", QStringLiteral("正则表达式"), m_searchPanel, true);
    row->addWidget(m_case); row->addWidget(m_word); row->addWidget(m_regex);
    auto *prev = button("↑", QStringLiteral("上一个匹配"), m_searchPanel);
    auto *next = button("↓", QStringLiteral("下一个匹配"), m_searchPanel);
    row->addWidget(prev); row->addWidget(next);
    m_matchLabel = new QLabel; m_matchLabel->setObjectName("matchCount"); m_matchLabel->setMinimumWidth(92); row->addWidget(m_matchLabel);
    auto *close = button("×", QStringLiteral("关闭（Esc）"), m_searchPanel); row->addWidget(close);
    layout->addLayout(row);
    m_replaceRow = new QWidget;
    auto *replaceLayout = new QHBoxLayout(m_replaceRow); replaceLayout->setContentsMargins(0, 0, 0, 0); replaceLayout->setSpacing(5);
    m_replace = new QLineEdit; m_replace->setObjectName("replaceInput");
    m_replace->setPlaceholderText(QStringLiteral("替换为…（正则捕获组：$1、${name}）"));
    replaceLayout->addWidget(m_replace, 1);
    auto *replace = button(QStringLiteral("替换"), QStringLiteral("替换当前匹配"), m_searchPanel);
    auto *all = button(QStringLiteral("全部替换"), QStringLiteral("替换当前文档中的全部匹配，可一次撤销"), m_searchPanel);
    all->setObjectName("replaceAll"); replaceLayout->addWidget(replace); replaceLayout->addWidget(all);
    layout->addWidget(m_replaceRow);
    connect(m_find, &QLineEdit::textChanged, this, [this] { m_searchTimer->start(); });
    connect(m_find, &QLineEdit::returnPressed, this, [this] { findNext(QApplication::keyboardModifiers() & Qt::ShiftModifier); });
    for (auto *toggle : {m_case, m_word, m_regex}) connect(toggle, &QToolButton::toggled, this, [this] { refreshSearch(); });
    connect(prev, &QToolButton::clicked, this, [this] { findNext(true); });
    connect(next, &QToolButton::clicked, this, [this] { findNext(); });
    connect(close, &QToolButton::clicked, this, &MainWindow::hideSearch);
    connect(replace, &QToolButton::clicked, this, &MainWindow::replaceCurrent);
    connect(all, &QToolButton::clicked, this, &MainWindow::replaceAll);
    connect(m_replace, &QLineEdit::returnPressed, this, &MainWindow::replaceCurrent);
    m_searchPanel->hide();
}

void MainWindow::setupStatus()
{
    m_fileLabel = new QLabel; m_fileLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    statusBar()->addWidget(m_fileLabel, 1);
    connect(statusBar(), &QStatusBar::messageChanged, this, [this](const QString &message) {
        m_fileLabel->setVisible(message.isEmpty());
    });
    m_position = button({}, QStringLiteral("跳转到行"), this); connect(m_position, &QToolButton::clicked, this, &MainWindow::gotoLine);
    m_encoding = button({}, QStringLiteral("重新解码 / 转换保存编码"), this); connect(m_encoding, &QToolButton::clicked, this, &MainWindow::showEncodingMenu);
    m_ending = button({}, QStringLiteral("更改换行符"), this); connect(m_ending, &QToolButton::clicked, this, &MainWindow::showEndingMenu);
    m_zoom = button({}, QStringLiteral("点击恢复默认字号（13 pt）；设置菜单可更换字体"), this);
    connect(m_zoom, &QToolButton::clicked, this, [this] { if (current()) current()->setFontSize(13); });
    statusBar()->addPermanentWidget(m_position); statusBar()->addPermanentWidget(m_encoding);
    statusBar()->addPermanentWidget(m_ending); statusBar()->addPermanentWidget(m_zoom);
}

Editor *MainWindow::current() const
{
    return m_pages && m_pages->currentWidget() == m_tabs ? qobject_cast<Editor *>(m_tabs->currentWidget()) : nullptr;
}

Editor *MainWindow::addEditor(const QString &name)
{
    auto *editor = new Editor;
    editor->displayName = name; editor->setDark(m_dark);
    editor->setAnimationSettings(m_animations);
    editor->setLineWrapMode(m_wrap ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
    editor->setEditorFont(configuredEditorFont());
    connect(editor->document(), &QTextDocument::modificationChanged, this, [this, editor] { updateTab(editor); updateStatus(); });
    connect(editor, &QPlainTextEdit::cursorPositionChanged, this, &MainWindow::updateStatus);
    connect(editor, &QPlainTextEdit::selectionChanged, this, &MainWindow::updateStatus);
    connect(editor, &QPlainTextEdit::undoAvailable, this, [this] { updateStatus(); });
    connect(editor, &QPlainTextEdit::redoAvailable, this, [this] { updateStatus(); });
    connect(editor, &Editor::editorFontChanged, this, [this, editor] {
        storeEditorFont(editor->editorFont()); updateStatus();
    });
    connect(editor, &QPlainTextEdit::textChanged, this, [this, editor] {
        if (editor == current() && m_searchTransition->isExpanded()) m_searchTimer->start();
        updateStatus();
    });
    m_tabs->addTab(editor, Theme::icon("file"), name);
    auto *closeButton = button(QStringLiteral("×"), QStringLiteral("关闭文档"), m_tabs->tabBar());
    closeButton->setFixedSize(24, 24);
    closeButton->setStyleSheet(QStringLiteral("QToolButton { padding: 0px; border: none; }"));
    m_tabs->tabBar()->setTabButton(m_tabs->indexOf(editor), QTabBar::RightSide, closeButton);
    connect(closeButton, &QToolButton::clicked, this, [this, editor] { closeTab(m_tabs->indexOf(editor)); });
    m_tabs->setCurrentWidget(editor); updateWorkspacePage(); editor->setFocus(); refreshDocumentList();
    return editor;
}

void MainWindow::newDocument()
{
    addEditor(QStringLiteral("未命名 %1").arg(++m_untitled));
}

void MainWindow::chooseFiles(const QString &encoding)
{
    const auto paths = QFileDialog::getOpenFileNames(this, QStringLiteral("打开文本文件"), m_storage->lastDirectory(), QStringLiteral("所有文件 (*)"));
    for (const QString &path : paths) openPath(path, encoding);
}

void MainWindow::openPath(const QString &path, const QString &forcedEncoding)
{
    if (QFileInfo(path).isDir()) { openFolder(path); return; }
    const QString actual = canonicalPath(path);
    // Keep the opening context even if the user switches folders during loading.
    const QString folderRoot = m_folderRoot;
    for (int i = 0; i < m_tabs->count(); ++i) {
        auto *editor = qobject_cast<Editor *>(m_tabs->widget(i));
        if (editor->path == actual) {
            m_tabs->setCurrentWidget(editor);
            updateWorkspacePage();
            if (!editor->loading) addRecent(actual, folderRoot);
            if (!forcedEncoding.isEmpty()) reopenEncoding(forcedEncoding);
            return;
        }
    }
    const QFileInfo info(actual);
    if (info.size() > MaxFileBytes) { QMessageBox::warning(this, QStringLiteral("无法打开"), QStringLiteral("单个文件上限为 128 MiB。")); return; }
    if (info.size() > 16LL * 1024 * 1024 && QMessageBox::question(this, QStringLiteral("打开较大文件"),
        QStringLiteral("此文件为 %1 MiB。载入编辑区可能需要一些时间并占用额外内存，是否继续？").arg(info.size() / (1024.0 * 1024), 0, 'f', 1)) != QMessageBox::Yes) return;
    // Keep a pristine initial tab until loading succeeds, so failed opens do not discard it.
    QPointer<Editor> pristine;
    if (current() && current()->path.isEmpty() && !current()->dirty() && current()->document()->isEmpty()) pristine = current();
    auto *editor = addEditor(info.fileName());
    editor->path = actual; editor->loading = true; editor->setReadOnly(true);
    editor->setPlaceholderText(QStringLiteral("正在读取文件并检测编码…")); updateTab(editor); updateStatus();
    auto *watcher = new QFutureWatcher<LoadedFile>(this);
    QPointer<Editor> guard(editor);
    const auto historyEpoch = m_historyEpoch;
    connect(watcher, &QFutureWatcher<LoadedFile>::finished, this, [this, watcher, guard, pristine, historyEpoch, folderRoot] {
        const auto result = watcher->result(); watcher->deleteLater();
        if (!guard || m_tabs->indexOf(guard) < 0) return;
        auto *editor = guard.data();
        editor->loading = false;
        if (!result.ok()) {
            const int index = m_tabs->indexOf(editor); m_tabs->removeTab(index); editor->deleteLater();
            updateWorkspacePage();
            QMessageBox::warning(this, QStringLiteral("打开失败"), result.error); return;
        }
        editor->largeFile = result.byteSize > 2 * 1024 * 1024;
        editor->encoding = result.encoding; editor->ending = result.ending;
        editor->mixedEndings = result.mixedEndings; editor->diskModified = result.modified; editor->diskSize = result.byteSize;
        editor->setPlainText(result.text); editor->document()->setModified(false);
        editor->setReadOnly(false); editor->setPlaceholderText(QStringLiteral("在这里开始书写…"));
        editor->setLanguage(QFileInfo(result.path).suffix());
        updateTab(editor);
        if (historyEpoch == m_historyEpoch) addRecent(result.path, folderRoot);
        if (pristine && pristine != editor && pristine->path.isEmpty() && !pristine->dirty() && pristine->document()->isEmpty() && !pristine->loading) {
            const int index = m_tabs->indexOf(pristine); if (index >= 0) { m_tabs->removeTab(index); pristine->deleteLater(); }
        }
        refreshDocumentList(); updateStatus();
        if (current() == editor) refreshSearch();
        if (result.encoding.confidence < 50) statusBar()->showMessage(QStringLiteral("编码判断置信度较低，请核对文字；可点击右下角编码重新打开。"), 12000);
        else if (result.mixedEndings) statusBar()->showMessage(QStringLiteral("检测到混合换行符，保存时会提示统一换行。"), 8000);
        else statusBar()->showMessage(QStringLiteral("已打开 %1%2").arg(editor->displayName, editor->largeFile ? QStringLiteral(" · 大文件模式（关闭语法着色）") : QString()), 5000);
    });
    watcher->setFuture(QtConcurrent::run([actual, forcedEncoding] { return loadFile(actual, forcedEncoding); }));
}

void MainWindow::openFolder(const QString &path)
{
    const QString selected = path.isEmpty() ? QFileDialog::getExistingDirectory(this, QStringLiteral("打开文件夹"), m_storage->lastDirectory()) : path;
    if (selected.isEmpty()) return;
    if (!QFileInfo(selected).isDir() || !QFileInfo(selected).isReadable()) {
        QMessageBox::warning(this, QStringLiteral("无法打开文件夹"), QStringLiteral("文件夹不存在或无法读取：%1").arg(selected)); return;
    }
    m_folderRoot = canonicalPath(selected);
    const auto index = m_files->setRootPath(m_folderRoot); m_tree->setRootIndex(index); m_folderHint->hide();
    m_folderLabel->setText(QFileInfo(m_folderRoot).fileName()); m_folderLabel->setToolTip(m_folderRoot);
    m_fileSearchInput->setEnabled(true); scheduleFileSearch();
    m_sidebarAction->setChecked(true); m_storage->record(m_folderRoot, RecentKind::Folder);
}

void MainWindow::focusFileSearch()
{
    if (m_folderRoot.isEmpty()) openFolder();
    if (m_folderRoot.isEmpty()) return;
    m_sidebarAction->setChecked(true);
    m_fileSearchInput->setFocus(); m_fileSearchInput->selectAll();
}

void MainWindow::scheduleFileSearch()
{
    ++m_fileSearchGeneration;
    if (m_fileSearchCancel) m_fileSearchCancel->store(true, std::memory_order_relaxed);
    m_fileSearchTimer->stop(); m_fileSearchResults->clear();
    const bool searching = !m_folderRoot.isEmpty() && !m_fileSearchInput->text().trimmed().isEmpty();
    m_tree->setVisible(!m_folderRoot.isEmpty() && !searching);
    m_fileSearchResults->setVisible(searching); m_fileSearchStatus->setVisible(searching);
    if (!searching) return;
    m_fileSearchStatus->setText(QStringLiteral("正在搜索…"));
    m_fileSearchTimer->start();
}

void MainWindow::runFileSearch()
{
    const QString query = m_fileSearchInput->text().trimmed();
    if (m_folderRoot.isEmpty() || query.isEmpty()) return;
    const auto generation = m_fileSearchGeneration;
    const auto cancel = std::make_shared<std::atomic_bool>(false);
    m_fileSearchCancel = cancel;
    const QString root = m_folderRoot;
    auto *watcher = new QFutureWatcher<FileSearchResult>(this);
    connect(watcher, &QFutureWatcher<FileSearchResult>::finished, this, [this, watcher, generation] {
        const auto result = watcher->result(); watcher->deleteLater();
        // Discard old results even if cancellation arrived just after the worker finished.
        if (generation != m_fileSearchGeneration || result.canceled) return;
        m_fileSearchResults->clear();
        for (const auto &match : result.matches) {
            auto *item = new QListWidgetItem(Theme::icon("file"), match.relativePath, m_fileSearchResults);
            item->setData(Qt::UserRole, match.path); item->setToolTip(match.path);
        }
        if (!result.error.isEmpty()) m_fileSearchStatus->setText(result.error);
        else if (result.truncated) m_fileSearchStatus->setText(QStringLiteral("显示前 %1 个匹配，请缩小搜索范围。").arg(result.matches.size()));
        else m_fileSearchStatus->setText(result.matches.isEmpty() ? QStringLiteral("没有匹配的文件") : QStringLiteral("找到 %1 个文件").arg(result.matches.size()));
        if (result.skippedDirectories)
            m_fileSearchStatus->setText(m_fileSearchStatus->text() + QStringLiteral("\n跳过 %1 个无法读取的目录。").arg(result.skippedDirectories));
        if (m_fileSearchResults->count()) m_fileSearchResults->setCurrentRow(0);
    });
    const auto mode = m_fileSearchFuzzyAction->isChecked() ? FileSearchMode::Fuzzy : FileSearchMode::Substring;
    watcher->setFuture(QtConcurrent::run([root, query, cancel, mode] { return FileSearch::findFiles(root, query, cancel, 1000, mode); }));
}

void MainWindow::openFileSearchResult()
{
    if (const auto *item = m_fileSearchResults->currentItem()) openPath(item->data(Qt::UserRole).toString());
}

void MainWindow::showFileContextMenu(const QString &path, const QPoint &globalPosition)
{
    QMenu menu(this); menu.setObjectName("fileContextMenu");
#ifdef Q_OS_MAC
    const auto text = QStringLiteral("在 Finder 中显示");
#elif defined(Q_OS_WIN)
    const auto text = QStringLiteral("在文件资源管理器中显示");
#else
    const auto text = QStringLiteral("在文件管理器中打开");
#endif
    auto *reveal = menu.addAction(Theme::icon("folder"), text);
    reveal->setObjectName("actionRevealInFileManager"); reveal->setData(path);
    reveal->setEnabled(!path.isEmpty() && QFileInfo::exists(path));
    reveal->setToolTip(path.isEmpty() ? QStringLiteral("请先保存文档")
        : !QFileInfo::exists(path) ? QStringLiteral("文件或文件夹已不存在") : path);
    // Copy the clicked path; opening the menu must not depend on the current tab.
    connect(reveal, &QAction::triggered, this, [this, path] { revealInFileManager(path); });
    menu.exec(globalPosition);
}

void MainWindow::revealInFileManager(const QString &path)
{
    const QFileInfo info(path);
    if (path.isEmpty() || !info.exists()) {
        statusBar()->showMessage(QStringLiteral("文件或文件夹已不存在，无法在系统文件管理器中打开。"), 6000); return;
    }
#ifdef Q_OS_MAC
    const bool opened = QProcess::startDetached(QStringLiteral("/usr/bin/open"), {QStringLiteral("-R"), info.absoluteFilePath()});
#elif defined(Q_OS_WIN)
    const bool opened = QProcess::startDetached(QStringLiteral("explorer.exe"),
        {QStringLiteral("/select,"), QDir::toNativeSeparators(info.absoluteFilePath())});
#else
    const bool opened = QDesktopServices::openUrl(QUrl::fromLocalFile(info.isDir() ? info.absoluteFilePath() : info.absolutePath()));
#endif
    if (!opened) statusBar()->showMessage(QStringLiteral("无法打开系统文件管理器。"), 6000);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_fileSearchInput || watched == m_fileSearchResults) {
        if (event->type() == QEvent::ShortcutOverride && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
            event->accept(); return true;
        }
        if (event->type() == QEvent::KeyPress) {
            const auto key = static_cast<QKeyEvent *>(event)->key();
            if (key == Qt::Key_Escape) {
                m_fileSearchInput->clear(); m_fileSearchInput->setFocus(); return true;
            }
            if (watched == m_fileSearchResults && (key == Qt::Key_Return || key == Qt::Key_Enter)) {
                openFileSearchResult(); return true;
            }
            if (watched == m_fileSearchInput && (key == Qt::Key_Down || key == Qt::Key_Up) && m_fileSearchResults->count()) {
                m_fileSearchResults->setFocus();
                m_fileSearchResults->setCurrentRow(key == Qt::Key_Down ? 0 : m_fileSearchResults->count() - 1);
                return true;
            }
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

bool MainWindow::save(Editor *editor, bool saveAs)
{
    if (!editor || editor->loading) return false;
    QString path = editor->path;
    if (saveAs || path.isEmpty()) {
        const QString directory = m_storage->lastDirectory().isEmpty() ? QDir::homePath() : m_storage->lastDirectory();
        const QString initial = path.isEmpty() ? QDir(directory).filePath(editor->displayName + ".txt") : path;
        path = QFileDialog::getSaveFileName(this, QStringLiteral("保存文件"), initial, QStringLiteral("所有文件 (*)"));
        if (path.isEmpty()) return false;
        path = canonicalPath(path);
        for (int i = 0; i < m_tabs->count(); ++i) {
            const auto *other = qobject_cast<Editor *>(m_tabs->widget(i));
            if (other != editor && other->path == path) {
                QMessageBox::warning(this, QStringLiteral("无法保存"), QStringLiteral("目标文件已经在另一个标签页打开，请先关闭该标签页。")); return false;
            }
        }
    }
    const QFileInfo disk(path);
    if (path == editor->path && (!disk.exists() || disk.lastModified() != editor->diskModified || disk.size() != editor->diskSize)) {
        if (QMessageBox::warning(this, QStringLiteral("文件已在磁盘上更改"), QStringLiteral("磁盘上的文件已被其他程序修改或删除。仍要用当前内容覆盖吗？"), QMessageBox::Save | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Save) return false;
    }
    if (editor->mixedEndings && QMessageBox::question(this, QStringLiteral("统一换行符"),
        QStringLiteral("文件含有混合换行符。本次保存会统一为 %1，是否继续？").arg(lineEndingName(editor->ending)), QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return false;
    QString error;
    if (!saveFile(path, editor->contents(), editor->encoding, editor->ending, &error)) {
        QMessageBox::critical(this, QStringLiteral("保存失败"), error); return false;
    }
    const bool newPath = editor->path != canonicalPath(path);
    editor->path = canonicalPath(path); editor->displayName = QFileInfo(path).fileName();
    editor->diskModified = QFileInfo(path).lastModified(); editor->diskSize = QFileInfo(path).size();
    editor->mixedEndings = false; editor->metadataModified = false;
    editor->document()->setModified(false); editor->setLanguage(QFileInfo(path).suffix());
    updateTab(editor); updateStatus();
    if (newPath) addRecent(path, m_folderRoot);
    statusBar()->showMessage(QStringLiteral("已保存 %1").arg(editor->displayName), 4000); return true;
}

bool MainWindow::confirmDiscard(Editor *editor)
{
    if (!editor->dirty()) return true;
    const auto answer = QMessageBox::warning(this, QStringLiteral("保存更改"), QStringLiteral("是否保存对“%1”的更改？").arg(editor->displayName),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Cancel) return false;
    return answer == QMessageBox::Discard || save(editor);
}

bool MainWindow::closeTab(int index)
{
    auto *editor = qobject_cast<Editor *>(m_tabs->widget(index));
    if (!editor || !confirmDiscard(editor)) return false;
    m_tabs->removeTab(index); editor->deleteLater();
    updateWorkspacePage();
    refreshDocumentList(); return true;
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (!confirmDiscard(qobject_cast<Editor *>(m_tabs->widget(i)))) { event->ignore(); return; }
    }
    m_sidebarTransition->finish(); m_searchTransition->finish(); m_pageTransition->finish();
    m_storage->setValue("window/sidebarWidth", m_sidebarTransition->expandedExtent());
    m_storage->setValue("window/geometry", saveGeometry()); m_storage->setValue("window/splitter", m_splitter->saveState());
    m_storage->setValue("appearance/sidebar", m_sidebarAction->isChecked());
    event->accept();
}

void MainWindow::updateTab(Editor *editor)
{
    const int index = m_tabs->indexOf(editor); if (index < 0) return;
    m_tabs->setTabText(index, editor->displayName + (editor->loading ? QStringLiteral(" …") : editor->dirty() ? QStringLiteral("  ●") : QString()));
    m_tabs->setTabToolTip(index, editor->path.isEmpty() ? QStringLiteral("尚未保存") : editor->path);
    refreshDocumentList();
}

void MainWindow::refreshDocumentList()
{
    QSignalBlocker blocker(m_documents);
    m_documents->clear();
    for (int i = 0; i < m_tabs->count(); ++i) {
        auto *item = new QListWidgetItem(Theme::icon("file"), m_tabs->tabText(i), m_documents);
        item->setToolTip(m_tabs->tabToolTip(i));
    }
    m_documents->setCurrentRow(m_tabs->currentIndex());
    m_documents->setFixedHeight(qBound(50, m_tabs->count() * 38 + 6, 200));
}

void MainWindow::updateStatus()
{
    if (!m_position || !m_saveAction) return;
    auto *editor = current();
    if (!editor) {
        m_saveAction->setEnabled(false); m_undoAction->setEnabled(false); m_redoAction->setEnabled(false);
        m_fileLabel->setText(QStringLiteral("欢迎使用 NyaEdit"));
        for (auto *button : {m_position, m_encoding, m_ending, m_zoom}) { button->setText({}); button->setEnabled(false); }
        setWindowTitle(QStringLiteral("欢迎 — NyaEdit")); return;
    }
    m_position->setEnabled(true); m_zoom->setEnabled(true);
    const auto cursor = editor->textCursor();
    const int selected = cursor.selectionEnd() - cursor.selectionStart();
    m_position->setText(QStringLiteral("行 %1，列 %2%3").arg(cursor.blockNumber() + 1).arg(cursor.positionInBlock() + 1)
        .arg(selected ? QStringLiteral(" · 已选 %1").arg(selected) : QString()));
    m_encoding->setText(editor->encoding.label());
    m_encoding->setToolTip(editor->encoding.detected ? QStringLiteral("自动检测：%1，置信度 %2%\n点击重新解码或转换保存编码").arg(editor->encoding.label()).arg(editor->encoding.confidence) : QStringLiteral("手动指定的保存编码；点击更改"));
    m_ending->setText(lineEndingName(editor->ending));
    m_zoom->setText(QStringLiteral("%1 pt").arg(editor->fontSize()));
    const QString kind = editor->path.isEmpty() ? QStringLiteral("纯文本") : QFileInfo(editor->path).suffix().toUpper();
    m_fileLabel->setText(editor->loading ? QStringLiteral("正在读取…") : QStringLiteral("%1  ·  %2 行%3").arg(kind.isEmpty() ? QStringLiteral("纯文本") : kind).arg(editor->blockCount()).arg(editor->largeFile ? QStringLiteral("  ·  大文件模式") : QString()));
    m_saveAction->setEnabled(!editor->loading);
    m_undoAction->setEnabled(!editor->loading && editor->document()->isUndoAvailable());
    m_redoAction->setEnabled(!editor->loading && editor->document()->isRedoAvailable());
    m_encoding->setEnabled(!editor->loading); m_ending->setEnabled(!editor->loading);
    setWindowTitle(QStringLiteral("%1%2 — NyaEdit").arg(editor->dirty() ? QStringLiteral("● ") : QString(), editor->displayName));
}

void MainWindow::addRecent(const QString &path, const QString &folderRoot)
{
    if (!folderRoot.isEmpty()) {
        const QString relative = QDir(folderRoot).relativeFilePath(canonicalPath(path));
        if (!QDir::isAbsolutePath(relative) && relative != ".." && !relative.startsWith("../")) return;
    }
    m_storage->record(path, RecentKind::File);
}

void MainWindow::rebuildRecentMenu()
{
    m_recent->clear();
    const auto &entries = m_storage->history();
    if (entries.isEmpty()) { m_recent->addAction(QStringLiteral("暂无历史记录"))->setEnabled(false); return; }
    for (const auto &entry : entries) {
        const bool folder = entry.kind == RecentKind::Folder;
        auto *action = m_recent->addAction(Theme::icon(folder ? "folder" : "file"), entry.path, this,
                                           [this, entry, folder] { openRecent(entry.path, folder); });
        action->setToolTip(folder ? QStringLiteral("文件夹：%1").arg(entry.path) : QStringLiteral("文件：%1").arg(entry.path));
    }
    m_recent->addSeparator();
    m_recent->addAction(QStringLiteral("清除历史记录"), this, &MainWindow::clearHistory);
}

void MainWindow::openRecent(const QString &path, bool folder)
{
    const QFileInfo info(path);
    if (folder ? !info.isDir() : !info.isFile()) {
        QMessageBox::warning(this, QStringLiteral("项目不可用"), QStringLiteral("该%1已移动、删除或无法访问：\n%2").arg(folder ? QStringLiteral("文件夹") : QStringLiteral("文件"), path)); return;
    }
    if (folder) openFolder(path); else openPath(path);
}

void MainWindow::clearHistory()
{
    ++m_historyEpoch;
    if (m_storage->clearHistory()) statusBar()->showMessage(QStringLiteral("已清除所有文件与文件夹历史记录。"), 5000);
}

void MainWindow::updateWorkspacePage()
{
    if (!m_pages) return;
    m_pages->setCurrentWidget(m_tabs->count() ? static_cast<QWidget *>(m_tabs) : static_cast<QWidget *>(m_welcome));
    if (!m_tabs->count()) {
        m_welcome->setHistory(m_storage->history()); hideSearch();
    }
    updateStatus();
}

void MainWindow::setDark(bool dark)
{
    m_dark = dark; Theme::apply(*qApp, dark); m_storage->setValue("appearance/dark", dark);
    for (int i = 0; i < m_tabs->count(); ++i) qobject_cast<Editor *>(m_tabs->widget(i))->setDark(dark);
}

QFont MainWindow::configuredEditorFont() const
{
    const auto family = m_storage->value("editor/fontFamily").toString().trimmed();
    auto font = family.isEmpty() ? Editor::defaultEditorFont() : QFont(family);
    font.setPointSize(qBound(Editor::MinimumFontSize, m_storage->value("editor/fontSize", 13).toInt(), Editor::MaximumFontSize));
    return font;
}

void MainWindow::storeEditorFont(const QFont &font)
{
    if (m_storage->value("editor/fontFamily").toString() != font.family()) m_storage->setValue("editor/fontFamily", font.family());
    if (m_storage->value("editor/fontSize").toInt() != font.pointSize()) m_storage->setValue("editor/fontSize", font.pointSize());
}

void MainWindow::chooseEditorFont()
{
    FontSettingsDialog dialog(current() ? current()->editorFont() : configuredEditorFont(), this);
    if (dialog.exec() != QDialog::Accepted) return;
    const auto font = dialog.selectedFont();
    storeEditorFont(font);
    for (int i = 0; i < m_tabs->count(); ++i) {
        auto *editor = qobject_cast<Editor *>(m_tabs->widget(i));
        QSignalBlocker blocker(editor); editor->setEditorFont(font);
    }
    updateStatus();
}

void MainWindow::chooseAnimationSettings()
{
    AnimationSettingsDialog dialog(m_animations, this);
    if (dialog.exec() != QDialog::Accepted) return;
    m_animations = dialog.selectedSettings();
    m_storage->setValue("animation/interface", m_animations.interfaceEnabled);
    m_storage->setValue("animation/cursor", m_animations.cursorEnabled);
    m_storage->setValue("animation/selection", m_animations.selectionEnabled);
    m_storage->setValue("animation/speed", m_animations.speed);
    applyAnimationSettings();
}

void MainWindow::applyAnimationSettings()
{
    m_sidebarTransition->finish(); m_searchTransition->finish(); m_pageTransition->finish();
    m_tree->setAnimationSettings(m_animations);
    for (int i = 0; i < m_tabs->count(); ++i) qobject_cast<Editor *>(m_tabs->widget(i))->setAnimationSettings(m_animations);
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls()) event->acceptProposedAction();
}
void MainWindow::dropEvent(QDropEvent *event)
{
    for (const QUrl &url : event->mimeData()->urls()) if (url.isLocalFile()) openPath(url.toLocalFile());
    event->acceptProposedAction();
}

void MainWindow::showSearch(bool replace)
{
    if (!current()) return;
    m_replaceRow->setVisible(replace);
    m_searchTransition->setExpanded(true, m_animations.interfaceEnabled ? m_animations.duration(180) : 0);
    if (current() && current()->textCursor().hasSelection()) {
        const QString selected = current()->textCursor().selectedText();
        if (!selected.contains(QChar::ParagraphSeparator) && selected.size() < 4096) m_find->setText(selected);
    }
    m_find->setFocus(); m_find->selectAll(); refreshSearch();
}
void MainWindow::hideSearch()
{
    m_searchTransition->setExpanded(false, m_animations.interfaceEnabled ? m_animations.duration(180) : 0);
    m_searchTimer->stop(); m_searchResult = {}; m_searchSnapshot.clear(); m_matchIndex = -1;
    if (current()) { current()->clearSearchMatches(); current()->setFocus(); }
}
SearchOptions MainWindow::searchOptions() const
{
    return {m_find->text(), m_case->isChecked(), m_word->isChecked(), m_regex->isChecked()};
}
void MainWindow::refreshSearch()
{
    m_searchTimer->stop(); m_matchIndex = -1;
    if (!m_searchTransition->isExpanded() || !current() || current()->loading) {
        m_searchResult = {}; m_searchSnapshot.clear();
        if (current()) current()->clearSearchMatches();
        return;
    }
    m_searchSnapshot = current()->contents();
    m_searchResult = Search::find(m_searchSnapshot, searchOptions());
    current()->setSearchMatches(m_searchResult.matches);
    m_matchLabel->setText(!m_searchResult.error.isEmpty() ? QStringLiteral("正则无效")
        : m_find->text().isEmpty() ? QStringLiteral("输入以查找")
        : QStringLiteral("%1%2 个匹配").arg(m_searchResult.matches.size()).arg(m_searchResult.truncated ? "+" : ""));
    m_matchLabel->setToolTip(m_searchResult.error.isEmpty() && m_searchResult.truncated ? QStringLiteral("导航最多显示 20000 个匹配；全部替换会处理整个文档。") : m_searchResult.error);
}
void MainWindow::selectMatch(int index)
{
    if (!current() || m_searchResult.matches.isEmpty()) return;
    const int count = m_searchResult.matches.size();
    m_matchIndex = (index % count + count) % count;
    const auto match = m_searchResult.matches.at(m_matchIndex);
    auto cursor = current()->textCursor(); cursor.setPosition(match.start); cursor.setPosition(match.start + match.length, QTextCursor::KeepAnchor);
    current()->setTextCursor(cursor); current()->ensureCursorVisible();
    m_matchLabel->setText(QStringLiteral("%1 / %2%3").arg(m_matchIndex + 1).arg(count).arg(m_searchResult.truncated ? "+" : ""));
}
void MainWindow::findNext(bool backwards)
{
    if (!m_searchTransition->isExpanded()) { showSearch(false); return; }
    if (m_searchTimer->isActive()) refreshSearch();
    if (!current() || m_searchResult.matches.isEmpty()) return;
    const auto cursor = current()->textCursor();
    if (m_matchIndex >= 0) {
        const auto old = m_searchResult.matches.at(m_matchIndex);
        if (cursor.selectionStart() == old.start && cursor.selectionEnd() == old.start + old.length) { selectMatch(m_matchIndex + (backwards ? -1 : 1)); return; }
    }
    const int position = backwards ? cursor.selectionStart() : cursor.selectionEnd();
    int index = 0;
    if (backwards) {
        index = m_searchResult.matches.size() - 1;
        for (int i = index; i >= 0; --i) if (m_searchResult.matches.at(i).start < position) { index = i; break; }
    } else {
        for (int i = 0; i < m_searchResult.matches.size(); ++i) if (m_searchResult.matches.at(i).start >= position) { index = i; break; }
    }
    selectMatch(index);
}
void MainWindow::replaceCurrent()
{
    if (!current() || current()->loading) return;
    if (m_searchTimer->isActive()) refreshSearch();
    auto cursor = current()->textCursor();
    if (m_matchIndex < 0 || m_searchResult.matches.isEmpty()) { findNext(); return; }
    const auto found = m_searchResult.matches.at(m_matchIndex);
    if (cursor.selectionStart() != found.start || cursor.selectionEnd() != found.start + found.length) { findNext(); return; }
    QString replacement = m_replace->text();
    if (m_regex->isChecked()) {
        auto it = Search::expression(searchOptions()).globalMatch(m_searchSnapshot);
        // Use the same global iterator as search, including its handling of zero-length matches.
        for (int i = 0; i <= m_matchIndex && it.hasNext(); ++i) {
            const auto match = it.next(); if (i == m_matchIndex) replacement = Search::expand(replacement, match);
        }
    }
    cursor.beginEditBlock(); cursor.insertText(replacement); cursor.endEditBlock(); current()->setTextCursor(cursor);
    const int resume = cursor.position() + (found.length == 0 ? 1 : 0);
    refreshSearch();
    for (int i = 0; i < m_searchResult.matches.size(); ++i) if (m_searchResult.matches.at(i).start >= resume) { selectMatch(i); return; }
    if (!m_searchResult.matches.isEmpty()) selectMatch(0);
}
void MainWindow::replaceAll()
{
    auto *editor = current(); if (!editor || editor->loading) return;
    int count = 0; QString error;
    const QString before = editor->contents();
    const QString after = Search::replaceAll(before, searchOptions(), m_replace->text(), &count, &error);
    if (!error.isEmpty()) { m_matchLabel->setText(QStringLiteral("正则无效")); m_matchLabel->setToolTip(error); return; }
    if (count && before != after) {
        auto cursor = editor->textCursor(); const int position = cursor.position();
        cursor.beginEditBlock(); cursor.select(QTextCursor::Document); cursor.insertText(after); cursor.endEditBlock();
        cursor.setPosition(qMin(position, int(after.size()))); editor->setTextCursor(cursor);
    }
    refreshSearch(); statusBar()->showMessage(QStringLiteral("已替换 %1 处，可一次撤销").arg(count), 6000);
}

void MainWindow::showEncodingMenu()
{
    auto *editor = current(); if (!editor || editor->loading) return;
    QMenu menu(this);
    auto *info = menu.addAction(QStringLiteral("当前：%1%2").arg(editor->encoding.label(), editor->encoding.detected ? QStringLiteral(" · 自动检测 %1%").arg(editor->encoding.confidence) : QString())); info->setEnabled(false);
    auto *reopen = menu.addMenu(QStringLiteral("按另一编码重新打开（重新解码）")); reopen->setEnabled(!editor->path.isEmpty());
    for (const QString &name : EncodingService::supported()) reopen->addAction(name, this, [this, name] { reopenEncoding(name); });
    auto *convert = menu.addMenu(QStringLiteral("转换保存编码（保存时生效）"));
    for (const QString &name : EncodingService::supported()) {
        auto *action = convert->addAction(name, this, [this, name] { changeEncoding(name, false); });
        action->setCheckable(true); action->setChecked(editor->encoding.name.compare(name, Qt::CaseInsensitive) == 0 && !editor->encoding.bom);
        if (!EncodingService::bomFor(name).isEmpty()) {
            auto *bomAction = convert->addAction(name + " BOM", this, [this, name] { changeEncoding(name, true); });
            bomAction->setCheckable(true); bomAction->setChecked(editor->encoding.name.compare(name, Qt::CaseInsensitive) == 0 && editor->encoding.bom);
        }
    }
    menu.exec(m_encoding->mapToGlobal(QPoint(0, -menu.sizeHint().height())));
}

void MainWindow::changeEncoding(const QString &name, bool bom)
{
    auto *editor = current(); if (!editor || editor->loading) return;
    const Encoding encoding{name, bom, 100, false}; QString error;
    EncodingService::encode(editor->contents(), encoding, &error);
    if (!error.isEmpty()) { QMessageBox::warning(this, QStringLiteral("无法转换编码"), error); return; }
    if (editor->encoding.name == name && editor->encoding.bom == bom) return;
    editor->encoding = encoding; editor->metadataModified = true; updateTab(editor); updateStatus();
    statusBar()->showMessage(QStringLiteral("保存编码已设为 %1，保存后生效。").arg(encoding.label()), 5000);
}

void MainWindow::reopenEncoding(const QString &name)
{
    auto *editor = current(); if (!editor || editor->path.isEmpty() || editor->loading) return;
    if (!confirmDiscard(editor)) return;
    editor->loading = true; editor->setReadOnly(true); updateTab(editor); updateStatus();
    auto *watcher = new QFutureWatcher<LoadedFile>(this); QPointer<Editor> guard(editor);
    const QString path = editor->path;
    connect(watcher, &QFutureWatcher<LoadedFile>::finished, this, [this, watcher, guard] {
        const auto result = watcher->result(); watcher->deleteLater(); if (!guard || m_tabs->indexOf(guard) < 0) return;
        auto *editor = guard.data(); editor->loading = false; editor->setReadOnly(false);
        if (!result.ok()) { updateTab(editor); updateStatus(); QMessageBox::warning(this, QStringLiteral("重新打开失败"), result.error); return; }
        const int position = editor->textCursor().position();
        editor->encoding = result.encoding; editor->ending = result.ending; editor->mixedEndings = result.mixedEndings;
        editor->diskModified = result.modified; editor->diskSize = result.byteSize; editor->largeFile = result.byteSize > 2 * 1024 * 1024;
        editor->metadataModified = false; editor->setPlainText(result.text); editor->document()->setModified(false);
        auto cursor = editor->textCursor(); cursor.setPosition(qMin(position, int(result.text.size()))); editor->setTextCursor(cursor);
        editor->setLanguage(QFileInfo(result.path).suffix()); updateTab(editor); updateStatus(); refreshSearch();
    });
    watcher->setFuture(QtConcurrent::run([path, name] { return loadFile(path, name); }));
}

void MainWindow::showEndingMenu()
{
    auto *editor = current(); if (!editor || editor->loading) return;
    QMenu menu(this);
    for (const auto ending : {LineEnding::LF, LineEnding::CRLF, LineEnding::CR}) {
        auto *action = menu.addAction(lineEndingName(ending)); action->setCheckable(true); action->setChecked(editor->ending == ending);
        connect(action, &QAction::triggered, this, [this, editor, ending] {
            if (editor->ending == ending) return;
            editor->ending = ending; editor->metadataModified = true; updateTab(editor); updateStatus();
        });
    }
    menu.exec(m_ending->mapToGlobal(QPoint(0, -menu.sizeHint().height())));
}

void MainWindow::gotoLine()
{
    auto *editor = current(); if (!editor || editor->loading) return;
    bool ok = false;
    const int line = QInputDialog::getInt(this, QStringLiteral("跳转到行"), QStringLiteral("行号（1–%1）").arg(editor->blockCount()), editor->textCursor().blockNumber() + 1, 1, editor->blockCount(), 1, &ok);
    if (!ok) return;
    auto cursor = editor->textCursor(); cursor.setPosition(editor->document()->findBlockByNumber(line - 1).position());
    editor->setTextCursor(cursor); editor->centerCursor(); editor->setFocus();
}

void MainWindow::checkExternalChanges()
{
    if (m_checkingExternal) return;
    auto *editor = current(); if (!editor || editor->loading || editor->path.isEmpty()) return;
    const QFileInfo info(editor->path);
    if (!info.exists() || (info.lastModified() == editor->diskModified && info.size() == editor->diskSize)) return;
    m_checkingExternal = true;
    const auto answer = QMessageBox::question(this, QStringLiteral("磁盘文件已更新"),
        QStringLiteral("“%1”已被其他程序修改。是否从磁盘重新载入？%2").arg(editor->displayName, editor->dirty() ? QStringLiteral("\n当前有未保存的更改，重新载入前会询问是否保存。") : QString()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer == QMessageBox::Yes) reopenEncoding(editor->encoding.name);
    m_checkingExternal = false;
}
}
