#pragma once
#include "Search.h"
#include "AppStorage.h"
#include "Animation.h"
#include "TreeSitter.h"
#include <QFont>
#include <QMainWindow>
#include <atomic>
#include <memory>

class QAction;
class QCloseEvent;
class QComboBox;
class QFileSystemModel;
class QFrame;
class QLabel;
class QLineEdit;
class QListWidget;
class QMenu;
class QSplitter;
class QStackedWidget;
class QTabWidget;
class QTimer;
class QToolButton;
class QTreeView;

namespace nya {
class Editor;
class FileTreeView;
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
    void closeEvent(QCloseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    virtual void revealInFileManager(const QString &path);
private:
    void setupMenus();
    void setupToolbar();
    void setupWorkspace();
    void setupSearch();
    void setupStatus();
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
    void chooseAnimationSettings();
    void chooseLanguageSettings();
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
    QStackedWidget *m_pages = nullptr;
    WelcomePage *m_welcome = nullptr;
    AppStorage *m_storage = nullptr;
    quint64 m_historyEpoch = 0;
    QSplitter *m_splitter = nullptr;
    QFrame *m_sidebar = nullptr;
    PanelTransition *m_sidebarTransition = nullptr;
    PanelTransition *m_searchTransition = nullptr;
    PageTransition *m_pageTransition = nullptr;
    AnimationSettings m_animations;
    LanguageSettings m_languages;
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
    bool m_checkingExternal = false;
    bool m_wrap = false;
};
}
