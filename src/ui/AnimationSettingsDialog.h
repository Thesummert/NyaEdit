#pragma once
#include "Animation.h"
#include <QDialog>

class QCheckBox;
class QDoubleSpinBox;
namespace nya {
class AnimationSettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit AnimationSettingsDialog(const AnimationSettings &settings, QWidget *parent = nullptr);
    AnimationSettings selectedSettings() const;
private:
    QCheckBox *m_interface;
    QCheckBox *m_cursor;
    QCheckBox *m_selection;
    QDoubleSpinBox *m_speed;
};
}
