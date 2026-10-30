#include "Editor.h"
#include <QAction>
#include <QContextMenuEvent>
#include <QGuiApplication>
#include <QGraphicsBlurEffect>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QInputMethodEvent>
#include <QJsonArray>
#include <QKeyEvent>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QTextBlock>
#include <algorithm>

namespace nya {
namespace {
class CompletionPopup : public QListWidget {
public:
    explicit CompletionPopup(QWidget *parent) : QListWidget(parent) {}

    void prepareBackdrop(QWidget *surface, const QRect &globalRect, bool dark)
    {
        // Capture only our own window, before showing the popup. Cache the blur
        // so selection changes never filter the text or repeat the expensive work.
        constexpr int padding = 96;
        const qreal ratio = surface->devicePixelRatioF();
        const QColor base(dark ? "#28364a" : "#ffffff");
        const QRect region(surface->mapFromGlobal(globalRect.topLeft() - QPoint(padding, padding)),
                           globalRect.size() + QSize(2 * padding, 2 * padding));
        QPixmap capture(region.size() * ratio);
        capture.setDevicePixelRatio(ratio); capture.fill(base);
        const auto clipped = region.intersected(surface->rect());
        if (!clipped.isEmpty()) {
            const auto snapshot = surface->grab(clipped);
            QPainter painter(&capture); painter.drawPixmap(clipped.topLeft() - region.topLeft(), snapshot);
        }
        QGraphicsScene scene;
        auto *item = scene.addPixmap(capture);
        auto *blur = new QGraphicsBlurEffect;
        blur->setBlurRadius(48 * ratio); blur->setBlurHints(QGraphicsBlurEffect::QualityHint);
        item->setGraphicsEffect(blur);
        m_backdrop = QPixmap(globalRect.size() * ratio);
        m_backdrop.setDevicePixelRatio(ratio); m_backdrop.fill(base);
        QPainter painter(&m_backdrop);
        scene.render(&painter, QRectF(QPointF(), globalRect.size()),
                     QRectF(QPointF(padding, padding), globalRect.size()));
        auto tint = base; tint.setAlpha(240); // 94% tint over the heavily blurred backdrop.
        painter.fillRect(QRect(QPoint(), globalRect.size()), tint);
    }
protected:
    void paintEvent(QPaintEvent *event) override
    {
        {
            QPainter painter(viewport());
            painter.drawPixmap(-viewport()->pos(), m_backdrop);
        }
        QListWidget::paintEvent(event);
    }
private:
    QPixmap m_backdrop;
};

bool identifierCharacter(QChar c) { return c.isLetterOrNumber() || c == '_'; }
int wordStart(const QTextCursor &cursor)
{
    const auto line = cursor.block().text();
    int column = cursor.positionInBlock();
    while (column > 0 && identifierCharacter(line.at(column - 1))) --column;
    return cursor.block().position() + column;
}
bool matchesPrefix(const QString &text, const QString &prefix)
{
    int matched = 0;
    for (const auto c : text) {
        if (matched < prefix.size() && c.toCaseFolded() == prefix.at(matched).toCaseFolded()) ++matched;
    }
    return matched == prefix.size();
}
}

int Editor::lspOffset(const QJsonObject &position) const
{
    const auto lineValue = position.value("line"), characterValue = position.value("character");
    const auto line = lineValue.toInt(-1), character = characterValue.toInt(-1);
    if (line < 0 || character < 0 || lineValue.toDouble(-1) != line || characterValue.toDouble(-1) != character) return -1;
    const auto block = document()->findBlockByNumber(line);
    if (!block.isValid() || character > block.length() - 1) return -1;
    const auto text = block.text();
    if (character > 0 && character < text.size() && text.at(character).isLowSurrogate() && text.at(character - 1).isHighSurrogate()) return -1;
    return block.position() + character;
}

void Editor::hideCompletions()
{
    if (m_completions) m_completions->hide();
}

void Editor::updateCompletionBackdrop()
{
    if (!m_completions) return;
    static_cast<CompletionPopup *>(m_completions)->prepareBackdrop(window(), m_completions->geometry(), m_dark);
    m_completions->viewport()->update();
}

bool Editor::showCompletions(const QJsonValue &result)
{
    hideCompletions();
    if (isReadOnly() || textCursor().hasSelection()) return false;
    if (!m_completions) {
        m_completions = new CompletionPopup(this);
        m_completions->setObjectName("lspCompletionPopup");
        m_completions->setWindowFlags(Qt::ToolTip | Qt::FramelessWindowHint);
        m_completions->setAttribute(Qt::WA_ShowWithoutActivating);
        m_completions->setFocusPolicy(Qt::NoFocus);
        m_completions->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        connect(m_completions, &QListWidget::itemClicked, this, [this] { acceptCompletion(); });
        connect(this, &QPlainTextEdit::textChanged, this, [this] {
            // Rehighlighting emits textChanged too, without editing the source.
            if (m_completions->isVisible() && contents() != m_completionText) hideCompletions();
        });
        connect(this, &QPlainTextEdit::cursorPositionChanged, this, &Editor::hideCompletions);
    }
    m_completions->clear(); m_completions->setFont(editorFont());
    m_completionText = contents(); m_completionPosition = textCursor().position();
    const auto cursor = textCursor();
    const auto prefix = cursor.block().text().mid(wordStart(cursor) - cursor.block().position(), cursor.position() - wordStart(cursor));
    const auto items = result.isArray() ? result.toArray() : result.toObject().value("items").toArray();
    QVector<QJsonObject> candidates;
    for (const auto &value : items) {
        const auto item = value.toObject();
        if (item.value("label").toString().isEmpty() || item.value("insertTextFormat").toInt(1) != 1) continue;
        if (!matchesPrefix(item.value("filterText").toString(item.value("label").toString()), prefix)) continue;
        candidates.append(item);
    }
    std::stable_sort(candidates.begin(), candidates.end(), [](const QJsonObject &a, const QJsonObject &b) {
        return a.value("sortText").toString(a.value("label").toString()) < b.value("sortText").toString(b.value("label").toString());
    });
    for (int i = 0; i < qMin(200, int(candidates.size())); ++i) {
        const auto &item = candidates.at(i);
        const auto detail = item.value("detail").toString();
        auto *row = new QListWidgetItem(item.value("label").toString() + (detail.isEmpty() ? QString() : "  " + detail), m_completions);
        row->setData(Qt::UserRole, item); row->setToolTip(detail);
    }
    if (m_completions->count() == 0) return false;
    m_completions->setCurrentRow(0);
    const auto anchor = viewport()->mapToGlobal(cursorRect().bottomLeft());
    const auto screen = QGuiApplication::screenAt(anchor);
    const auto available = screen ? screen->availableGeometry() : QRect(anchor, QSize(600, 400));
    const int width = qMin(available.width(), qBound(260, m_completions->sizeHintForColumn(0) + 28, 620));
    const int height = qMin(available.height(), qMin(8, m_completions->count()) * (m_completions->sizeHintForRow(0) + 1) + 6);
    const int x = qBound(available.left(), anchor.x(), available.right() - width + 1);
    int y = anchor.y();
    if (y + height > available.bottom()) y = viewport()->mapToGlobal(cursorRect().topLeft()).y() - height;
    y = qBound(available.top(), y, available.bottom() - height + 1);
    m_completions->setGeometry(x, y, width, height);
    updateCompletionBackdrop(); m_completions->show();
    return true;
}

bool Editor::handleCompletionKey(QKeyEvent *event)
{
    if (!m_completions || !m_completions->isVisible()) return false;
    switch (event->key()) {
    case Qt::Key_Escape: hideCompletions(); event->accept(); return true;
    case Qt::Key_Up:
    case Qt::Key_Down:
    case Qt::Key_PageUp:
    case Qt::Key_PageDown: {
        const int step = event->key() == Qt::Key_Up ? -1 : event->key() == Qt::Key_Down ? 1 : event->key() == Qt::Key_PageUp ? -8 : 8;
        m_completions->setCurrentRow(qBound(0, m_completions->currentRow() + step, m_completions->count() - 1));
        event->accept(); return true;
    }
    case Qt::Key_Tab:
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (event->modifiers() == Qt::NoModifier) { acceptCompletion(); event->accept(); return true; }
        break;
    default: break;
    }
    hideCompletions();
    return false;
}

bool Editor::event(QEvent *event)
{
    if (event->type() == QEvent::ShortcutOverride && m_completions && m_completions->isVisible()) {
        const auto key = static_cast<QKeyEvent *>(event)->key();
        if (key == Qt::Key_Escape || key == Qt::Key_Tab || key == Qt::Key_Return || key == Qt::Key_Enter
            || key == Qt::Key_Up || key == Qt::Key_Down || key == Qt::Key_PageUp || key == Qt::Key_PageDown) {
            event->accept(); return true;
        }
    }
    return QPlainTextEdit::event(event);
}

void Editor::acceptCompletion()
{
    if (!m_completions || !m_completions->currentItem()) return;
    const auto item = m_completions->currentItem()->data(Qt::UserRole).toJsonObject();
    hideCompletions();
    if (isReadOnly() || loading || contents() != m_completionText
        || textCursor().position() != m_completionPosition || textCursor().hasSelection()) return;
    struct Edit { int start; int end; QString text; };
    QVector<Edit> edits;
    const auto textEdit = item.value("textEdit").toObject();
    int start = wordStart(textCursor()), end = textCursor().position();
    const auto line = textCursor().block().text();
    int column = textCursor().positionInBlock();
    while (column < line.size() && identifierCharacter(line.at(column))) { ++column; ++end; }
    QString text = item.value("insertText").toString(item.value("label").toString());
    if (!textEdit.isEmpty()) {
        const auto range = textEdit.value("range").toObject(textEdit.value("replace").toObject());
        start = lspOffset(range.value("start").toObject()); end = lspOffset(range.value("end").toObject());
        if (!textEdit.value("newText").isString() || start < 0 || end < start || start > m_completionPosition || end < m_completionPosition) return;
        text = textEdit.value("newText").toString();
    }
    edits.append({start, end, text});
    const auto additional = item.value("additionalTextEdits").toArray();
    if (additional.size() > 256) return;
    int finalPosition = start + int(text.size());
    for (const auto &value : additional) {
        const auto edit = value.toObject(), range = edit.value("range").toObject();
        const auto from = lspOffset(range.value("start").toObject()), to = lspOffset(range.value("end").toObject());
        if (from < 0 || to < from || !edit.value("newText").isString()) return;
        const auto newText = edit.value("newText").toString();
        edits.append({from, to, newText});
        if (to <= start) finalPosition += int(newText.size()) - (to - from);
    }
    std::sort(edits.begin(), edits.end(), [](const Edit &a, const Edit &b) { return a.start > b.start; });
    for (int i = 1; i < edits.size(); ++i)
        if (edits.at(i).end > edits.at(i - 1).start || edits.at(i).start == edits.at(i - 1).start) return;
    auto cursor = textCursor(); cursor.beginEditBlock();
    for (const auto &edit : edits) {
        cursor.setPosition(edit.start); cursor.setPosition(edit.end, QTextCursor::KeepAnchor); cursor.insertText(edit.text);
    }
    cursor.endEditBlock(); cursor.setPosition(finalPosition); setTextCursor(cursor); ensureCursorVisible();
}

void Editor::contextMenuEvent(QContextMenuEvent *event)
{
    auto *menu = createStandardContextMenu(); menu->addSeparator();
    auto *complete = menu->addAction(QStringLiteral("代码补全\tAlt+/"), this, &Editor::requestCompletion);
    auto *definition = menu->addAction(QStringLiteral("转到定义\tF12"), this, &Editor::requestDefinition);
    complete->setEnabled(!isReadOnly() && !path.isEmpty()); definition->setEnabled(!loading && !path.isEmpty());
    menu->exec(event->globalPos()); delete menu;
}

void Editor::mousePressEvent(QMouseEvent *event)
{
    hideCompletions();
    if (event->button() == Qt::LeftButton && event->modifiers() == Qt::ControlModifier && !loading) {
        setTextCursor(cursorForPosition(event->pos())); emit definitionRequested(); event->accept(); return;
    }
    QPlainTextEdit::mousePressEvent(event);
}

void Editor::inputMethodEvent(QInputMethodEvent *event)
{
    QPlainTextEdit::inputMethodEvent(event);
    if (!event->commitString().isEmpty()) emit completionRequested(true);
}
}
