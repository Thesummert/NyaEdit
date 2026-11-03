#pragma once
#include "TreeSitter.h"
#include "AppStorage.h"
#include <QDialog>

class QCheckBox;
class QLineEdit;
class QListWidget;
class QPushButton;
class QLabel;
class QDialogButtonBox;
namespace nya {
class TreeSitterManager;
class LanguageSettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit LanguageSettingsDialog(const LanguageSettings &settings, QWidget *parent = nullptr, StoragePaths paths = {});
    LanguageSettings settings() const;
    void accept() override;
    void reject() override;
private:
    void refreshParsers();
    void setDownloading(bool downloading);
    void saveServer();
    void selectServer(int row);
    LspServerSettings editedServer() const;
    QCheckBox *m_enabled;
    QLineEdit *m_executable;
    QLineEdit *m_config;
    QLineEdit *m_parserDirectory;
    QListWidget *m_parsers;
    QPushButton *m_download;
    QPushButton *m_cancelDownload;
    QLabel *m_downloadStatus;
    QDialogButtonBox *m_buttons = nullptr;
    TreeSitterManager *m_manager = nullptr;
    StoragePaths m_paths;
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
