#pragma once
#include "Animation.h"
#include <QPersistentModelIndex>
#include <QPixmap>
#include <QPointer>
#include <QTreeView>

namespace nya {
class EditorBackground;
class FileTreeView : public QTreeView {
    Q_OBJECT
public:
    explicit FileTreeView(QWidget *parent = nullptr);
    void setAnimationSettings(const AnimationSettings &settings);
    void setBackground(EditorBackground *background, QWidget *surface);
    void setModel(QAbstractItemModel *model) override;
    void setRootIndex(const QModelIndex &index) override;
protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void changeEvent(QEvent *event) override;
    void scrollContentsBy(int dx, int dy) override;
private:
    void transition(const QModelIndex &index, bool expanding);
    void startTransition(qreal start = -1.0);
    void finishTransition();
    void invalidateFrame();
    void paintTransition(QPainter &painter);
    int descendantHeight(const QModelIndex &index, bool cached) const;
    int cachedRowBottom(const QModelIndex &index) const;
    QVariantAnimation m_transition;
    QPointer<EditorBackground> m_background;
    QPointer<QWidget> m_backgroundSurface;
    QPixmap m_logicalFrame;
    QPixmap m_expandedFrame;
    QPixmap m_collapsedFrame;
    QVector<QPair<QPersistentModelIndex, QRect>> m_visibleRows;
    QPersistentModelIndex m_transitionIndex;
    QPersistentModelIndex m_pendingExpansion;
    QMetaObject::Connection m_insertedConnection;
    QMetaObject::Connection m_resetConnection;
    QMetaObject::Connection m_removedConnection;
    int m_transitionTop = 0;
    int m_expandedTop = 0;
    int m_collapsedTop = 0;
    int m_childrenHeight = 0;
    int m_duration = 180;
    QSize m_viewSize;
    qreal m_startProgress = 0;
    bool m_waitingForFrame = false;
    bool m_animationsEnabled = true;
    bool m_expanding = true;
};
}
