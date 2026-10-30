#include "Theme.h"
#include <QApplication>
#include <QEvent>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QStyleFactory>
#include <QSvgRenderer>

// Resource initialization must be outside namespace nya for a static-library build.
static void initializeIconResources()
{
    Q_INIT_RESOURCE(icons);
}

namespace nya {
namespace {
class MenuSurfaceStyle : public QObject {
public:
    explicit MenuSurfaceStyle(QApplication &app) : QObject(&app)
    {
        setObjectName("nyaMenuSurfaceStyle"); app.installEventFilter(this);
    }
protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::Polish) {
            if (auto *menu = qobject_cast<QMenu *>(watched)) {
                // Apply before the popup's native window is created, including
                // standard edit menus created internally by Qt.
                menu->setWindowFlag(Qt::FramelessWindowHint);
                menu->setAttribute(Qt::WA_TranslucentBackground);
                menu->setAutoFillBackground(false);
            }
        }
        return QObject::eventFilter(watched, event);
    }
};
}

QIcon Theme::icon(const QString &name, const QColor &color)
{
    QPixmap pixmap(40, 40);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(2, 2);
    p.setPen(QPen(color, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    if (name == "new") {
        p.drawRoundedRect(QRectF(4, 2, 12, 16), 2, 2);
        p.drawLine(10, 7, 10, 13); p.drawLine(7, 10, 13, 10);
    } else if (name == "folder") {
        QPainterPath path;
        path.moveTo(2, 6); path.lineTo(2, 4); path.lineTo(8, 4); path.lineTo(10, 6);
        path.lineTo(18, 6); path.lineTo(18, 16); path.lineTo(2, 16); path.closeSubpath();
        p.drawPath(path);
    } else if (name == "save") {
        p.drawRoundedRect(QRectF(3, 2, 14, 16), 2, 2);
        p.drawRect(6, 2, 7, 5); p.drawRect(6, 11, 8, 7);
    } else if (name == "search") {
        p.drawEllipse(QRectF(3, 2, 10, 10)); p.drawLine(12, 11, 17, 16);
    } else if (name == "sun") {
        p.drawEllipse(QRectF(6, 6, 8, 8));
        for (int i = 0; i < 8; ++i) { p.drawLine(10, 1, 10, 3); p.translate(10, 10); p.rotate(45); p.translate(-10, -10); }
    } else if (name == "sidebar") {
        p.drawRoundedRect(QRectF(2, 3, 16, 14), 2, 2); p.drawLine(7, 3, 7, 17);
    } else if (name == "file") {
        p.drawRoundedRect(QRectF(4, 2, 12, 16), 2, 2);
        p.drawLine(7, 7, 13, 7); p.drawLine(7, 10, 13, 10); p.drawLine(7, 13, 11, 13);
    } else if (name == "undo" || name == "redo") {
        if (name == "redo") { p.translate(20, 0); p.scale(-1, 1); }
        p.drawLine(7, 4, 3, 8); p.drawLine(3, 8, 7, 12);
        p.drawArc(QRectF(3, 8, 14, 9), -170 * 16, 350 * 16);
    }
    QIcon icon;
    pixmap.setDevicePixelRatio(2);
    icon.addPixmap(pixmap);
    return icon;
}

QIcon Theme::appIcon()
{
    static const QIcon icon = [] {
        initializeIconResources();
        QSvgRenderer renderer(QStringLiteral(":/nyaedit/logo.svg"));
        QIcon result;
        for (int size : {16, 24, 32, 48, 64, 128, 256, 512, 1024}) {
            QPixmap pixmap(size, size);
            pixmap.fill(Qt::transparent);
            QPainter painter(&pixmap);
            painter.setRenderHint(QPainter::Antialiasing);
            renderer.render(&painter);
            painter.end();
            result.addPixmap(pixmap);
        }
        return result;
    }();
    return icon;
}

void Theme::apply(QApplication &app, bool dark)
{
    if (!app.findChild<QObject *>("nyaMenuSurfaceStyle", Qt::FindDirectChildrenOnly)) new MenuSurfaceStyle(app);
    app.setStyle(QStyleFactory::create("Fusion"));
    const QString bg = dark ? "#141820" : "#f5f7fa";
    const QString panel = dark ? "#1b202a" : "#ffffff";
    const QString raised = dark ? "#232a36" : "#e9eef4";
    const QString border = dark ? "#2c3442" : "#d8e0ea";
    const QString fg = dark ? "#dce4ef" : "#243145";
    const QString muted = dark ? "#8492a6" : "#65758a";
    const QString accent = dark ? "#67e8c2" : "#087b65";
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(bg));
    palette.setColor(QPalette::WindowText, QColor(fg));
    palette.setColor(QPalette::Base, QColor(bg));
    palette.setColor(QPalette::AlternateBase, QColor(panel));
    palette.setColor(QPalette::Text, QColor(fg));
    palette.setColor(QPalette::Button, QColor(panel));
    palette.setColor(QPalette::ButtonText, QColor(fg));
    const QString selection = dark ? "#307baa" : "#176aa0";
    palette.setColor(QPalette::Highlight, QColor(selection));
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::ToolTipBase, QColor(panel));
    palette.setColor(QPalette::ToolTipText, QColor(fg));
    palette.setColor(QPalette::Link, QColor(accent));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor(muted));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(muted));
    app.setPalette(palette);
    QFont uiFont = app.font();
    uiFont.setPixelSize(13);
    app.setFont(uiFont);
    app.setStyleSheet(QStringLiteral(R"(
        QMainWindow { background: %1; }
        QWidget#welcomePage { background: %1; }
        QLabel#welcomeTitle { font-size: 28px; font-weight: 700; color: %5; }
        QLabel#welcomeSubtitle { color: %6; }
        QListWidget#welcomeHistory { border: 1px solid %4; border-radius: 8px; }
        QMenuBar { background: %2; padding: 3px 10px; }
        QMenuBar::item { padding: 5px 10px; border-radius: 5px; }
        QMenuBar::item:selected, QMenu::item:selected { background: %3; }
        QMenu { background: %2; border: 1px solid %4; border-radius: 10px; padding: 6px; }
        QMenu::item { padding: 7px 30px 7px 12px; border-radius: 6px; }
        QMenu::item:disabled { color: %6; }
        QMenu::separator { height: 1px; background: %4; margin: 5px; }
        QToolBar { background: %2; border: none; border-bottom: 1px solid %4; spacing: 6px; padding: 10px 14px; }
        QToolBar::separator { background: %4; width: 1px; margin: 4px 8px; }
        QToolButton, QPushButton { background: transparent; color: %5; border: 1px solid transparent; border-radius: 6px; padding: 6px 10px; }
        QToolButton:hover, QPushButton:hover { background: %3; }
        QToolButton:checked, QPushButton:checked { background: %3; color: %7; border-color: %4; }
        QPushButton#primary { background: %7; color: %1; font-weight: 600; }
        QToolButton:disabled, QPushButton:disabled { color: %6; }
        QTabWidget::pane { border: none; }
        QTabWidget { background: %1; }
        QTabBar { background: %2; }
        QTabBar::tab { background: %2; color: %6; padding: 13px 16px; border-right: 1px solid %4; border-bottom: 2px solid transparent; min-width: 112px; }
        QTabBar::tab:selected { background: %1; color: %5; border-bottom: 2px solid %7; }
        QTabBar::tab:hover { color: %5; }
        QPlainTextEdit { background: %1; border: none; padding: 8px 0; }
        QPlainTextEdit#editor { background: transparent; }
        QPlainTextEdit#fontPreview { border: 1px solid %4; border-radius: 6px; padding: 10px; }
        QDockWidget { color: %5; }
        QDockWidget::title { background: %2; padding: 6px 12px; }
        QWidget#taskPanel { background: %1; }
        QPlainTextEdit#taskOutput { padding: 4px; selection-background-color: %8; selection-color: white; }
        QFrame#sidebar { background: %2; border-right: 1px solid %4; }
        QLabel#sectionLabel { color: %6; font-size: 11px; font-weight: 600; padding: 14px 14px 6px; }
        QLabel#brand { color: %5; font-size: 19px; font-weight: 700; padding-right: 12px; }
        QLabel#tagline { color: %6; font-size: 12px; }
        QLabel#hint, QLabel#fileSearchStatus { color: %6; padding: 12px 14px; }
        QLabel#fileSearchHint { color: %6; font-size: 11px; padding: 0px 12px 6px; }
        QListWidget, QTreeView { background: %2; border: none; outline: none; }
        QListWidget::item { padding: 8px 12px; border-radius: 4px; }
        QListWidget::item:selected, QTreeView::item:selected { background: %3; color: %7; }
        QTreeView::item { padding: 4px; }
        QSplitter::handle { background: %4; width: 1px; }
        QFrame#searchPanel { background: %2; border-bottom: 1px solid %4; }
        QLineEdit { background: %1; border: 1px solid %4; border-radius: 5px; padding: 7px 10px; selection-background-color: %8; selection-color: white; }
        QLineEdit:focus { border: 1px solid %7; }
        QStatusBar { background: %2; border-top: 1px solid %4; color: %6; padding-left: 12px; }
        QStatusBar::item { border: none; }
        QStatusBar QToolButton { padding: 4px 9px; color: %6; font-size: 12px; }
        QScrollBar:vertical { background: transparent; width: 10px; margin: 1px; }
        QScrollBar::handle:vertical { background: %4; min-height: 30px; border-radius: 4px; }
        QScrollBar:horizontal { background: transparent; height: 10px; margin: 1px; }
        QScrollBar::handle:horizontal { background: %4; min-width: 30px; border-radius: 4px; }
        QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
        QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
        QToolTip { background: %2; color: %5; border: 1px solid %4; padding: 5px; }
    )").arg(bg, panel, raised, border, fg, muted, accent, selection));
}
}
