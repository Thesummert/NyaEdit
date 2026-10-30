#pragma once
#include <QWidget>
#include <QTimer>

class QLabel;
class QPlainTextEdit;
class QToolButton;
namespace nya {
class TaskPanel : public QWidget {
    Q_OBJECT
public:
    explicit TaskPanel(QWidget *parent = nullptr);
    void beginTask(const QString &label, const QString &workspaceFolder);
    void appendOutput(const QString &text);
    void setRunning(bool running);
    void refreshFont();
    void flush();
signals:
    void stopRequested();
private:
    enum class EscapeState { Normal, Escape, Csi, Osc, OscEscape };
    QString plainOutput(const QString &text);
    QLabel *m_title;
    QPlainTextEdit *m_output;
    QToolButton *m_stop;
    QTimer m_flushTimer;
    QString m_pending;
    EscapeState m_escapeState = EscapeState::Normal;
    bool m_afterCarriageReturn = false;
};
}
