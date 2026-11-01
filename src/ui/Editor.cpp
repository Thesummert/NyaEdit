#include "Editor.h"
#include "EditorBackground.h"
#include "Highlighter.h"
#include <QFontDatabase>
#include <QApplication>
#include <QDesktopServices>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QListWidget>
#include <QPainter>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextBrowser>
#include <QUrl>
#include <QWheelEvent>
#include <algorithm>

namespace nya {
class Gutter : public QWidget {
public:
    explicit Gutter(Editor *editor) : QWidget(editor), m_editor(editor) { }
    QSize sizeHint() const override { return {m_editor->gutterWidth(), 0}; }
    void paintEvent(QPaintEvent *event) override { m_editor->paintGutter(event); }
private:
    Editor *m_editor;
};

QFont Editor::defaultEditorFont()
{
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
#ifdef Q_OS_MAC
    font.setFamily(QStringLiteral("Menlo"));
#elif defined(Q_OS_WIN)
    font.setFamily(QStringLiteral("Consolas"));
#endif
    font.setPointSize(13);
    return font;
}

Editor::Editor(QWidget *parent) : QPlainTextEdit(parent), m_gutter(new Gutter(this)),
    m_highlighter(new Highlighter(document()))
{
    setObjectName("editor");
    viewport()->setAutoFillBackground(false);
    m_editorFont = defaultEditorFont();
    connect(m_highlighter, &Highlighter::languageStatusChanged, this, &Editor::languageStatusChanged);
    setFont(m_editorFont);
    setTabStopDistance(QFontMetricsF(this->font(), viewport()).horizontalAdvance(' ') * 4);
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setFrameShape(QFrame::NoFrame);
    setCenterOnScroll(false);
    setPlaceholderText(QStringLiteral("在这里开始书写…\n\n打开文件  Ctrl/⌘ O     ·     保存  Ctrl/⌘ S     ·     查找  Ctrl/⌘ F"));
    m_cursorMotion.setParent(this); m_cursorMotion.setObjectName("cursorMotion");
    m_cursorMotion.setEasingCurve(QEasingCurve::OutCubic);
    connect(&m_cursorMotion, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        const QRectF old = m_paintedCursor; m_paintedCursor = value.toRectF();
        viewport()->update(old.united(m_paintedCursor).adjusted(-2, -2, 2, 2).toAlignedRect());
    });
    m_cursorBlink.setParent(this); m_cursorBlink.setObjectName("cursorBlink");
    m_cursorBlink.setLoopCount(-1); m_cursorBlink.setStartValue(1.0); m_cursorBlink.setKeyValueAt(0.35, 1.0);
    m_cursorBlink.setKeyValueAt(0.55, 0.0); m_cursorBlink.setKeyValueAt(0.8, 0.0); m_cursorBlink.setEndValue(1.0);
    m_cursorBlink.setEasingCurve(QEasingCurve::InOutSine);
    connect(&m_cursorBlink, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        m_cursorOpacity = value.toReal(); viewport()->update(m_paintedCursor.adjusted(-2, -2, 2, 2).toAlignedRect());
    });
    m_selectionFade.setParent(this); m_selectionFade.setObjectName("selectionFade");
    m_selectionFade.setEasingCurve(QEasingCurve::OutCubic);
    connect(&m_selectionFade, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) { updateSelectionPalette(value.toReal()); });
    connect(this, &QPlainTextEdit::blockCountChanged, this, [this] { updateGutterWidth(); });
    connect(this, &QPlainTextEdit::updateRequest, this, [this](const QRect &rect, int dy) {
        if (dy && m_background && m_background->isActive()) {
            // Text scrolling must not shift the fixed background picture.
            viewport()->update(); m_gutter->update();
        } else if (dy) m_gutter->scroll(0, dy);
        else m_gutter->update(0, rect.y(), m_gutter->width(), rect.height());
        if (rect.contains(viewport()->rect())) updateGutterWidth();
        if (dy) updateAnimatedCursor(false);
    });
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, &Editor::updateHighlights);
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, [this] { updateAnimatedCursor(true); restartCursorBlink(); });
    connect(this, &QPlainTextEdit::selectionChanged, this, &Editor::animateSelection);
    connect(this, &QPlainTextEdit::textChanged, this, &Editor::clearSearchMatches);
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this] { updateHighlights(); updateAnimatedCursor(false); });
    connect(horizontalScrollBar(), &QScrollBar::valueChanged, this, [this] { updateAnimatedCursor(false); });
    setAnimationSettings(m_animationSettings);
    updateGutterWidth(); updateHighlights();
}

bool Editor::dirty() const { return metadataModified || document()->isModified(); }
QString Editor::contents() const
{
    // toPlainText() replaces non-breaking spaces with ASCII spaces. Preserve them on save/search.
    QString text = document()->toRawText();
    text.replace(QChar::ParagraphSeparator, QChar('\n'));
    text.replace(QChar::LineSeparator, QChar('\n'));
    return text;
}
void Editor::setLanguage(const QString &suffix)
{
    const auto ext = suffix.toLower();
    const bool wasMarkdown = m_markdown;
    m_markdown = ext == "md" || ext == "markdown";
    if (!m_markdown && isMarkdownPreviewVisible()) setMarkdownPreviewVisible(false);
    m_highlighter->setLanguage(largeFile ? QString() : suffix);
    if (wasMarkdown != m_markdown) emit markdownStateChanged();
}
bool Editor::isMarkdownPreviewVisible() const
{
    return m_markdownPreview && !m_markdownPreview->isHidden();
}
void Editor::setMarkdownPreviewVisible(bool visible)
{
    if (visible && (!m_markdown || loading)) return;
    if (visible == isMarkdownPreviewVisible()) return;
    if (visible && !m_markdownPreview) {
        // Cover the source widget without replacing its document or undo history.
        m_markdownPreview = new QTextBrowser(this);
        m_markdownPreview->setObjectName("inlineMarkdownPreview");
        m_markdownPreview->setFont(qApp->font());
        m_markdownPreview->setFrameShape(QFrame::NoFrame);
        m_markdownPreview->document()->setDocumentMargin(20);
        m_markdownPreview->setOpenLinks(false);
        connect(m_markdownPreview, &QTextBrowser::anchorClicked, this, [this](const QUrl &url) {
            if (url.isRelative() && url.path().isEmpty() && url.hasFragment())
                m_markdownPreview->scrollToAnchor(url.fragment());
            else if (url.scheme() == "http" || url.scheme() == "https" || url.scheme() == "mailto")
                QDesktopServices::openUrl(url);
        });
    }
    setReadOnly(visible || loading);
    hideCompletions();
    setFocusProxy(visible ? m_markdownPreview : nullptr);
    if (visible) {
        m_markdownPreview->setGeometry(rect());
        m_markdownPreview->show(); m_markdownPreview->raise();
    } else m_markdownPreview->hide();
    setFocus();
    emit markdownStateChanged();
}
void Editor::setLanguageSettings(const LanguageSettings &settings) { m_highlighter->setLanguageSettings(settings); }
LanguageSettings Editor::languageSettings() const { return m_highlighter->languageSettings(); }
TreeSitterStatus Editor::treeSitterStatus() const { return m_highlighter->treeSitterStatus(); }
void Editor::setDark(bool dark)
{
    m_dark = dark;
    // Reapplying an application stylesheet can restore a cached widget font.
    setFont(m_editorFont);
    setTabStopDistance(QFontMetricsF(this->font(), viewport()).horizontalAdvance(' ') * 4);
    updateGutterWidth();
    m_highlighter->setDark(dark); m_gutter->update(); updateHighlights();
    m_selectionFade.stop(); updateSelectionPalette(); updateAnimatedCursor(false);
    if (m_completions && m_completions->isVisible()) updateCompletionBackdrop();
}
void Editor::setSearchMatches(const QVector<SearchMatch> &matches) { m_matches = matches; updateHighlights(); }
EditorBackground *Editor::background() const { return m_background.data(); }
void Editor::setBackground(EditorBackground *background, QWidget *surface)
{
    if (m_background) disconnect(m_background, nullptr, this, nullptr);
    m_background = background;
    m_backgroundSurface = surface;
    const auto refresh = [this] { updateHighlights(); viewport()->update(); m_gutter->update(); };
    if (background) connect(background, &EditorBackground::changed, this, refresh);
    refresh();
}
void Editor::clearSearchMatches() { m_matches.clear(); updateHighlights(); }
int Editor::fontSize() const { return m_editorFont.pointSize(); }
void Editor::setFontSize(int size)
{
    auto font = m_editorFont;
    font.setPointSize(qBound(MinimumFontSize, size, MaximumFontSize));
    setEditorFont(font);
}
void Editor::setEditorFont(const QFont &font)
{
    QFont normalized = font;
    normalized.setPointSize(qBound(MinimumFontSize, font.pointSize(), MaximumFontSize));
    if (m_editorFont == normalized) return;
    const bool sizeChanged = normalized.pointSize() != fontSize();
    m_editorFont = normalized; setFont(m_editorFont);
    setTabStopDistance(QFontMetricsF(this->font(), viewport()).horizontalAdvance(' ') * 4);
    updateGutterWidth(); m_gutter->update(); viewport()->update();
    updateAnimatedCursor(false);
    emit editorFontChanged();
    if (sizeChanged) emit zoomChanged();
}
int Editor::gutterWidth() const
{
    const int digits = QString::number(qMax(1, blockCount())).size();
    return 24 + int(fontMetrics().horizontalAdvance('9')) * qMax(3, digits);
}
void Editor::updateGutterWidth()
{
    setViewportMargins(gutterWidth(), 0, 0, 0);
    m_gutter->setGeometry(QRect(contentsRect().left(), contentsRect().top(), gutterWidth(), contentsRect().height()));
}
void Editor::setAnimationSettings(const AnimationSettings &settings)
{
    m_animationSettings = settings;
    m_cursorMotion.stop(); m_cursorBlink.stop(); m_selectionFade.stop();
    setCursorWidth(settings.cursorEnabled ? 0 : 1);
    updateSelectionPalette(); updateAnimatedCursor(false); restartCursorBlink(); viewport()->update();
}

QRectF Editor::caretRect() const
{
    QRectF rect = cursorRect();
    rect.setWidth(overwriteMode() ? qMax(2.0, QFontMetricsF(font()).horizontalAdvance(QLatin1Char(' '))) : 2.0);
    return rect;
}

void Editor::updateAnimatedCursor(bool animate)
{
    const QRectF target = caretRect();
    m_cursorMotion.stop(); m_targetCursor = target;
    if (!animate || !m_animationSettings.cursorEnabled || !hasFocus() || !isVisible() ||
        m_paintedCursor.isEmpty() || !viewport()->rect().intersects(m_paintedCursor.toAlignedRect()) ||
        qAbs(target.top() - m_paintedCursor.top()) > viewport()->height() / 2) {
        m_paintedCursor = target; viewport()->update(); return;
    }
    m_cursorMotion.setDuration(m_animationSettings.duration(90));
    m_cursorMotion.setStartValue(m_paintedCursor); m_cursorMotion.setEndValue(target); m_cursorMotion.start();
}

void Editor::restartCursorBlink()
{
    m_cursorBlink.stop(); m_cursorOpacity = 1;
    if (m_animationSettings.cursorEnabled && hasFocus() && isVisible() && !isReadOnly() && !textCursor().hasSelection() && QApplication::cursorFlashTime() > 0) {
        m_cursorBlink.setDuration(m_animationSettings.duration(QApplication::cursorFlashTime())); m_cursorBlink.start();
    }
}

void Editor::updateSelectionPalette(qreal strength)
{
    const QColor highlight(m_dark ? "#307baa" : "#176aa0");
    const QColor background(m_dark ? "#141820" : "#f5f7fa");
    const auto mix = [strength](int bg, int fg) { return qRound(bg + (fg - bg) * strength); };
    const QColor color(mix(background.red(), highlight.red()), mix(background.green(), highlight.green()), mix(background.blue(), highlight.blue()));
    auto colors = palette();
    for (auto group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
        colors.setColor(group, QPalette::Base, Qt::transparent);
        colors.setColor(group, QPalette::Highlight, color); colors.setColor(group, QPalette::HighlightedText, Qt::white);
    }
    setPalette(colors); viewport()->update();
}

void Editor::animateSelection()
{
    m_selectionFade.stop(); restartCursorBlink();
    if (!m_animationSettings.selectionEnabled || !textCursor().hasSelection() || !isVisible()) { updateSelectionPalette(); return; }
    // Keep the selection visible throughout the fade, including continuous dragging.
    m_selectionFade.setDuration(m_animationSettings.duration(140));
    m_selectionFade.setStartValue(0.9); m_selectionFade.setEndValue(1.0); m_selectionFade.start();
}

void Editor::paintEvent(QPaintEvent *event)
{
    {
        QPainter painter(viewport());
        paintBackground(painter, viewport(), event->rect());
    }
    QPlainTextEdit::paintEvent(event);
    if (!m_animationSettings.cursorEnabled || !hasFocus() || isReadOnly() || textCursor().hasSelection()) return;
    if (m_targetCursor != caretRect()) updateAnimatedCursor(false);
    QPainter painter(viewport());
    QColor color(m_dark ? "#8df5d6" : "#087b65"); color.setAlphaF(m_cursorOpacity);
    painter.fillRect(m_paintedCursor, color);
}

void Editor::paintBackground(QPainter &painter, QWidget *target, const QRect &dirtyRect)
{
    painter.fillRect(dirtyRect, QColor(m_dark ? "#141820" : "#f5f7fa"));
    if (!m_background) return;
    // Every part of the window uses the same image coordinates, including the gutter.
    QWidget *surface = m_backgroundSurface ? m_backgroundSurface.data() : this;
    const QRect rect = (m_backgroundSurface ? surface->rect() : contentsRect())
        .translated(-target->mapTo(surface, QPoint()));
    m_background->paint(painter, rect);
}

void Editor::focusInEvent(QFocusEvent *event)
{
    QPlainTextEdit::focusInEvent(event); updateAnimatedCursor(false); restartCursorBlink();
}
void Editor::focusOutEvent(QFocusEvent *event)
{
    hideCompletions();
    m_cursorMotion.stop(); m_cursorBlink.stop(); m_selectionFade.stop(); updateSelectionPalette();
    QPlainTextEdit::focusOutEvent(event); viewport()->update();
}
void Editor::showEvent(QShowEvent *event)
{
    QPlainTextEdit::showEvent(event); updateAnimatedCursor(false); restartCursorBlink();
}
void Editor::hideEvent(QHideEvent *event)
{
    hideCompletions();
    m_cursorMotion.stop(); m_cursorBlink.stop(); m_selectionFade.stop(); updateSelectionPalette();
    QPlainTextEdit::hideEvent(event);
}
void Editor::resizeEvent(QResizeEvent *event)
{
    QPlainTextEdit::resizeEvent(event);
    if (m_markdownPreview) m_markdownPreview->setGeometry(rect());
    m_gutter->setGeometry(QRect(contentsRect().left(), contentsRect().top(), gutterWidth(), contentsRect().height()));
    updateAnimatedCursor(false);
}
void Editor::paintGutter(QPaintEvent *event)
{
    QPainter p(m_gutter);
    paintBackground(p, m_gutter, event->rect());
    p.setFont(font());
    QTextBlock block = firstVisibleBlock();
    int number = block.blockNumber();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    while (block.isValid() && top <= event->rect().bottom()) {
        const int height = qRound(blockBoundingRect(block).height());
        if (block.isVisible() && top + height >= event->rect().top()) {
            const bool current = number == textCursor().blockNumber();
            p.setPen(QColor(current ? (m_dark ? "#67e8c2" : "#087b65") : (m_dark ? "#536075" : "#95a0af")));
            p.drawText(0, top, m_gutter->width() - 14, fontMetrics().height(), Qt::AlignRight, QString::number(number + 1));
        }
        block = block.next(); top += height; ++number;
    }
}
void Editor::updateHighlights()
{
    QList<QTextEdit::ExtraSelection> selections;
    QTextEdit::ExtraSelection line;
    QColor lineColor(m_dark ? "#1b212c" : "#edf2f7");
    if (m_background && m_background->isActive()) lineColor.setAlpha(160);
    line.format.setBackground(lineColor);
    line.format.setProperty(QTextFormat::FullWidthSelection, true);
    line.cursor = textCursor(); line.cursor.clearSelection();
    selections.append(line);
    const int start = firstVisibleBlock().position();
    const int end = cursorForPosition(QPoint(viewport()->width(), viewport()->height())).position() + 1;
    auto it = std::lower_bound(m_matches.begin(), m_matches.end(), start,
                              [](const SearchMatch &match, int pos) { return match.start + match.length < pos; });
    for (int n = 0; it != m_matches.end() && it->start <= end && n < 1000; ++it, ++n) {
        if (it->length == 0) continue;
        QTextEdit::ExtraSelection match;
        match.format.setBackground(QColor(m_dark ? "#645332" : "#ffe5a3"));
        match.cursor = QTextCursor(document());
        match.cursor.setPosition(it->start);
        match.cursor.setPosition(it->start + it->length, QTextCursor::KeepAnchor);
        selections.append(match);
    }
    setExtraSelections(selections);
    m_gutter->update();
}
void Editor::indentSelection(bool outdent)
{
    QTextCursor selection = textCursor();
    const int start = selection.selectionStart(), end = selection.selectionEnd();
    QTextBlock first = document()->findBlock(start);
    QTextBlock last = document()->findBlock(end);
    if (end > start && last.position() == end) last = last.previous();
    QTextCursor edit(document());
    edit.beginEditBlock();
    for (QTextBlock block = first; block.isValid(); block = block.next()) {
        edit.setPosition(block.position());
        if (outdent) {
            const QString text = block.text();
            int count = 0;
            if (text.startsWith('\t')) count = 1;
            else while (count < qMin(4, int(text.size())) && text.at(count) == ' ') ++count;
            edit.setPosition(block.position() + count, QTextCursor::KeepAnchor); edit.removeSelectedText();
        } else edit.insertText(QStringLiteral("    "));
        if (block == last) break;
    }
    edit.endEditBlock();
    selection.setPosition(first.position());
    selection.setPosition(last.position() + int(last.length()) - 1, QTextCursor::KeepAnchor);
    setTextCursor(selection);
}
void Editor::keyPressEvent(QKeyEvent *event)
{
    if (handleCompletionKey(event)) return;
    if (isReadOnly()) { QPlainTextEdit::keyPressEvent(event); return; }
    if (event->key() == Qt::Key_Backtab || (event->key() == Qt::Key_Tab && textCursor().hasSelection())) {
        indentSelection(event->key() == Qt::Key_Backtab); return;
    }
    if (event->key() == Qt::Key_Tab && event->modifiers() == Qt::NoModifier) {
        auto cursor = textCursor(); cursor.insertText(QString(4 - cursor.positionInBlock() % 4, ' ')); setTextCursor(cursor); return;
    }
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && !textCursor().hasSelection()) {
        const auto cursor = textCursor();
        const QString before = cursor.block().text().left(cursor.positionInBlock());
        int spaces = 0;
        while (spaces < before.size() && (before.at(spaces) == ' ' || before.at(spaces) == '\t')) ++spaces;
        auto edit = cursor; edit.beginEditBlock(); edit.insertText("\n" + before.left(spaces)); edit.endEditBlock(); setTextCursor(edit); return;
    }
    QPlainTextEdit::keyPressEvent(event);
    if ((!event->text().isEmpty() && !event->text().at(0).isSpace()
         && !(event->modifiers() & (Qt::ControlModifier | Qt::MetaModifier | Qt::AltModifier)))
        || event->key() == Qt::Key_Backspace) emit completionRequested(true);
}
void Editor::wheelEvent(QWheelEvent *event)
{
    if (event->modifiers() & Qt::ControlModifier) {
        if (event->angleDelta().y() != 0) setFontSize(fontSize() + (event->angleDelta().y() > 0 ? 1 : -1));
        event->accept(); return;
    }
    QPlainTextEdit::wheelEvent(event);
}
}
