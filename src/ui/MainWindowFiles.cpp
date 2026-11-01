#include "MainWindow.h"
#include "Editor.h"
#include "EditorLsp.h"
#include "FileTreeView.h"
#include "Theme.h"
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QRegularExpression>
#include <QShortcut>
#include <QStatusBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <functional>

namespace nya {
namespace {
bool containsPath(const QString &parent, const QString &path)
{
    const QString relative = QDir(parent).relativeFilePath(path);
    return relative == "." || (!QDir::isAbsolutePath(relative) && relative != ".." && !relative.startsWith("../"));
}
QString targetDirectory(const QString &path)
{
    const QFileInfo info(path);
    return info.isDir() ? info.absoluteFilePath() : info.absolutePath();
}
bool exists(const QString &path) { return QFileInfo::exists(path) || QFileInfo(path).isSymLink(); }
bool validName(const QString &name)
{
    return !name.trimmed().isEmpty() && name != "." && name != ".."
        && !name.contains('/') && !name.contains('\\') && !name.contains(QChar::Null)
#ifdef Q_OS_WIN
        && !name.contains(QRegularExpression(QStringLiteral("[<>:\"|?*]"))) && !name.endsWith('.') && !name.endsWith(' ')
#endif
        ;
}
// Stage recursive copies beside the destination. Failed copies never leave a
// partially populated destination, and existing files are never overwritten.
bool copyEntry(const QString &source, const QString &destination, QString &error)
{
    const QFileInfo info(source);
    if (info.isSymLink()) {
        error = QStringLiteral("暂不支持复制符号链接：%1").arg(source); return false;
    }
    if (info.isFile()) {
        QFile file(source);
        if (file.copy(destination)) return true;
        error = QStringLiteral("无法复制 %1：%2").arg(source, file.errorString()); return false;
    }
    if (!info.isDir() || !info.isReadable() || !QDir().mkdir(destination)) {
        error = QStringLiteral("无法读取或创建文件夹：%1").arg(source); return false;
    }
    const auto entries = QDir(source).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    for (const auto &entry : entries)
        if (!copyEntry(entry.absoluteFilePath(), QDir(destination).filePath(entry.fileName()), error)) return false;
    return true;
}
QString unusedDestination(const QString &directory, const QFileInfo &source)
{
    const QDir target(directory);
    QString path = target.filePath(source.fileName());
    const QString suffix = source.isDir() || source.suffix().isEmpty() ? QString() : "." + source.suffix();
    const QString stem = suffix.isEmpty() ? source.fileName() : source.completeBaseName();
    for (int number = 1; exists(path); ++number)
        path = target.filePath(stem + QStringLiteral(" - 副本%1").arg(number == 1 ? QString() : QString::number(number)) + suffix);
    return path;
}
}

void MainWindow::setupFileActions()
{
    const auto selectedPath = [this] {
        return m_tree->currentIndex().isValid() ? m_files->filePath(m_tree->currentIndex()) : m_folderRoot;
    };
    const auto shortcut = [this](const QKeySequence &keys, const std::function<void()> &callback) {
        auto *key = new QShortcut(keys, m_tree);
        key->setContext(Qt::WidgetWithChildrenShortcut);
        connect(key, &QShortcut::activated, this, callback);
    };
    shortcut(QKeySequence(Qt::Key_F2), [this, selectedPath] { renameTreeEntry(selectedPath()); });
    shortcut(QKeySequence::Copy, [this, selectedPath] { copyTreeEntry(selectedPath(), false); });
    shortcut(QKeySequence::Cut, [this, selectedPath] { copyTreeEntry(selectedPath(), true); });
    shortcut(QKeySequence::Paste, [this, selectedPath] { pasteTreeEntry(selectedPath()); });
    shortcut(QKeySequence::Delete, [this, selectedPath] { deleteTreeEntry(selectedPath()); });
    shortcut(QKeySequence(QStringLiteral("Shift+Delete")), [this, selectedPath] { deleteTreeEntry(selectedPath(), true); });
}

void MainWindow::showFileContextMenu(const QString &path, const QPoint &position, bool manageFiles, bool blank)
{
    QMenu menu(this); menu.setObjectName("fileContextMenu");
    const QFileInfo info(path);
    const bool available = !path.isEmpty() && info.exists();
    const bool root = path == m_folderRoot;
    const auto add = [&](const QString &text, const char *name, bool enabled, const std::function<void()> &callback) {
        auto *action = menu.addAction(text); action->setObjectName(name); action->setEnabled(enabled);
        connect(action, &QAction::triggered, this, callback); return action;
    };
    if (manageFiles) {
        add(QStringLiteral("新建文件…"), "actionNewFile", available, [this, path] { createTreeEntry(path, false); });
        add(QStringLiteral("新建文件夹…"), "actionNewFolder", available, [this, path] { createTreeEntry(path, true); });
        menu.addSeparator();
        add(QStringLiteral("重命名…"), "actionRenameFile", available && !root, [this, path] { renameTreeEntry(path); });
        add(QStringLiteral("复制"), "actionCopyFile", available, [this, path] { copyTreeEntry(path, false); });
        add(QStringLiteral("剪切"), "actionCutFile", available && !root, [this, path] { copyTreeEntry(path, true); });
        const auto *clipboard = qApp->clipboard()->mimeData();
        const auto urls = clipboard ? clipboard->urls() : QList<QUrl>();
        add(QStringLiteral("粘贴"), "actionPasteFile", available && urls.size() == 1 && urls.first().isLocalFile(),
            [this, path] { pasteTreeEntry(path); });
        add(QStringLiteral("复制路径"), "actionCopyFilePath", available,
            [path] { qApp->clipboard()->setText(QDir::toNativeSeparators(path)); });
        menu.addSeparator();
        const bool shift = QApplication::keyboardModifiers().testFlag(Qt::ShiftModifier);
        auto *remove = add(shift ? QStringLiteral("永久删除…") : QStringLiteral("移到回收站…（Shift：永久删除）"),
            "actionTrashFile", available && !root, [this, path] {
                deleteTreeEntry(path, QApplication::keyboardModifiers().testFlag(Qt::ShiftModifier));
            });
        remove->setToolTip(QStringLiteral("按住 Shift 点击可永久删除，不经过回收站。"));
        menu.addSeparator();
    }
    // Keep the existing reveal action absent on blank tree space.
    if (!blank) {
#ifdef Q_OS_MAC
        const auto text = QStringLiteral("在 Finder 中显示");
#elif defined(Q_OS_WIN)
        const auto text = QStringLiteral("在文件资源管理器中显示");
#else
        const auto text = QStringLiteral("在文件管理器中打开");
#endif
        auto *reveal = add(text, "actionRevealInFileManager", available, [this, path] { revealInFileManager(path); });
        reveal->setIcon(Theme::icon("folder")); reveal->setData(path);
        reveal->setToolTip(path.isEmpty() ? QStringLiteral("请先保存文档")
            : !available ? QStringLiteral("文件或文件夹已不存在") : path);
    }
    menu.exec(position);
}

void MainWindow::createTreeEntry(const QString &path, bool directory)
{
    if (path.isEmpty() || !QFileInfo(path).exists()) return;
    const QString parent = targetDirectory(path);
    bool ok = false;
    const QString name = QInputDialog::getText(this, directory ? QStringLiteral("新建文件夹") : QStringLiteral("新建文件"),
        QStringLiteral("名称"), QLineEdit::Normal, {}, &ok);
    if (!ok) return;
    if (!validName(name)) { QMessageBox::warning(this, QStringLiteral("创建失败"), QStringLiteral("请输入有效的名称，不能包含路径分隔符。")); return; }
    const QString destination = QDir(parent).filePath(name);
    QFile file(destination);
    if (exists(destination) || !(directory ? QDir().mkdir(destination) : file.open(QIODevice::WriteOnly | QIODevice::NewOnly))) {
        QMessageBox::warning(this, QStringLiteral("创建失败"), QStringLiteral("名称已存在或目录不可写：%1").arg(destination)); return;
    }
    file.close(); refreshTreeEntry(destination);
    if (!directory) openPath(destination);
}

bool MainWindow::treeEntryBusy(const QString &path)
{
    for (int i = 0; i < m_tabs->count(); ++i) {
        const auto *editor = qobject_cast<Editor *>(m_tabs->widget(i));
        if (editor->loading && !editor->path.isEmpty() && containsPath(path, editor->path)) {
            QMessageBox::warning(this, QStringLiteral("暂时无法操作"), QStringLiteral("文件正在加载，请完成后重试。")); return true;
        }
    }
    return false;
}

void MainWindow::renameTreeEntry(const QString &path)
{
    if (path.isEmpty() || path == m_folderRoot || !exists(path) || treeEntryBusy(path)) return;
    const QFileInfo info(path);
    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("重命名"), QStringLiteral("新名称"), QLineEdit::Normal, info.fileName(), &ok);
    if (!ok || name == info.fileName()) return;
    if (!validName(name)) { QMessageBox::warning(this, QStringLiteral("重命名失败"), QStringLiteral("请输入有效的文件或文件夹名称。")); return; }
    const QString destination = info.dir().filePath(name);
    if (exists(destination) || !QDir().rename(path, destination)) {
        QMessageBox::warning(this, QStringLiteral("重命名失败"), QStringLiteral("目标已存在或没有操作权限：%1").arg(destination)); return;
    }
    relocateEditors(path, destination); refreshTreeEntry(destination);
}

void MainWindow::copyTreeEntry(const QString &path, bool cut)
{
    if (path.isEmpty() || !exists(path) || (cut && path == m_folderRoot)) return;
    auto *data = new QMimeData;
    data->setUrls({QUrl::fromLocalFile(path)});
    if (cut) data->setData("application/x-nyaedit-cut", "1");
    qApp->clipboard()->setMimeData(data);
    statusBar()->showMessage(cut ? QStringLiteral("已剪切，选择目标文件夹后粘贴。") : QStringLiteral("已复制，选择目标文件夹后粘贴。"), 4000);
}

void MainWindow::pasteTreeEntry(const QString &path)
{
    if (path.isEmpty() || !QFileInfo(path).exists()) return;
    const auto *data = qApp->clipboard()->mimeData();
    if (!data) return;
    const auto urls = data->urls();
    if (urls.size() != 1 || !urls.first().isLocalFile()) return;
    const QString source = QDir::cleanPath(urls.first().toLocalFile());
    const QFileInfo info(source);
    const bool cut = data->data("application/x-nyaedit-cut") == "1";
    if (!info.exists() || (cut && (source == m_folderRoot || treeEntryBusy(source)))) return;
    const QString directory = targetDirectory(path);
    if (cut && info.absolutePath() == directory) return;
    if (info.isDir() && containsPath(info.canonicalFilePath(), QFileInfo(directory).canonicalFilePath())) {
        QMessageBox::warning(this, QStringLiteral("粘贴失败"), QStringLiteral("不能将文件夹放入自身或其子文件夹。")); return;
    }
    const QString destination = unusedDestination(directory, info);
    QString error;
    bool ok = false;
    if (cut) {
        ok = QDir().rename(source, destination);
        if (!ok) error = QStringLiteral("无法移动；请检查权限，跨磁盘移动文件夹请使用系统文件管理器。");
    } else {
        QTemporaryDir staging(QDir(directory).filePath(".nya-copy-XXXXXX"));
        if (!staging.isValid()) error = QStringLiteral("无法在目标目录创建临时文件夹。");
        else {
            const QString staged = staging.filePath(info.fileName());
            ok = copyEntry(source, staged, error) && QDir().rename(staged, destination);
            if (!ok && error.isEmpty()) error = QStringLiteral("目标已存在或无法完成复制。");
        }
    }
    if (!ok) { QMessageBox::warning(this, QStringLiteral("粘贴失败"), error); return; }
    if (cut) { relocateEditors(source, destination); qApp->clipboard()->clear(); }
    refreshTreeEntry(destination);
}

void MainWindow::relocateEditors(const QString &from, const QString &to)
{
    for (int i = 0; i < m_tabs->count(); ++i) {
        auto *editor = qobject_cast<Editor *>(m_tabs->widget(i));
        if (editor->path.isEmpty() || !containsPath(from, editor->path)) continue;
        const auto relative = QDir(from).relativeFilePath(editor->path);
        editor->path = relative == "." ? to : QDir(to).filePath(relative);
        editor->path = QFileInfo(editor->path).canonicalFilePath();
        editor->displayName = QFileInfo(editor->path).fileName();
        editor->setLanguage(QFileInfo(editor->path).suffix());
        m_lsp->refreshEditor(editor); updateTab(editor);
    }
    updateStatus();
}

void MainWindow::deleteTreeEntry(const QString &path, bool permanent)
{
    if (path.isEmpty() || path == m_folderRoot || !exists(path) || treeEntryBusy(path)) return;
    const QString prompt = permanent
        ? QStringLiteral("永久删除“%1”？\n文件夹内的所有内容也会删除，不经过回收站，无法恢复。")
        : QStringLiteral("将“%1”移到回收站？");
    if (QMessageBox::warning(this, permanent ? QStringLiteral("永久删除") : QStringLiteral("移到回收站"), prompt.arg(QFileInfo(path).fileName()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
    QList<Editor *> affected;
    for (int i = 0; i < m_tabs->count(); ++i) {
        auto *editor = qobject_cast<Editor *>(m_tabs->widget(i));
        if (!editor->path.isEmpty() && containsPath(path, editor->path)) affected.append(editor);
    }
    for (auto *editor : affected) if (!confirmDiscard(editor)) return;
    const QFileInfo info(path);
    const bool removed = permanent
        ? (info.isDir() && !info.isSymLink() ? QDir(path).removeRecursively() : QFile::remove(path))
        : QFile::moveToTrash(path);
    if (!removed) {
        QMessageBox::warning(this, QStringLiteral("删除失败"), permanent
            ? QStringLiteral("无法完成永久删除，请检查权限或文件是否被占用。文件夹内可能已有部分内容被删除。")
            : QStringLiteral("无法移到回收站，原文件已保留。"));
        scheduleFileSearch(); return;
    }
    for (auto *editor : affected) {
        m_lsp->removeEditor(editor); m_tabs->removeTab(m_tabs->indexOf(editor)); editor->deleteLater();
    }
    updateWorkspacePage(); refreshDocumentList(); updateStatus(); scheduleFileSearch();
}

void MainWindow::refreshTreeEntry(const QString &path)
{
    scheduleFileSearch();
    const auto index = m_files->index(path);
    if (index.isValid()) {
        m_tree->expand(index.parent()); m_tree->setCurrentIndex(index); m_tree->scrollTo(index);
    }
    statusBar()->showMessage(QStringLiteral("已完成：%1").arg(QFileInfo(path).fileName()), 4000);
}
}
