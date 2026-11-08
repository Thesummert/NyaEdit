#include "MarkdownPreview.h"
#include "EditorBackground.h"
#include "MarkdownMath.h"
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QPainter>
#include <QPaintEvent>
#include <QSet>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextImageFormat>
#include <QWheelEvent>

namespace nya {
MarkdownPreview::MarkdownPreview(QWidget *parent) : QTextBrowser(parent)
{
    setFrameShape(QFrame::NoFrame);
    document()->setDocumentMargin(20);
    setOpenLinks(false);
    viewport()->setAutoFillBackground(false);
    connect(this, &QTextBrowser::anchorClicked, this, &MarkdownPreview::followLink);
}

void MarkdownPreview::setPreviewFont(const QFont &font)
{
    const bool changed = document()->defaultFont() != font;
    setFont(font);
    document()->setDefaultFont(font);
    if (changed && m_hasMarkdown) renderMarkdown(m_source, m_path);
}

void MarkdownPreview::setBackground(EditorBackground *background, QWidget *surface)
{
    if (m_background) disconnect(m_background, nullptr, this, nullptr);
    m_background = background;
    m_backgroundSurface = surface;
    if (background) connect(background, &EditorBackground::changed, this, [this] { viewport()->update(); });
    viewport()->update();
}

void MarkdownPreview::renderMarkdown(const QString &source, const QString &path)
{
    m_source = source;
    m_hasMarkdown = true;
    m_path = path;
    document()->setBaseUrl(path.isEmpty() ? QUrl()
        : QUrl::fromLocalFile(QFileInfo(path).absolutePath() + QDir::separator()));
    QList<MathExpression> expressions;
    const QString markdown = extractMarkdownMath(source, expressions);
    document()->setMarkdown(markdown, QTextDocument::MarkdownFeatures(QTextDocument::MarkdownDialectGitHub)
        | QTextDocument::MarkdownNoHTML);
    if (!expressions.isEmpty()) resetMarkdownMath();
    for (const auto &expression : expressions) {
        QTextCursor cursor = document()->find(expression.token);
        if (cursor.isNull()) continue;
        const QImage image = renderMarkdownMath(expression.source, document()->defaultFont(), palette().color(QPalette::Text), expression.display);
        if (image.isNull()) {
            cursor.insertText(expression.original);
            continue;
        }
        const QUrl url(QStringLiteral("nya-math:") + expression.token);
        document()->addResource(QTextDocument::ImageResource, url, image);
        QTextImageFormat format;
        format.setName(url.toString());
        format.setWidth(image.width() / image.devicePixelRatio());
        format.setHeight(image.height() / image.devicePixelRatio());
        format.setToolTip(expression.source);
        cursor.insertImage(format);
        if (expression.display && cursor.block().text().trimmed() == QString(QChar::ObjectReplacementCharacter)) {
            auto blockFormat = cursor.blockFormat();
            blockFormat.setAlignment(Qt::AlignHCenter);
            cursor.setBlockFormat(blockFormat);
        }
    }
    // Qt renders headings without the fragment targets used by Markdown tables of contents.
    QSet<QString> anchors;
    QHash<QString, int> suffixes;
    for (auto block = document()->begin(); block.isValid(); block = block.next()) {
        if (!block.blockFormat().headingLevel() || block.text().isEmpty()) continue;
        QString slug;
        for (const QChar ch : block.text().toLower()) {
            if (ch.isLetterOrNumber() || ch.category() == QChar::Mark_NonSpacing
                || ch == QLatin1Char('-') || ch == QLatin1Char('_')) slug += ch;
            else if (ch.isSpace()) slug += QLatin1Char('-');
        }
        QString anchor = slug;
        int &suffix = suffixes[slug];
        while (anchors.contains(anchor)) anchor = slug + QLatin1Char('-') + QString::number(++suffix);
        anchors.insert(anchor);
        QTextCursor cursor(block);
        cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
        auto format = cursor.charFormat();
        auto names = format.anchorNames(); names.append(anchor);
        format.setAnchor(true); format.setAnchorNames(names);
        cursor.setCharFormat(format);
    }
}

void MarkdownPreview::followLink(const QUrl &url)
{
    if (!url.isValid() || url.isEmpty()) return;
    if (url.isRelative() && url.path().isEmpty() && url.hasFragment()) {
        scrollToAnchor(url.fragment(QUrl::FullyDecoded)); return;
    }
    const QUrl resolved = document()->baseUrl().resolved(url);
    if (resolved.isLocalFile()) {
        if (!m_path.isEmpty() && QFileInfo(resolved.toLocalFile()) == QFileInfo(m_path)) {
            if (resolved.hasFragment()) scrollToAnchor(resolved.fragment(QUrl::FullyDecoded));
            else verticalScrollBar()->setValue(0);
        } else emit localLinkClicked(resolved);
    } else if (resolved.scheme() == "http" || resolved.scheme() == "https" || resolved.scheme() == "mailto") {
        QDesktopServices::openUrl(resolved);
    }
}

void MarkdownPreview::paintEvent(QPaintEvent *event)
{
    {
        QPainter painter(viewport());
        painter.fillRect(event->rect(), palette().brush(QPalette::Base));
        if (m_background) {
            QWidget *surface = m_backgroundSurface;
            const QRect area = surface && surface->window() == window()
                ? surface->rect().translated(-viewport()->mapTo(surface, QPoint())) : viewport()->rect();
            m_background->paint(painter, area);
        }
    }
    QTextBrowser::paintEvent(event);
}

void MarkdownPreview::scrollContentsBy(int dx, int dy)
{
    QTextBrowser::scrollContentsBy(dx, dy);
    if (m_background && m_background->isActive()) viewport()->update();
}

void MarkdownPreview::wheelEvent(QWheelEvent *event)
{
    if (event->modifiers() & Qt::ControlModifier) {
        if (event->angleDelta().y()) emit zoomRequested(event->angleDelta().y() > 0 ? 1 : -1);
        event->accept(); return;
    }
    QTextBrowser::wheelEvent(event);
}
}
