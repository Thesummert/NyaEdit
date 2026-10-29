#include "Animation.h"
#include <QEvent>
#include <QPainter>
#include <QSplitter>
#include <cmath>

namespace nya {
int AnimationSettings::duration(int milliseconds) const
{
    const double normalized = std::isfinite(speed) ? qBound(0.25, speed, 3.0) : 1.0;
    return qMax(1, qRound(milliseconds / normalized));
}

PanelTransition::PanelTransition(QWidget *panel, Qt::Orientation orientation)
    : QObject(panel), m_panel(panel), m_orientation(orientation),
      m_minimum(orientation == Qt::Horizontal ? panel->minimumWidth() : panel->minimumHeight()),
      m_maximum(orientation == Qt::Horizontal ? panel->maximumWidth() : panel->maximumHeight()),
      m_expanded(!panel->isHidden())
{
    m_animation.setEasingCurve(QEasingCurve::OutCubic);
    connect(&m_animation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) { setExtent(value.toInt()); });
    connect(&m_animation, &QVariantAnimation::finished, this, &PanelTransition::finish);
}

void PanelTransition::setExtent(int extent)
{
    if (m_orientation == Qt::Horizontal) {
        m_panel->setFixedWidth(extent);
        if (auto *splitter = qobject_cast<QSplitter *>(m_panel->parentWidget())) {
            auto sizes = splitter->sizes();
            if (sizes.size() == 2) { const int total = sizes[0] + sizes[1]; splitter->setSizes({extent, qMax(0, total - extent)}); }
        }
    } else m_panel->setFixedHeight(extent);
}

void PanelTransition::setExpanded(bool expanded, int duration)
{
    const bool running = m_animation.state() == QAbstractAnimation::Running;
    if (!running && expanded == m_expanded && expanded == !m_panel->isHidden()) return;
    int from = m_panel->isHidden() ? 0 : (m_orientation == Qt::Horizontal ? m_panel->width() : m_panel->height());
    if (!running && !m_panel->isHidden()) m_expandedExtent = from;
    m_animation.stop(); m_expanded = expanded; m_transitioning = true;
    const int hint = m_orientation == Qt::Horizontal ? m_panel->sizeHint().width() : m_panel->sizeHint().height();
    const int to = expanded ? qBound(m_minimum, m_orientation == Qt::Horizontal ? qMax(m_expandedExtent, m_minimum) : hint, m_maximum) : 0;
    if (expanded) m_expandedExtent = to;
    if (duration <= 0 || !m_panel->window()->isVisible()) { finish(); return; }
    setExtent(from); m_panel->show();
    m_animation.setDuration(duration); m_animation.setStartValue(from); m_animation.setEndValue(to); m_animation.start();
}

void PanelTransition::finish()
{
    if (!m_transitioning) return;
    m_transitioning = false;
    m_animation.stop();
    // Preserve the splitter's remembered width when hiding, including across restarts.
    if (m_orientation == Qt::Horizontal && m_expandedExtent > 0) setExtent(m_expandedExtent);
    if (!m_expanded) m_panel->hide();
    if (m_orientation == Qt::Horizontal) {
        m_panel->setMinimumWidth(m_minimum); m_panel->setMaximumWidth(m_maximum);
    } else { m_panel->setMinimumHeight(m_minimum); m_panel->setMaximumHeight(m_maximum); }
    if (m_expanded) m_panel->show();
}

int PanelTransition::expandedExtent() const
{
    if (m_expanded && !m_transitioning && !m_panel->isHidden())
        return m_orientation == Qt::Horizontal ? m_panel->width() : m_panel->height();
    return m_expandedExtent;
}
void PanelTransition::restoreExpandedExtent(int extent)
{
    m_expandedExtent = qBound(m_minimum, extent, m_maximum);
}

PageTransition::PageTransition(QWidget *workspace) : QWidget(workspace)
{
    setAttribute(Qt::WA_TransparentForMouseEvents); setFocusPolicy(Qt::NoFocus); hide();
    workspace->installEventFilter(this);
    m_animation.setStartValue(1.0); m_animation.setEndValue(0.0); m_animation.setEasingCurve(QEasingCurve::OutCubic);
    connect(&m_animation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) { m_opacity = value.toReal(); update(); });
    connect(&m_animation, &QVariantAnimation::finished, this, &PageTransition::finish);
}
void PageTransition::start(int duration)
{
    finish();
    if (duration <= 0 || !parentWidget()->isVisible()) return;
    setGeometry(parentWidget()->rect()); m_opacity = 1; show(); raise();
    m_animation.setDuration(duration); m_animation.start();
}
void PageTransition::finish() { m_animation.stop(); hide(); }
void PageTransition::paintEvent(QPaintEvent *)
{
    QPainter painter(this); QColor color = palette().color(QPalette::Window); color.setAlphaF(m_opacity);
    painter.fillRect(rect(), color);
}
bool PageTransition::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::Hide) finish();
    if (event->type() == QEvent::Resize) { finish(); setGeometry(parentWidget()->rect()); }
    return QWidget::eventFilter(watched, event);
}
}
