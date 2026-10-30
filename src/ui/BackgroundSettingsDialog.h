#pragma once
#include "EditorBackground.h"
#include <QDialog>

class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QSpinBox;
namespace nya {
class BackgroundSettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit BackgroundSettingsDialog(const BackgroundSettings &settings, bool dark, QWidget *parent = nullptr);
    BackgroundSettings selectedSettings() const;
    void accept() override;
private:
    bool updatePreview();
    QLineEdit *m_path;
    QSpinBox *m_opacity;
    QSpinBox *m_blur;
    QLabel *m_status;
    QDialogButtonBox *m_buttons;
    EditorBackground *m_background;
    QTimer m_previewTimer;
};
}
