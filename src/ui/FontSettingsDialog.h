#pragma once
#include <QDialog>
#include <QFont>

class QFontComboBox;
class QSpinBox;
class QPlainTextEdit;

namespace nya {
class FontSettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit FontSettingsDialog(const QFont &font, QWidget *parent = nullptr);
    QFont selectedFont() const;
private:
    void updatePreview();
    QFontComboBox *m_family;
    QSpinBox *m_size;
    QPlainTextEdit *m_preview;
};
}
