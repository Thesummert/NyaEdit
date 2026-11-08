#pragma once
#include <QPointer>
#include <QTextBrowser>

namespace nya {
class EditorBackground;
class MarkdownPreview : public QTextBrowser {
    Q_OBJECT
public:
    explicit MarkdownPreview(QWidget *parent = nullptr);
    void setPreviewFont(const QFont &font);
    void setPlainText(const QString &text) { m_hasMarkdown = false; QTextBrowser::setPlainText(text); }
    void setBackground(EditorBackground *background, QWidget *surface = nullptr);
    void renderMarkdown(const QString &source, const QString &path);
signals:
    void localLinkClicked(const QUrl &url);
    void zoomRequested(int steps);
protected:
    void paintEvent(QPaintEvent *event) override;
    void scrollContentsBy(int dx, int dy) override;
    void wheelEvent(QWheelEvent *event) override;
private:
    void followLink(const QUrl &url);
    QPointer<EditorBackground> m_background;
    QPointer<QWidget> m_backgroundSurface;
    QString m_path;
    QString m_source;
    bool m_hasMarkdown = false;
};
}
