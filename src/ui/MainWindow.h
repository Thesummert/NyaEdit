#pragma once
#include "Search.h"
#include "AppStorage.h"
#include "Animation.h"
#include "TreeSitter.h"
#include <QFont>
#include <QMainWindow>
#include <QJsonObject>
#include <QPointer>
#include <QUrl>
#include <atomic>
#include <memory>

class QAction;
class QCloseEvent;
class QComboBox;
class QDockWidget;
class QFileSystemModel;
class QFrame;
class QLabel;
class QLineEdit;
class QListWidget;
class QMenu;
class QSplitter;
class QStackedWidget;
class QTabWidget;
class QTextBrowser;
class QTimer;
class QToolButton;
class QTreeView;

namespace nya {
class Editor;
class EditorBackground;
class EditorLsp;
class FileTreeView;
class TaskPanel;
class TaskRunner;
class WelcomePage;
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr, StoragePaths storagePaths = {});
    ~MainWindow() override;
    void openPath(const QString &path, const QString &forcedEncoding = {});
    void openFolder(const QString &path = {});
    void newDocument();
protected:
    void paintEvent(QPaintEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    virtual void revealInFileManager(const QString &path);
    virtual void openInTerminal(const QString &directory);
private:
    void setupMenus();
    void setupToolbar();
    void setupWorkspace();
    void setupSearch();
    void setupStatus();
    void setupMarkdownPreview();
    void updateMarkdownPreview();
    void setupTasks();
    void chooseTask();
    void runTask(const QString &label);
    void runBuildTask();
    void configureTasks();
    void openProjectTerminal();
    void rebuildTasksMenu();
    void updateTaskActions();
    void reportTaskError(const QString &error);
    Editor *current() const;
    Editor *addEditor(const QString &name);
    void chooseFiles(const QString &encoding = {});
    bool save(Editor *editor, bool saveAs = false);
    bool confirmDiscard(Editor *editor);
    bool closeTab(int index);
    void updateTab(Editor *editor);
    void updateStatus();
    void updateTreeSitterStatus();
    void showTreeSitterError();
    void refreshDocumentList();
    void rebuildRecentMenu();
    void addRecent(const QString &path, const QString &folderRoot);
    void openRecent(const QString &path, bool folder);
    void clearHistory();
    void updateWorkspacePage();
    void setDark(bool dark);
    void chooseEditorFont();
    void chooseBackgroundSettings();
    void updateBackgroundSurface();
    void chooseAnimationSettings();
    void chooseLanguageSettings();
    void updateLspStatus();
    void showDefinitions(Editor *source, const QJsonArray &locations);
    void navigateToDefinition(const QJsonObject &location);
    void applyDefinitionPosition(Editor *editor);
    void applyAnimationSettings();
    QFont configuredEditorFont() const;
    void storeEditorFont(const QFont &font);
    void showSearch(bool replace);
    void hideSearch();
    SearchOptions searchOptions() const;
    void refreshSearch();
    void findNext(bool backwards = false);
    void selectMatch(int index);
    void replaceCurrent();
    void replaceAll();
    void showEncodingMenu();
    void reopenEncoding(const QString &name);
    void changeEncoding(const QString &name, bool bom);
    void showEndingMenu();
    void gotoLine();
    void checkExternalChanges();
    void focusFileSearch();
    void scheduleFileSearch();
    void runFileSearch();
    void openFileSearchResult();
    void showFileContextMenu(const QString &path, const QPoint &globalPosition);

    QTabWidget *m_tabs = nullptr;
    QDockWidget *m_markdownDock = nullptr;
    QTextBrowser *m_markdownPreview = nullptr;
    QTimer *m_markdownTimer = nullptr;
    QPointer<Editor> m_markdownEditor;
    QString m_markdownPath;
    int m_markdownRevision = -2;
    bool m_markdownLoading = false;
    QStackedWidget *m_pages = nullptr;
    WelcomePage *m_welcome = nullptr;
    AppStorage *m_storage = nullptr;
    EditorBackground *m_background = nullptr;
    quint64 m_historyEpoch = 0;
    QSplitter *m_splitter = nullptr;
    QFrame *m_sidebar = nullptr;
    PanelTransition *m_sidebarTransition = nullptr;
    PanelTransition *m_searchTransition = nullptr;
    PageTransition *m_pageTransition = nullptr;
    AnimationSettings m_animations;
    LanguageSettings m_languages;
    EditorLsp *m_lsp = nullptr;
    QToolButton *m_lspStatus = nullptr;
    QAction *m_completionAction = nullptr;
    QAction *m_definitionAction = nullptr;
    QPointer<Editor> m_definitionTarget;
    QJsonObject m_definitionRange;
    QListWidget *m_documents = nullptr;
    QFileSystemModel *m_files = nullptr;
    FileTreeView *m_tree = nullptr;
    QLabel *m_folderLabel = nullptr;
    QLabel *m_folderHint = nullptr;
    QLineEdit *m_fileSearchInput = nullptr;
    QAction *m_fileSearchFuzzyAction = nullptr;
    QLabel *m_fileSearchStatus = nullptr;
    QListWidget *m_fileSearchResults = nullptr;
    QTimer *m_fileSearchTimer = nullptr;
    QString m_folderRoot;
    quint64 m_fileSearchGeneration = 0;
    std::shared_ptr<std::atomic_bool> m_fileSearchCancel;
    QMenu *m_recent = nullptr;
    QMenu *m_tasksMenu = nullptr;
    QDockWidget *m_taskDock = nullptr;
    TaskPanel *m_taskPanel = nullptr;
    TaskRunner *m_taskRunner = nullptr;
    QAction *m_runTaskAction = nullptr;
    QAction *m_buildTaskAction = nullptr;
    QAction *m_repeatTaskAction = nullptr;
    QAction *m_stopTaskAction = nullptr;
    QString m_lastTaskLabel;
    QString m_lastTaskRoot;
    QFrame *m_searchPanel = nullptr;
    QWidget *m_replaceRow = nullptr;
    QLineEdit *m_find = nullptr;
    QLineEdit *m_replace = nullptr;
    QToolButton *m_case = nullptr;
    QToolButton *m_word = nullptr;
    QToolButton *m_regex = nullptr;
    QLabel *m_matchLabel = nullptr;
    QLabel *m_fileLabel = nullptr;
    QToolButton *m_treeSitterStatus = nullptr;
    QToolButton *m_position = nullptr;
    QToolButton *m_encoding = nullptr;
    QToolButton *m_ending = nullptr;
    QToolButton *m_zoom = nullptr;
    QAction *m_saveAction = nullptr;
    QAction *m_undoAction = nullptr;
    QAction *m_redoAction = nullptr;
    QAction *m_wrapAction = nullptr;
    QAction *m_sidebarAction = nullptr;
    QTimer *m_searchTimer = nullptr;
    SearchResult m_searchResult;
    QString m_searchSnapshot;
    int m_matchIndex = -1;
    int m_untitled = 0;
    bool m_dark = true;
    bool m_backgroundVisible = false;
    bool m_checkingExternal = false;
    bool m_wrap = false;
};
}
