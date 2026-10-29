#include "AnimationSettingsDialog.h"
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <cmath>

namespace nya {
AnimationSettingsDialog::AnimationSettingsDialog(const AnimationSettings &settings, QWidget *parent) : QDialog(parent)
{
    setObjectName("animationSettingsDialog"); setWindowTitle(QStringLiteral("动画设置")); setMinimumWidth(400);
    auto *layout = new QVBoxLayout(this); layout->setContentsMargins(24, 24, 24, 20); layout->setSpacing(16);
    auto *description = new QLabel(QStringLiteral("调整界面与编辑动画，确认后立即应用于所有文档。"));
    description->setWordWrap(true); layout->addWidget(description);
    m_interface = new QCheckBox(QStringLiteral("窗口界面动画（侧栏、文件树、搜索面板、页面切换）"));
    m_interface->setObjectName("interfaceAnimation"); m_interface->setChecked(settings.interfaceEnabled); layout->addWidget(m_interface);
    m_cursor = new QCheckBox(QStringLiteral("光标动画（平滑移动与淡入淡出闪烁）"));
    m_cursor->setObjectName("cursorAnimation"); m_cursor->setChecked(settings.cursorEnabled); layout->addWidget(m_cursor);
    m_selection = new QCheckBox(QStringLiteral("选择区域动画（高亮渐变）"));
    m_selection->setObjectName("selectionAnimation"); m_selection->setChecked(settings.selectionEnabled); layout->addWidget(m_selection);
    auto *form = new QFormLayout;
    m_speed = new QDoubleSpinBox; m_speed->setObjectName("animationSpeed"); m_speed->setRange(0.25, 3.0);
    m_speed->setDecimals(2); m_speed->setSingleStep(0.25); m_speed->setSuffix(QStringLiteral(" ×"));
    m_speed->setValue(std::isfinite(settings.speed) ? settings.speed : 1.0); m_speed->setAccessibleName(QStringLiteral("动画速度"));
    form->addRow(QStringLiteral("动画速度(&S)"), m_speed); layout->addLayout(form);
    auto *hint = new QLabel(QStringLiteral("1 × 为默认速度；数值越大，动画越快。三个动画开关互相独立。"));
    hint->setWordWrap(true); layout->addWidget(hint);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Reset);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("确定")); buttons->button(QDialogButtonBox::Ok)->setObjectName("primary");
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消")); buttons->button(QDialogButtonBox::Reset)->setText(QStringLiteral("恢复默认"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::Reset), &QPushButton::clicked, this, [this] {
        m_interface->setChecked(true); m_cursor->setChecked(true); m_selection->setChecked(true); m_speed->setValue(1.0);
    });
    layout->addWidget(buttons);
}
AnimationSettings AnimationSettingsDialog::selectedSettings() const
{
    return {m_interface->isChecked(), m_cursor->isChecked(), m_selection->isChecked(), m_speed->value()};
}
}
