#pragma once
#include "FileIO.h"
#include "Search.h"
#include "Animation.h"
#include "TreeSitter.h"
#include <QPlainTextEdit>
#include <QPointer>
#include <QJsonValue>
#include <QJsonObject>

class QListWidget;

namespace nya {
class Highlighter;
class Gutter;
class EditorBackground;
class Editor : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit Editor(QWidget *parent = nullptr);
    QString path;
    QString displayName;
    Encoding encoding;
    LineEnding ending = LineEnding::LF;
    QDateTime diskModified;
    qint64 diskSize = 0;
    bool mixedEndings = false;
    bool metadataModified = false;
    bool loading = false;
    bool largeFile = false;
    bool dirty() const;
    QString contents() const;
    void setLanguage(const QString &suffix);
    void setLanguageSettings(const LanguageSettings &settings);
    LanguageSettings languageSettings() const;
    TreeSitterStatus treeSitterStatus() const;
    void setDark(bool dark);
    void setBackground(EditorBackground *background, QWidget *surface = nullptr);
    EditorBackground *background() const;
    void setSearchMatches(const QVector<SearchMatch> &matches);
    void clearSearchMatches();
    int gutterWidth() const;
    void paintGutter(QPaintEvent *event);
    void setFontSize(int size);
    int fontSize() const;
    static QFont defaultEditorFont();
    static constexpr int MinimumFontSize = 6;
    static constexpr int MaximumFontSize = 96;
    void setEditorFont(const QFont &font);
    QFont editorFont() const { return m_editorFont; }
    void setAnimationSettings(const AnimationSettings &settings);
    AnimationSettings animationSettings() const { return m_animationSettings; }
    void requestCompletion() { emit completionRequested(false); }
    void requestDefinition() { emit definitionRequested(); }
    bool showCompletions(const QJsonValue &result);
    void hideCompletions();
    int lspOffset(const QJsonObject &position) const;
signals:
    void languageStatusChanged(const QString &status);
    void zoomChanged();
    void editorFontChanged();
    void completionRequested(bool automatic);
    void definitionRequested();
protected:
    bool event(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void inputMethodEvent(QInputMethodEvent *event) override;
private:
    void acceptCompletion();
    bool handleCompletionKey(QKeyEvent *event);
    void updateCompletionBackdrop();
    void updateAnimatedCursor(bool animate);
    void restartCursorBlink();
    void animateSelection();
    void updateSelectionPalette(qreal strength = 1.0);
    void paintBackground(QPainter &painter, QWidget *target, const QRect &dirtyRect);
    QRectF caretRect() const;
    void updateGutterWidth();
    void updateHighlights();
    void indentSelection(bool outdent);
    Gutter *m_gutter;
    Highlighter *m_highlighter;
    QPointer<EditorBackground> m_background;
    QPointer<QWidget> m_backgroundSurface;
    QVector<SearchMatch> m_matches;
    QFont m_editorFont;
    bool m_dark = true;
    AnimationSettings m_animationSettings;
    QVariantAnimation m_cursorMotion;
    QVariantAnimation m_cursorBlink;
    QVariantAnimation m_selectionFade;
    QRectF m_paintedCursor;
    QRectF m_targetCursor;
    qreal m_cursorOpacity = 1.0;
    QListWidget *m_completions = nullptr;
    QString m_completionText;
    int m_completionPosition = -1;
};
}
