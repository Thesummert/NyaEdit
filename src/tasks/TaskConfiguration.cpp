#include "TaskConfiguration.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSet>
#include <functional>

namespace nya {
namespace {
bool knownKeys(const QJsonObject &object, const QStringList &keys, QString *error)
{
    for (auto it = object.begin(); it != object.end(); ++it) {
        if (!keys.contains(it.key())) { *error = QStringLiteral("不支持的配置字段：%1").arg(it.key()); return false; }
    }
    return true;
}

bool strings(const QJsonValue &value, QStringList *result, QString *error)
{
    if (!value.isArray()) { *error = QStringLiteral("需要字符串数组。"); return false; }
    for (const auto &item : value.toArray()) {
        if (!item.isString() || item.toString().contains(QChar::Null)) {
            *error = QStringLiteral("数组中只能包含不含空字符的字符串。"); return false;
        }
        result->append(item.toString());
    }
    return true;
}

QJsonObject platformTask(QJsonObject object)
{
#ifdef Q_OS_WIN
    const auto platform = object.value("windows").toObject();
#elif defined(Q_OS_MAC)
    const auto platform = object.value("osx").toObject();
#else
    const auto platform = object.value("linux").toObject();
#endif
    for (auto it = platform.begin(); it != platform.end(); ++it) {
        if (it.key() == "options") {
            auto options = object.value("options").toObject();
            const auto overrides = it.value().toObject();
            for (auto option = overrides.begin(); option != overrides.end(); ++option) {
                if (option.key() == "env") {
                    auto env = options.value("env").toObject();
                    const auto additions = option.value().toObject();
                    for (auto entry = additions.begin(); entry != additions.end(); ++entry) env.insert(entry.key(), entry.value());
                    options.insert("env", env);
                } else options.insert(option.key(), option.value());
            }
            object.insert("options", options);
        } else object.insert(it.key(), it.value());
    }
    return object;
}

QString variable(const QString &name, const TaskContext &context, QString *error)
{
    if (name == "workspaceFolder") return context.workspaceFolder;
    if (name == "workspaceFolderBasename") return QFileInfo(context.workspaceFolder).fileName();
    if (name == "pathSeparator") return QString(QDir::separator());
    if (name.startsWith("env:")) {
        const auto key = name.mid(4);
        if (!key.isEmpty() && context.environment.contains(key)) return context.environment.value(key);
        *error = QStringLiteral("环境变量未定义：%1").arg(key); return {};
    }
    if (QStringList{"file", "fileDirname", "fileBasename", "fileBasenameNoExtension", "relativeFile"}.contains(name)) {
        if (context.file.isEmpty()) { *error = QStringLiteral("变量 ${%1} 需要一个已保存的当前文档。").arg(name); return {}; }
        const QFileInfo file(context.file);
        if (name == "file") return file.absoluteFilePath();
        if (name == "fileDirname") return file.absolutePath();
        if (name == "fileBasename") return file.fileName();
        if (name == "fileBasenameNoExtension") return file.completeBaseName();
        return QDir(context.workspaceFolder).relativeFilePath(file.absoluteFilePath());
    }
    *error = QStringLiteral("不支持的变量：${%1}").arg(name); return {};
}

QString quoteArgument(QString text)
{
#ifdef Q_OS_WIN
    text.replace("'", "''");
#else
    text.replace("'", "'\\''");
#endif
    return "'" + text + "'";
}

QString expand(const QString &text, const TaskContext &context, QString *error, bool shell = false)
{
    // Shell substitutions preserve their surrounding quote context. In args/cwd/env
    // values remain literal; shell arguments are quoted separately after expansion.
    static const QRegularExpression expression(QStringLiteral("\\$\\{([^{}]+)\\}"));
    auto matches = expression.globalMatch(text);
    QString result; int end = 0; QChar quote;
    while (matches.hasNext()) {
        const auto match = matches.next();
        const auto prefix = text.mid(end, match.capturedStart() - end);
        if (shell) for (int i = 0; i < prefix.size(); ++i) {
            const auto c = prefix.at(i);
#ifdef Q_OS_WIN
            const bool escape = c == '`' && quote != '\'';
#else
            const bool escape = c == '\\' && quote != '\'';
#endif
            if (escape) { ++i; continue; }
            if (c == quote) quote = {};
            else if (quote.isNull() && (c == '\'' || c == '"')) quote = c;
        }
        result += prefix;
        auto value = variable(match.captured(1), context, error);
        if (!error->isEmpty()) return {};
        if (shell) {
            if (quote.isNull()) value = quoteArgument(value);
            else if (quote == '\'') {
#ifdef Q_OS_WIN
                value.replace("'", "''");
#else
                value.replace("'", "'\\''");
#endif
            } else {
#ifdef Q_OS_WIN
                value.replace("`", "``"); value.replace("$", "`$"); value.replace("\"", "`\"");
#else
                value.replace("\\", "\\\\"); value.replace("$", "\\$"); value.replace("`", "\\`"); value.replace("\"", "\\\"");
#endif
            }
        }
        result += value; end = match.capturedEnd();
    }
    return result + text.mid(end);
}
}

QString TaskConfiguration::filePath(const QString &workspaceFolder)
{
    return QDir(workspaceFolder).filePath(QStringLiteral(".ne/task.json"));
}

TaskConfiguration TaskConfiguration::load(const QString &workspaceFolder)
{
    QFile file(filePath(workspaceFolder));
    if (!QFileInfo(file).isFile()) {
        TaskConfiguration result; result.error = QStringLiteral("任务配置不存在或不是文件：%1。请使用“运行 → 配置任务…”创建。").arg(file.fileName()); return result;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        TaskConfiguration result; result.error = QStringLiteral("无法读取 %1：%2").arg(file.fileName(), file.errorString()); return result;
    }
    if (file.size() > 1024 * 1024) {
        TaskConfiguration result; result.error = QStringLiteral("任务配置不能超过 1 MiB。"); return result;
    }
    return parse(file.readAll());
}

TaskConfiguration TaskConfiguration::parse(const QByteArray &json)
{
    TaskConfiguration result;
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        const auto prefix = json.left(int(parseError.offset));
        result.error = QStringLiteral("task.json 第 %1 行，第 %2 列：%3")
            .arg(prefix.count('\n') + 1).arg(QString::fromUtf8(prefix.mid(prefix.lastIndexOf('\n') + 1)).size() + 1).arg(parseError.errorString());
        return result;
    }
    const auto fail = [&result](const QString &error) { result.error = error; result.tasks.clear(); return result; };
    if (!document.isObject()) return fail(QStringLiteral("task.json 顶层必须是 JSON 对象。"));
    const auto root = document.object(); QString error;
    if (!knownKeys(root, {"version", "tasks", "$schema"}, &error)) return fail(error);
    if (root.value("version").toString() != "2.0.0") return fail(QStringLiteral("version 必须为 \"2.0.0\"。"));
    if (!root.value("tasks").isArray()) return fail(QStringLiteral("tasks 必须是任务数组。"));
    if (root.value("tasks").toArray().size() > 256) return fail(QStringLiteral("最多支持 256 个任务。"));
    QSet<QString> labels; bool defaultBuild = false;
    const QStringList taskKeys{"label", "detail", "type", "command", "args", "options", "dependsOn", "dependsOrder", "group", "windows", "osx", "linux"};
    for (const auto &value : root.value("tasks").toArray()) {
        if (!value.isObject()) return fail(QStringLiteral("每个任务必须是 JSON 对象。"));
        auto object = value.toObject();
        if (!knownKeys(object, taskKeys, &error)) return fail(error);
        for (const auto &key : {"windows", "osx", "linux"}) {
            if (!object.contains(key)) continue;
            if (!object.value(key).isObject()) return fail(QStringLiteral("%1 必须是平台覆盖对象。").arg(key));
            const auto overrides = object.value(key).toObject();
            if (!knownKeys(overrides, {"type", "command", "args", "options"}, &error)) return fail(error);
            if (overrides.contains("options")) {
                if (!overrides.value("options").isObject()) return fail(QStringLiteral("平台 options 必须是对象。"));
                const auto options = overrides.value("options").toObject();
                if (!knownKeys(options, {"cwd", "env"}, &error)) return fail(error);
                if (options.contains("env") && !options.value("env").isObject()) return fail(QStringLiteral("平台 options.env 必须是对象。"));
            }
        }
        object = platformTask(object);
        ProjectTask task;
        if (!object.value("label").isString() || object.value("label").toString().trimmed().isEmpty()) return fail(QStringLiteral("任务需要非空的 label。"));
        task.label = object.value("label").toString();
        if (task.label.contains(QChar::Null) || labels.contains(task.label)) return fail(QStringLiteral("任务 label 重复或含空字符：%1").arg(task.label));
        labels.insert(task.label);
        for (const auto &key : {"detail", "type", "command", "dependsOrder"})
            if (object.contains(key) && (!object.value(key).isString() || object.value(key).toString().contains(QChar::Null)))
                return fail(QStringLiteral("任务“%1”的 %2 必须是不含空字符的字符串。").arg(task.label, key));
        task.detail = object.value("detail").toString();
        task.type = object.value("type").toString("shell");
        if (task.type != "shell" && task.type != "process") return fail(QStringLiteral("任务“%1”的 type 只支持 shell 或 process。").arg(task.label));
        task.command = object.value("command").toString();
        if (object.contains("args") && !strings(object.value("args"), &task.args, &error)) return fail(task.label + ": " + error);
        if (object.contains("dependsOn")) {
            if (object.value("dependsOn").isString()) task.dependsOn = QStringList{object.value("dependsOn").toString()};
            else if (!strings(object.value("dependsOn"), &task.dependsOn, &error)) return fail(task.label + ": " + error);
        }
        if (object.contains("dependsOrder") && object.value("dependsOrder").toString() != "sequence")
            return fail(QStringLiteral("任务“%1”的 dependsOrder 目前只支持 sequence（顺序执行）。").arg(task.label));
        if (task.command.trimmed().isEmpty() && task.dependsOn.isEmpty()) return fail(QStringLiteral("任务“%1”需要 command 或 dependsOn。").arg(task.label));
        if (task.command.trimmed().isEmpty() && !task.args.isEmpty()) return fail(QStringLiteral("任务“%1”没有 command，不能设置 args。").arg(task.label));
        if (object.contains("options")) {
            if (!object.value("options").isObject()) return fail(QStringLiteral("任务 options 必须是对象。"));
            const auto options = object.value("options").toObject();
            if (!knownKeys(options, {"cwd", "env"}, &error)) return fail(error);
            if (options.contains("cwd")) {
                if (!options.value("cwd").isString() || options.value("cwd").toString().isEmpty() || options.value("cwd").toString().contains(QChar::Null))
                    return fail(QStringLiteral("options.cwd 必须是非空目录字符串。"));
                task.cwd = options.value("cwd").toString();
            }
            if (options.contains("env") && !options.value("env").isObject()) return fail(QStringLiteral("options.env 必须是环境变量对象。"));
            task.env = options.value("env").toObject();
            for (auto it = task.env.begin(); it != task.env.end(); ++it)
                if (it.key().isEmpty() || it.key().contains('=') || it.key().contains(QChar::Null) ||
                    (!it.value().isString() && !it.value().isNull()) || it.value().toString().contains(QChar::Null))
                    return fail(QStringLiteral("环境变量名称无效，或值不是字符串 / null：%1").arg(it.key()));
        }
        const auto group = object.value("group");
        if (group.isString()) task.group = group.toString();
        else if (group.isObject()) {
            const auto info = group.toObject();
            if (!knownKeys(info, {"kind", "isDefault"}, &error)) return fail(error);
            if (!info.value("kind").isString() || (info.contains("isDefault") && !info.value("isDefault").isBool())) return fail(QStringLiteral("group 需要 kind 字符串和可选的 isDefault 布尔值。"));
            task.group = info.value("kind").toString(); task.isDefault = info.value("isDefault").toBool();
        } else if (!group.isUndefined()) return fail(QStringLiteral("group 必须是 build / test 字符串或对象。"));
        if (!group.isUndefined() && task.group != "build" && task.group != "test") return fail(QStringLiteral("group.kind 只支持 build 或 test。"));
        if (task.group == "build" && task.isDefault) {
            if (defaultBuild) return fail(QStringLiteral("只能指定一个默认构建任务。"));
            defaultBuild = true;
        }
        result.tasks.append(task);
    }
    for (const auto &task : result.tasks) for (const auto &dependency : task.dependsOn)
        if (!labels.contains(dependency)) return fail(QStringLiteral("任务“%1”依赖不存在的任务“%2”。").arg(task.label, dependency));
    return result;
}

QString TaskConfiguration::defaultBuildTask() const
{
    QString first;
    for (const auto &task : tasks) if (task.group == "build") {
        if (task.isDefault) return task.label;
        if (first.isEmpty()) first = task.label;
    }
    return first;
}

QVector<TaskInvocation> TaskConfiguration::plan(const QString &label, const TaskContext &context, QString *error) const
{
    error->clear();
    if (!ok()) { *error = this->error; return {}; }
    if (context.workspaceFolder.isEmpty() || !QFileInfo(context.workspaceFolder).isDir()) { *error = QStringLiteral("项目目录不存在。"); return {}; }
    QVector<TaskInvocation> result; QSet<QString> visiting, visited;
    std::function<bool(const QString &)> visit = [&](const QString &name) {
        if (visited.contains(name)) return true;
        if (visiting.contains(name)) { *error = QStringLiteral("任务依赖存在循环：%1").arg(name); return false; }
        const ProjectTask *task = nullptr;
        for (const auto &entry : tasks) if (entry.label == name) { task = &entry; break; }
        if (!task) { *error = QStringLiteral("任务不存在：%1").arg(name); return false; }
        visiting.insert(name);
        for (const auto &dependency : task->dependsOn) if (!visit(dependency)) return false;
        visiting.remove(name); visited.insert(name);
        if (task->command.trimmed().isEmpty()) return true;
        TaskInvocation invocation; invocation.label = name; invocation.environment = context.environment;
        const auto cwd = expand(task->cwd, context, error);
        invocation.workingDirectory = QDir::cleanPath(QDir(context.workspaceFolder).absoluteFilePath(cwd));
        for (auto it = task->env.begin(); it != task->env.end(); ++it) {
            if (it.value().isNull()) invocation.environment.remove(it.key());
            else invocation.environment.insert(it.key(), expand(it.value().toString(), context, error));
            if (!error->isEmpty()) return false;
        }
        QStringList args;
        for (const auto &arg : task->args) {
            args.append(expand(arg, context, error));
            if (!error->isEmpty()) return false;
        }
        const auto command = expand(task->command, context, error, task->type == "shell");
        if (!error->isEmpty()) return false;
        if (task->type == "process") {
            invocation.program = command; invocation.arguments = args;
            if (QDir::isRelativePath(command) && (command.contains('/') || command.contains('\\')))
                invocation.program = QDir(invocation.workingDirectory).absoluteFilePath(command);
        } else {
            QString script = command;
            for (const auto &arg : args) script += ' ' + quoteArgument(arg);
#ifdef Q_OS_WIN
            invocation.program = QStringLiteral("powershell.exe");
            // A substituted executable path may itself be quoted; PowerShell needs &.
            if (script.startsWith('\'')) script.prepend("& ");
            invocation.arguments = QStringList{"-NoLogo", "-NoProfile", "-NonInteractive", "-Command",
                "[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false); " + script};
#else
            invocation.program = QStringLiteral("/bin/sh"); invocation.arguments = QStringList{"-c", script};
#endif
        }
        // Show the authored command without revealing substituted environment values.
        invocation.displayCommand = task->command;
        for (const auto &arg : task->args) invocation.displayCommand += ' ' + quoteArgument(arg);
        result.append(invocation); return true;
    };
    if (!visit(label)) return {};
    return result;
}

QByteArray TaskConfiguration::templateContents()
{
    return R"({
  "version": "2.0.0",
  "tasks": [
    {
      "label": "示例任务",
      "type": "shell",
      "command": "echo NyaEdit task is ready",
      "options": { "cwd": "${workspaceFolder}" },
      "group": { "kind": "build", "isDefault": true }
    }
  ]
}
)";
}
}
