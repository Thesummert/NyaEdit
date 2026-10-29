#pragma once
#include <QObject>
#include <QVariantAnimation>
#include <QWidget>

namespace nya {
struct AnimationSettings {
    bool interfaceEnabled = true;
    bool cursorEnabled = true;
    bool selectionEnabled = true;
    double speed = 1.0;
    int duration(int milliseconds) const;
};

// Animate the layout extent, restoring the original constraints at rest.
class PanelTransition : public QObject {
public:
    PanelTransition(QWidget *panel, Qt::Orientation orientation);
    void setExpanded(bool expanded, int duration);
    void finish();
    bool isExpanded() const { return m_expanded; }
    int expandedExtent() const;
    void restoreExpandedExtent(int extent);
private:
    void setExtent(int extent);
    QWidget *m_panel;
    Qt::Orientation m_orientation;
    QVariantAnimation m_animation;
    int m_minimum;
    int m_maximum;
    int m_expandedExtent = 0;
    bool m_expanded;
    bool m_transitioning = false;
};

// A short reveal over the workspace; mouse and keyboard input pass through.
class PageTransition : public QWidget {
public:
    explicit PageTransition(QWidget *workspace);
    void start(int duration);
    void finish();
protected:
    void paintEvent(QPaintEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
private:
    QVariantAnimation m_animation;
    qreal m_opacity = 0;
};
}
