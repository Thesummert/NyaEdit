#pragma once
#include "TreeSitter.h"
#include <QDialog>

class QCheckBox;
class QLineEdit;
class QListWidget;
class QPushButton;
namespace nya {
class LanguageSettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit LanguageSettingsDialog(const LanguageSettings &settings, QWidget *parent = nullptr);
    LanguageSettings settings() const;
private:
    void saveServer();
    void selectServer(int row);
    LspServerSettings editedServer() const;
    QCheckBox *m_enabled;
    QLineEdit *m_executable;
    QLineEdit *m_config;
    QCheckBox *m_lspEnabled;
    QListWidget *m_lspServers;
    QWidget *m_serverForm;
    QPushButton *m_removeServer;
    QCheckBox *m_serverEnabled;
    QLineEdit *m_serverName;
    QLineEdit *m_lspExecutable;
    QLineEdit *m_lspArguments;
    QLineEdit *m_lspExtensions;
    QVector<LspServerSettings> m_servers;
    int m_selected = -1;
};
}
