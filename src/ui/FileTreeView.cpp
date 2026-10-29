#include "FileTreeView.h"
#include <QEvent>
#include <QPainter>
#include <QPaintEvent>

namespace nya {
FileTreeView::FileTreeView(QWidget *parent) : QTreeView(parent)
{
    // Qt's built-in tree animation has a fixed duration. Keep expansion logical
    // and immediate, and animate the visible rows with our configured duration.
    setAnimated(false);
    m_transition.setParent(this); m_transition.setObjectName("treeTransition");
    m_transition.setEasingCurve(QEasingCurve::OutCubic);
    connect(&m_transition, &QVariantAnimation::valueChanged, this, [this] { viewport()->update(); });
    connect(&m_transition, &QVariantAnimation::finished, this, &FileTreeView::finishTransition);
    connect(this, &QTreeView::expanded, this, [this](const QModelIndex &index) { transition(index, true); });
    connect(this, &QTreeView::collapsed, this, [this](const QModelIndex &index) { transition(index, false); });
}

void FileTreeView::setAnimationSettings(const AnimationSettings &settings)
{
    finishTransition(); m_animationsEnabled = settings.interfaceEnabled; m_duration = settings.duration(180);
}

void FileTreeView::setModel(QAbstractItemModel *model)
{
    invalidateFrame();
    disconnect(m_insertedConnection); disconnect(m_resetConnection); disconnect(m_removedConnection);
    QTreeView::setModel(model);
    if (!model) return;
    m_insertedConnection = connect(model, &QAbstractItemModel::rowsInserted, this, [this](const QModelIndex &parent) {
        if (m_pendingExpansion.isValid() && parent == m_pendingExpansion && isExpanded(parent)) {
            m_pendingExpansion = QPersistentModelIndex(); startTransition();
        }
    });
    m_resetConnection = connect(model, &QAbstractItemModel::modelAboutToBeReset, this, &FileTreeView::invalidateFrame);
    m_removedConnection = connect(model, &QAbstractItemModel::rowsAboutToBeRemoved, this, &FileTreeView::invalidateFrame);
}

void FileTreeView::setRootIndex(const QModelIndex &index)
{
    invalidateFrame(); QTreeView::setRootIndex(index);
}

void FileTreeView::transition(const QModelIndex &index, bool expanding)
{
    const QRect row = visualRect(index);
    if (!m_animationsEnabled || !isVisible() || m_logicalFrame.isNull() || !viewport()->rect().intersects(row)) {
        finishTransition(); return;
    }
    if (m_transition.state() == QAbstractAnimation::Running && index == m_transitionIndex) {
        const qreal progress = m_transition.currentValue().toReal();
        m_transition.stop(); m_expanding = expanding;
        startTransition(progress); return;
    }
    m_transition.stop(); m_waitingForFrame = false; m_pendingExpansion = QPersistentModelIndex();
    m_transitionIndex = index; m_expanding = expanding; m_transitionTop = row.bottom() + 1;
    m_expandedTop = m_collapsedTop = cachedRowBottom(index);
    m_childrenHeight = expanding ? 0 : descendantHeight(index, true);
    m_expandedFrame = expanding ? QPixmap() : m_logicalFrame;
    m_collapsedFrame = expanding ? m_logicalFrame : QPixmap();
    // QFileSystemModel fetches children asynchronously on the first expansion.
    // Preserve the old frame until those rows arrive, so the first open animates too.
    if (expanding && model()->rowCount(index) == 0 && model()->hasChildren(index)) {
        m_pendingExpansion = index; return;
    }
    startTransition();
}

void FileTreeView::startTransition(qreal start)
{
    // Wait for layout and automatic scrollbars to settle. The clock should only
    // start once both snapshots exist and the first animated frame can be drawn.
    m_startProgress = start < 0 ? (m_expanding ? 0.0 : 1.0) : start;
    m_waitingForFrame = true; viewport()->update();
}

void FileTreeView::finishTransition()
{
    m_transition.stop(); m_pendingExpansion = QPersistentModelIndex();
    m_waitingForFrame = false;
    m_transitionIndex = QPersistentModelIndex();
    m_expandedFrame = QPixmap(); m_collapsedFrame = QPixmap(); viewport()->update();
}

void FileTreeView::invalidateFrame()
{
    finishTransition(); m_logicalFrame = QPixmap(); m_visibleRows.clear();
}

void FileTreeView::paintTransition(QPainter &painter)
{
    if (m_transition.state() != QAbstractAnimation::Running || m_expandedFrame.isNull() || m_collapsedFrame.isNull()) return;
    const int revealed = qRound(m_childrenHeight * m_transition.currentValue().toReal());
    const QRect region(0, m_transitionTop, viewport()->width(), qMax(0, viewport()->height() - m_transitionTop));
    painter.save();
    painter.setClipRect(region, Qt::IntersectClip); painter.fillRect(region, viewport()->palette().brush(QPalette::Base));
    painter.save();
    painter.setClipRect(QRect(0, m_transitionTop, viewport()->width(), revealed), Qt::IntersectClip);
    painter.drawPixmap(0, m_transitionTop - m_expandedTop + revealed - m_childrenHeight, m_expandedFrame); painter.restore();
    painter.setClipRect(QRect(0, m_transitionTop + revealed, viewport()->width(), region.height()), Qt::IntersectClip);
    painter.drawPixmap(0, m_transitionTop - m_collapsedTop + revealed, m_collapsedFrame); painter.restore();
}

int FileTreeView::cachedRowBottom(const QModelIndex &index) const
{
    for (const auto &row : m_visibleRows) if (row.first == index) return row.second.bottom() + 1;
    return visualRect(index).bottom() + 1;
}

int FileTreeView::descendantHeight(const QModelIndex &index, bool cached) const
{
    const auto isDescendant = [&index](QModelIndex child) {
        for (auto parent = child.parent(); parent.isValid(); parent = parent.parent()) if (parent == index) return true;
        return false;
    };
    const int top = cached ? cachedRowBottom(index) : visualRect(index).bottom() + 1;
    int bottom = top;
    if (cached) {
        for (const auto &row : m_visibleRows)
            if (isDescendant(row.first)) bottom = qMax(bottom, row.second.bottom() + 1);
    } else {
        auto child = indexBelow(index);
        while (child.isValid() && isDescendant(child)) {
            const QRect rect = visualRect(child);
            if (rect.isEmpty()) break;
            bottom = qMax(bottom, rect.bottom() + 1);
            if (bottom >= viewport()->height()) break;
            child = indexBelow(child);
        }
    }
    return qBound(0, bottom - top, qMax(0, viewport()->height() - top));
}

void FileTreeView::paintEvent(QPaintEvent *event)
{
    QTreeView::paintEvent(event);
    if (!m_animationsEnabled) return;
    // Cache only the visible rows. Render directly through QTreeView instead of
    // grabbing the viewport during a paint event, which would cause recursive paints.
    QPixmap frame(viewport()->size() * devicePixelRatioF()); frame.setDevicePixelRatio(devicePixelRatioF());
    frame.fill(viewport()->palette().color(QPalette::Base));
    {
        QPainter painter(&frame); drawTree(&painter, viewport()->rect());
    }
    m_logicalFrame = frame;
    m_visibleRows.clear();
    for (auto index = indexAt(QPoint(viewport()->width() / 2, 0)); index.isValid(); index = indexBelow(index)) {
        const QRect row = visualRect(index);
        if (row.top() >= viewport()->height()) break;
        m_visibleRows.append({index, row});
    }
    if (m_waitingForFrame || m_transition.state() == QAbstractAnimation::Running) {
        const QRect row = visualRect(m_transitionIndex);
        if (!viewport()->rect().intersects(row)) { finishTransition(); return; }
        m_transitionTop = row.bottom() + 1;
        if (m_expanding) {
            m_expandedFrame = frame; m_expandedTop = m_transitionTop;
            m_childrenHeight = descendantHeight(m_transitionIndex, false);
        } else { m_collapsedFrame = frame; m_collapsedTop = m_transitionTop; }
        if (m_waitingForFrame) {
            m_waitingForFrame = false;
            const qreal end = m_expanding ? 1.0 : 0.0;
            m_transition.setStartValue(m_startProgress); m_transition.setEndValue(end);
            m_transition.setDuration(qMax(1, qRound(m_duration * qAbs(end - m_startProgress))));
            m_transition.start();
        }
    }
    QPainter painter(viewport()); painter.setClipRegion(event->region()); paintTransition(painter);
}

void FileTreeView::resizeEvent(QResizeEvent *event)
{
    // A scrollbar changes the viewport size while the tree widget stays the same
    // size. Preserve the pending/active transition in that case.
    if (m_viewSize != size() || !m_transitionIndex.isValid()) invalidateFrame();
    m_viewSize = size(); QTreeView::resizeEvent(event);
}
void FileTreeView::hideEvent(QHideEvent *event) { invalidateFrame(); QTreeView::hideEvent(event); }
void FileTreeView::scrollContentsBy(int dx, int dy)
{
    if ((dx || dy) && !m_waitingForFrame && !m_pendingExpansion.isValid()) invalidateFrame();
    QTreeView::scrollContentsBy(dx, dy);
}
void FileTreeView::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::StyleChange || event->type() == QEvent::PaletteChange || event->type() == QEvent::FontChange) invalidateFrame();
    QTreeView::changeEvent(event);
}
}
