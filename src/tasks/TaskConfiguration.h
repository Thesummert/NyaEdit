#pragma once
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QStringList>
#include <QVector>

namespace nya {
struct ProjectTask {
    QString label;
    QString detail;
    QString type = QStringLiteral("shell");
    QString command;
    QStringList args;
    QString cwd = QStringLiteral("${workspaceFolder}");
    QJsonObject env;
    QStringList dependsOn;
    QString group;
    bool isDefault = false;
};

struct TaskContext {
    QString workspaceFolder;
    QString file;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
};

struct TaskInvocation {
    QString label;
    QString program;
    QStringList arguments;
    QString workingDirectory;
    QProcessEnvironment environment;
    QString displayCommand;
};

class TaskConfiguration {
public:
    QVector<ProjectTask> tasks;
    QString error;
    bool ok() const { return error.isEmpty(); }
    static QString filePath(const QString &workspaceFolder);
    static TaskConfiguration load(const QString &workspaceFolder);
    static TaskConfiguration parse(const QByteArray &json);
    static QByteArray templateContents();
    QString defaultBuildTask() const;
    QVector<TaskInvocation> plan(const QString &label, const TaskContext &context, QString *error) const;
};
}
