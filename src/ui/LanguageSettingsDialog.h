#pragma once
#include "TreeSitter.h"
#include <QDialog>

class QCheckBox;
class QLineEdit;
namespace nya {
class LanguageSettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit LanguageSettingsDialog(const LanguageSettings &settings, QWidget *parent = nullptr);
    LanguageSettings settings() const;
private:
    QCheckBox *m_enabled;
    QLineEdit *m_executable;
    QLineEdit *m_config;
};
}
