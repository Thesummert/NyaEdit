#include "WelcomePage.h"
#include "Theme.h"
#include <QFileInfo>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QKeyEvent>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace nya {
WelcomePage::WelcomePage(QWidget *parent) : QWidget(parent)
{
    setObjectName("welcomePage");
    auto *outer = new QVBoxLayout(this); outer->setContentsMargins(32, 24, 32, 24);
    outer->addStretch(1);
    auto *content = new QWidget; content->setMaximumWidth(760);
    auto *layout = new QVBoxLayout(content); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(14);
    auto *hero = new QHBoxLayout; hero->setSpacing(18);
    auto *logo = new QLabel; logo->setPixmap(Theme::appIcon().pixmap(64, 64)); hero->addWidget(logo);
    auto *titles = new QVBoxLayout;
    auto *title = new QLabel(QStringLiteral("欢迎使用 NyaEdit")); title->setObjectName("welcomeTitle"); titles->addWidget(title);
    auto *subtitle = new QLabel(QStringLiteral("轻盈落笔，专注文本。")); subtitle->setObjectName("welcomeSubtitle"); titles->addWidget(subtitle);
    hero->addLayout(titles, 1); layout->addLayout(hero);
    auto *actions = new QHBoxLayout; actions->setSpacing(10);
    auto *create = new QPushButton(Theme::icon("new"), QStringLiteral("新建文档")); create->setObjectName("primary");
    create->setAccessibleName(QStringLiteral("新建文档"));
    auto *files = new QPushButton(Theme::icon("folder"), QStringLiteral("打开文件…")); files->setObjectName("welcomeOpenFiles");
    auto *folder = new QPushButton(Theme::icon("folder"), QStringLiteral("打开文件夹…")); folder->setObjectName("welcomeOpenFolder");
    for (auto *button : {create, files, folder}) { button->setCursor(Qt::PointingHandCursor); actions->addWidget(button); }
    actions->addStretch(); layout->addLayout(actions);
    connect(create, &QPushButton::clicked, this, &WelcomePage::newDocumentRequested);
    connect(files, &QPushButton::clicked, this, &WelcomePage::openFilesRequested);
    connect(folder, &QPushButton::clicked, this, &WelcomePage::openFolderRequested);
    auto *heading = new QLabel(QStringLiteral("最近打开 · 文件与文件夹")); heading->setObjectName("welcomeSubtitle");
    layout->addSpacing(12); layout->addWidget(heading);
    m_history = new QListWidget; m_history->setObjectName("welcomeHistory");
    m_history->setTextElideMode(Qt::ElideMiddle); m_history->setMinimumHeight(210); m_history->setMaximumHeight(360);
    m_history->setAccessibleName(QStringLiteral("最近打开的文件与文件夹"));
    m_history->installEventFilter(this);
    layout->addWidget(m_history);
    m_empty = new QLabel(QStringLiteral("还没有历史记录。打开一个文件或文件夹，从这里继续上次的工作。"));
    m_empty->setObjectName("welcomeSubtitle"); m_empty->setWordWrap(true); layout->addWidget(m_empty);
    connect(m_history, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
        emit recentRequested(item->data(Qt::UserRole).toString(), item->data(Qt::UserRole + 1).toBool());
    });
    outer->addWidget(content); outer->addStretch(2);
    setHistory({});
}

void WelcomePage::setHistory(const QVector<RecentEntry> &entries)
{
    m_history->clear();
    for (const auto &entry : entries) {
        const bool folder = entry.kind == RecentKind::Folder;
        const QFileInfo info(entry.path);
        const bool exists = folder ? info.isDir() : info.isFile();
        const QString name = info.fileName().isEmpty() ? entry.path : info.fileName();
        const QString label = name + (folder ? QStringLiteral("  ·  文件夹") : QStringLiteral("  ·  文件"))
            + (exists ? QString() : QStringLiteral("  ·  已移动或删除")) + '\n' + QDir::toNativeSeparators(entry.path);
        auto *item = new QListWidgetItem(Theme::icon(folder ? "folder" : "file"), label, m_history);
        item->setSizeHint(QSize(0, 64)); item->setToolTip(entry.path);
        item->setData(Qt::UserRole, entry.path); item->setData(Qt::UserRole + 1, folder);
    }
    m_history->setVisible(!entries.isEmpty()); m_empty->setVisible(entries.isEmpty());
}

bool WelcomePage::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_history && event->type() == QEvent::KeyPress) {
        const int key = static_cast<QKeyEvent *>(event)->key();
        if (key == Qt::Key_Return || key == Qt::Key_Enter) {
            if (const auto *item = m_history->currentItem())
                emit recentRequested(item->data(Qt::UserRole).toString(), item->data(Qt::UserRole + 1).toBool());
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}
}
