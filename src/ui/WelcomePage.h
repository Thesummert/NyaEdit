#pragma once
#include "AppStorage.h"
#include <QWidget>

class QLabel;
class QListWidget;
namespace nya {
class WelcomePage : public QWidget {
    Q_OBJECT
public:
    explicit WelcomePage(QWidget *parent = nullptr);
    void setHistory(const QVector<RecentEntry> &entries);
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
signals:
    void newDocumentRequested();
    void openFilesRequested();
    void openFolderRequested();
    void recentRequested(const QString &path, bool folder);
private:
    QListWidget *m_history;
    QLabel *m_empty;
};
}
