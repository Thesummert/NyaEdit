#pragma once
#include "TaskConfiguration.h"
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <memory>

namespace nya {
class TaskProcess;
class TaskOutputDecoder;
class TaskRunner : public QObject {
    Q_OBJECT
public:
    explicit TaskRunner(QObject *parent = nullptr);
    ~TaskRunner() override;
    bool isRunning() const { return m_running; }
    bool start(const QVector<TaskInvocation> &plan);
    bool start(const TaskConfiguration &configuration, const QString &label, const TaskContext &context);
    void stop();
signals:
    void output(const QString &text);
    void runningChanged(bool running);
    void taskStarted(const QString &label);
    void finished(int exitCode, bool canceled);
private:
    void startNext();
    void preparePlan(const TaskConfiguration &configuration, const QString &label, TaskContext context);
    void resolveEnvironment(const TaskConfiguration &configuration, const QString &label, const TaskContext &context);
    void readOutput();
    void finish(int exitCode);
    QVector<TaskInvocation> m_plan;
    int m_index = 0;
    bool m_running = false;
    bool m_stopping = false;
    QPointer<TaskProcess> m_process;
    std::unique_ptr<TaskOutputDecoder> m_decoder;
    QTimer m_killTimer;
    QTimer m_environmentTimer;
};
}
