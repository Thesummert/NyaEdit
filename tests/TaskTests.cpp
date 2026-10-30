#include "TaskConfiguration.h"
#include "TaskRunner.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTextStream>
#include <QThread>
#include <QtTest>
#ifdef Q_OS_UNIX
#include <signal.h>
#endif

using namespace nya;

static int fixture(const QStringList &args)
{
    const auto mode = args.value(0);
    QFile output; output.open(stdout, QIODevice::WriteOnly | QIODevice::Unbuffered);
    if (mode == "fail") { output.write("failed\n"); return 7; }
    if (mode == "write" || mode == "delayed-write") {
        if (mode == "delayed-write") QThread::msleep(700);
        QFile file(args.value(1)); if (!file.open(QIODevice::WriteOnly)) return 3; file.write("done"); return 0;
    }
    if (mode == "child") {
        QProcess child;
        child.start(QCoreApplication::applicationFilePath(), {"--task-fixture", "delayed-write", args.value(1)});
        if (!child.waitForStarted()) return 4;
        output.write("child ready\n"); output.flush(); QThread::msleep(10000); return 0;
    }
    if (mode == "slow" || mode == "ignore-term") {
#ifdef Q_OS_UNIX
        if (mode == "ignore-term") ::signal(SIGTERM, SIG_IGN);
#endif
        output.write("ready\n"); output.flush(); QThread::msleep(10000); return 0;
    }
    if (mode == "utf8") {
        const auto bytes = QStringLiteral("中文😀\n").toUtf8();
        for (char byte : bytes) { output.write(&byte, 1); output.flush(); QThread::msleep(12); }
        return 0;
    }
    QJsonObject result{{"cwd", QDir::currentPath()}, {"env", qEnvironmentVariable("NYA_TASK_TEST")}, {"args", QJsonArray::fromStringList(args.mid(1))}};
    output.write(QJsonDocument(result).toJson(QJsonDocument::Compact)); output.write("\n");
    QFile error; error.open(stderr, QIODevice::WriteOnly | QIODevice::Unbuffered); error.write("stderr-visible\n");
    return 0;
}

class TaskTests : public QObject {
    Q_OBJECT
    static QJsonObject task(const QString &label, const QString &mode, const QStringList &args = {})
    {
        return {{"label", label}, {"type", "process"}, {"command", QCoreApplication::applicationFilePath()},
            {"args", QJsonArray::fromStringList(QStringList{"--task-fixture", mode} + args)}};
    }
    static TaskConfiguration configuration(const QJsonArray &tasks)
    {
        return TaskConfiguration::parse(QJsonDocument(QJsonObject{{"version", "2.0.0"}, {"tasks", tasks}}).toJson());
    }
    static QVector<TaskInvocation> makePlan(const QJsonArray &tasks, const QString &label, const QString &root)
    {
        const auto config = configuration(tasks); QString error;
        TaskContext context; context.workspaceFolder = root;
        const auto result = config.plan(label, context, &error);
        if (!error.isEmpty()) qWarning() << error;
        return result;
    }
#ifdef Q_OS_UNIX
    static TaskContext shellContext(const QString &root)
    {
        TaskContext context; context.workspaceFolder = root;
        context.environment.insert("HOME", root); context.environment.insert("ZDOTDIR", root);
        context.environment.insert("PATH", "/usr/bin:/bin");
        context.environment.remove("ENV"); context.environment.remove("BASH_ENV");
#ifdef Q_OS_MAC
        context.environment.insert("SHELL", "/bin/zsh");
#else
        context.environment.insert("SHELL", "/bin/sh");
#endif
        return context;
    }
    static bool writeStartup(const QString &root, const QByteArray &contents)
    {
#ifdef Q_OS_MAC
        QFile file(root + "/.zshrc");
#else
        QFile file(root + "/.profile");
#endif
        return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
    }
#endif
private slots:
    void terminalEnvironmentResolvesProcessShellAndOverrides()
    {
#ifdef Q_OS_UNIX
        QTemporaryDir root;
        const auto bin = root.filePath("tools with spaces"), overrideBin = root.filePath("override tools");
        QVERIFY(QDir().mkpath(bin)); QVERIFY(QDir().mkpath(overrideBin));
        const QString executable = "nya-env-fixture";
        QVERIFY(QFile::link(QCoreApplication::applicationFilePath(), bin + '/' + executable));
        QVERIFY(writeStartup(root.path(), "printf 'private startup noise\\n'\nprintf 'private stderr\\n' >&2\nexport PATH='" + bin.toUtf8()
            + ":'$PATH\nexport NYA_TASK_TEST='from startup\nvalue=中文'\n"));
        auto context = shellContext(root.path());
        auto processTask = task("process", "echo", {"${env:NYA_TASK_TEST}"}); processTask.insert("command", executable);
        auto shellTask = processTask; shellTask.insert("label", "shell"); shellTask.insert("type", "shell"); shellTask.insert("dependsOn", "process");
        const auto config = configuration({processTask, shellTask});
        TaskRunner runner; QString output;
        connect(&runner, &TaskRunner::output, this, [&](const QString &text) { output += text; });
        QSignalSpy done(&runner, &TaskRunner::finished);
        QVERIFY(runner.start(config, "shell", context)); QVERIFY(runner.isRunning());
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 12000); QCOMPARE(done.first().at(0).toInt(), 0);
        QCOMPARE(output.count("stderr-visible"), 2);
        QVERIFY(output.contains("from startup\\nvalue=中文")); QVERIFY(!output.contains("private startup")); QVERIFY(!output.contains("private stderr"));
        QFile replacement(overrideBin + '/' + executable); QVERIFY(replacement.open(QIODevice::WriteOnly));
        replacement.write("#!/bin/sh\nprintf 'explicit PATH override works\\n'\n"); replacement.close();
        QVERIFY(replacement.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        processTask.insert("options", QJsonObject{{"env", QJsonObject{{"PATH", overrideBin + ":${env:PATH}"}}}});
        output.clear(); QVERIFY(runner.start(configuration({processTask}), "process", context));
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 2, 12000); QCOMPARE(done.last().at(0).toInt(), 0);
        QVERIFY(output.contains("explicit PATH override works")); QVERIFY(!output.contains("stderr-visible"));
#else
        QSKIP("Unix login-shell environment import");
#endif
    }
    void terminalEnvironmentFailureAndCancellation()
    {
#ifdef Q_OS_UNIX
        QTemporaryDir root; auto context = shellContext(root.path());
        QVERIFY(writeStartup(root.path(), "exit 1\n"));
        const auto config = configuration({task("echo", "echo")});
        TaskRunner runner; QString output;
        connect(&runner, &TaskRunner::output, this, [&](const QString &text) { output += text; });
        QSignalSpy done(&runner, &TaskRunner::finished);
        QVERIFY(runner.start(config, "echo", context)); QTRY_COMPARE(done.count(), 1); QCOMPARE(done.first().at(0).toInt(), 0);
        QVERIFY(output.contains(QStringLiteral("使用应用环境"))); QVERIFY(output.contains("stderr-visible"));
        const auto sentinel = root.filePath("should-not-run");
        QVERIFY(writeStartup(root.path(), "printf ready > '" + root.filePath("startup-ready").toUtf8() + "'\nsleep 30\n"));
        QVERIFY(runner.start(configuration({task("write", "write", {sentinel})}), "write", context));
        QTRY_VERIFY(QFileInfo::exists(root.filePath("startup-ready"))); runner.stop();
        QTRY_COMPARE(done.count(), 2); QVERIFY(done.last().at(1).toBool()); QVERIFY(!QFileInfo::exists(sentinel));
#else
        QSKIP("Unix login-shell environment import");
#endif
    }
    void configurationValidationAndDefaults()
    {
        const auto config = TaskConfiguration::parse(TaskConfiguration::templateContents());
        QVERIFY2(config.ok(), qPrintable(config.error)); QCOMPARE(config.tasks.size(), 1); QVERIFY(!config.defaultBuildTask().isEmpty());
        QVERIFY(!TaskConfiguration::parse("{\n broken").ok());
        QVERIFY(TaskConfiguration::parse("{\n broken").error.contains(QStringLiteral("第 2 行")));
        QVERIFY(!configuration({task("a", "echo"), task("a", "echo")}).ok());
        auto invalid = task("a", "echo"); invalid.insert("args", QJsonArray{42}); QVERIFY(!configuration({invalid}).ok());
        invalid = task("a", "echo"); invalid.insert("type", "npm"); QVERIFY(!configuration({invalid}).ok());
        invalid = task("a", "echo"); invalid.insert("dependsOn", "missing"); QVERIFY(!configuration({invalid}).ok());
        invalid = task("a", "echo"); invalid.insert("dependsOrder", "parallel"); QVERIFY(!configuration({invalid}).ok());
        invalid = task("a", "echo"); invalid.insert("isBackground", true); QVERIFY(!configuration({invalid}).ok());
        invalid = task("a", "echo"); invalid.insert("options", QJsonObject{{"env", QJsonObject{{"A", 42}}}}); QVERIFY(!configuration({invalid}).ok());
        invalid = task("a", "echo"); invalid.insert("options", QJsonObject{{"cwd", 42}}); QVERIFY(!configuration({invalid}).ok());
        invalid = task("a", "echo"); invalid.insert("osx", QJsonObject{{"options", 42}}); QVERIFY(!configuration({invalid}).ok());
        invalid = task("a", "echo"); invalid.insert("group", QJsonObject{{"kind", ""}}); QVERIFY(!configuration({invalid}).ok());
        auto first = task("a", "echo"), second = task("b", "echo");
        first.insert("group", "build"); second.insert("group", QJsonObject{{"kind", "build"}, {"isDefault", true}});
        QCOMPARE(configuration({first, second}).defaultBuildTask(), QString("b"));
        first.insert("group", QJsonObject{{"kind", "build"}, {"isDefault", true}}); QVERIFY(!configuration({first, second}).ok());
        QTemporaryDir root; QVERIFY(!TaskConfiguration::load(root.path()).ok());
    }
    void repositoryConfigurationIsValid()
    {
        const auto root = QDir(QFileInfo(QString::fromUtf8(__FILE__)).absolutePath()).absoluteFilePath("..");
        const auto config = TaskConfiguration::load(root); QVERIFY2(config.ok(), qPrintable(config.error));
        QCOMPARE(config.tasks.size(), 4); QCOMPARE(config.defaultBuildTask(), QStringLiteral("构建"));
        TaskContext context; context.workspaceFolder = root; QString error;
        const auto plan = config.plan(QStringLiteral("启动 NyaEdit"), context, &error);
        QVERIFY2(!plan.isEmpty(), qPrintable(error)); QCOMPARE(plan.size(), 3);
    }
    void dependenciesAreOrderedDeduplicatedAndValidated()
    {
        QTemporaryDir root;
        auto a = task("a", "echo"), b = task("b", "echo"), c = task("c", "echo");
        b.insert("dependsOn", "a"); c.insert("dependsOn", QJsonArray{"a", "b"});
        const QJsonObject all{{"label", "all"}, {"dependsOn", QJsonArray{"b", "c"}}, {"dependsOrder", "sequence"}};
        const auto plan = makePlan({a, b, c, all}, "all", root.path());
        QCOMPARE(plan.size(), 3); QCOMPARE(plan.at(0).label, QString("a")); QCOMPARE(plan.at(1).label, QString("b")); QCOMPARE(plan.at(2).label, QString("c"));
        a.insert("dependsOn", "c");
        TaskContext context; context.workspaceFolder = root.path(); QString error;
        QVERIFY(configuration({a, b, c}).plan("a", context, &error).isEmpty()); QVERIFY(error.contains(QStringLiteral("循环")));
        QVERIFY(configuration({task("a", "echo")}).plan("missing", context, &error).isEmpty()); QVERIFY(!error.isEmpty());
        auto bad = task("bad", "echo", {"${unsupported}"}); bad.insert("dependsOn", "a");
        QVERIFY(configuration({task("a", "echo"), bad}).plan("bad", context, &error).isEmpty()); QVERIFY(error.contains("unsupported"));
    }
    void variablesEnvironmentAndPlatformOverrides()
    {
        QTemporaryDir root; TaskContext context; context.workspaceFolder = root.path();
        context.file = root.filePath(QStringLiteral("源文件 name.test.cpp")); context.environment.insert("NYA_TASK_TEST", "inherited");
        context.environment.insert("REMOVE_ME", "inherited");
        auto entry = task("echo", "echo", {"${file}", "${fileBasename}", "${fileBasenameNoExtension}", "${relativeFile}", "${env:NYA_TASK_TEST}"});
        entry.insert("options", QJsonObject{{"cwd", "${fileDirname}"}, {"env", QJsonObject{{"NYA_TASK_TEST", "override-${env:NYA_TASK_TEST}"}, {"REMOVE_ME", QJsonValue::Null}}}});
        const QJsonObject override{{"args", QJsonArray{"platform", "${workspaceFolderBasename}"}}, {"options", QJsonObject{{"env", QJsonObject{{"PLATFORM", "yes"}}}}}};
#ifdef Q_OS_WIN
        entry.insert("windows", override);
#elif defined(Q_OS_MAC)
        entry.insert("osx", override);
#else
        entry.insert("linux", override);
#endif
        QString error; auto plan = configuration({entry}).plan("echo", context, &error);
        QVERIFY2(!plan.isEmpty(), qPrintable(error)); QCOMPARE(plan.first().arguments.first(), QString("platform"));
        QCOMPARE(plan.first().workingDirectory, root.path()); QCOMPARE(plan.first().environment.value("NYA_TASK_TEST"), QString("override-inherited"));
        QCOMPARE(plan.first().environment.value("PLATFORM"), QString("yes")); QVERIFY(!plan.first().environment.contains("REMOVE_ME"));
        entry = task("echo", "echo", {"${file}", "${fileBasename}", "${fileBasenameNoExtension}", "${relativeFile}", "${env:NYA_TASK_TEST}"});
        plan = configuration({entry}).plan("echo", context, &error); QVERIFY2(!plan.isEmpty(), qPrintable(error));
        QCOMPARE(plan.first().arguments.mid(2), QStringList({context.file, QStringLiteral("源文件 name.test.cpp"), QStringLiteral("源文件 name.test"), QStringLiteral("源文件 name.test.cpp"), "inherited"}));
        context.file.clear(); QVERIFY(configuration({entry}).plan("echo", context, &error).isEmpty()); QVERIFY(!error.isEmpty());
    }
    void realProcessOutputAndUtf8Chunks()
    {
        QTemporaryDir root;
        auto entry = task("echo", "echo", {QStringLiteral("空 格😀"), "'; $() & \""});
        entry.insert("options", QJsonObject{{"env", QJsonObject{{"NYA_TASK_TEST", "custom value"}}}});
        TaskRunner runner; QString output;
        connect(&runner, &TaskRunner::output, this, [&](const QString &text) { output += text; });
        QSignalSpy done(&runner, &TaskRunner::finished);
        QVERIFY(runner.start(makePlan({entry}, "echo", root.path()))); QVERIFY(!runner.start(makePlan({entry}, "echo", root.path())));
        QTRY_COMPARE(done.count(), 1); QVERIFY(!runner.isRunning()); QCOMPARE(done.first().at(0).toInt(), 0);
        QVERIFY(output.contains("stderr-visible")); QVERIFY(output.contains("custom value")); QVERIFY(output.contains(QStringLiteral("空 格😀")));
        QVERIFY(output.contains(root.path()));
        output.clear(); done.clear();
        QVERIFY(runner.start(makePlan({task("utf8", "utf8")}, "utf8", root.path())));
        QTRY_COMPARE(done.count(), 1); QVERIFY(output.contains(QStringLiteral("中文😀"))); QVERIFY(!output.contains(QChar::ReplacementCharacter));
    }
    void shellCommandsAndQuotedVariables()
    {
        QTemporaryDir root; TaskContext context; context.workspaceFolder = root.path();
        context.file = root.filePath("a ' \" $() ` ; &.txt"); context.environment.insert("VALUE", "a'\" $() ` \\ ; &");
#ifdef Q_OS_WIN
        const QString command = "Write-Output ${workspaceFolder}; Write-Output \"${file}\"; Write-Output '${env:VALUE}'";
#else
        const QString command = "printf '%s\\n' ${workspaceFolder} \"${file}\" '${env:VALUE}'";
#endif
        const auto config = configuration({QJsonObject{{"label", "shell"}, {"command", command}}});
        QString error; const auto plan = config.plan("shell", context, &error); QVERIFY2(!plan.isEmpty(), qPrintable(error));
        TaskRunner runner; QString output; connect(&runner, &TaskRunner::output, this, [&](const QString &text) { output += text; });
        QSignalSpy done(&runner, &TaskRunner::finished); QVERIFY(runner.start(plan)); QTRY_COMPARE(done.count(), 1);
        QCOMPARE(done.first().at(0).toInt(), 0);
        QVERIFY(output.contains(context.workspaceFolder + '\n')); QVERIFY(output.contains(context.file + '\n')); QVERIFY(output.contains(context.environment.value("VALUE") + '\n'));
#ifndef Q_OS_WIN
        output.clear(); done.clear();
        const auto arguments = configuration({QJsonObject{{"label", "args"}, {"command", "printf '%s\\n'"},
            {"args", QJsonArray{"${file}", "${env:VALUE}"}}}}).plan("args", context, &error);
        QVERIFY(runner.start(arguments)); QTRY_COMPARE(done.count(), 1); QCOMPARE(done.first().at(0).toInt(), 0);
        QVERIFY(output.contains(context.file + '\n')); QVERIFY(output.contains(context.environment.value("VALUE") + '\n'));
#endif
    }
    void failureStopsChainAndRunnerCanRestart()
    {
        QTemporaryDir root; const auto sentinel = root.filePath("must-not-exist");
        auto next = task("next", "write", {sentinel}); next.insert("dependsOn", "fail");
        TaskRunner runner; QSignalSpy done(&runner, &TaskRunner::finished);
        QVERIFY(runner.start(makePlan({task("fail", "fail"), next}, "next", root.path())));
        QTRY_COMPARE(done.count(), 1); QCOMPARE(done.first().at(0).toInt(), 7); QVERIFY(!QFileInfo::exists(sentinel));
        auto missing = task("missing", "echo"); missing.insert("command", root.filePath("no-such-executable"));
        done.clear(); QVERIFY(runner.start(makePlan({missing}, "missing", root.path()))); QTRY_COMPARE(done.count(), 1); QCOMPARE(done.first().at(0).toInt(), -1);
        done.clear(); QVERIFY(runner.start(makePlan({task("ok", "write", {sentinel})}, "ok", root.path()))); QTRY_COMPARE(done.count(), 1);
        QCOMPARE(done.first().at(0).toInt(), 0); QVERIFY(QFileInfo::exists(sentinel));
    }
    void stopTerminatesDescendantsAndPendingStart()
    {
        QTemporaryDir root; const auto sentinel = root.filePath("child-must-not-write");
        TaskRunner runner; QString output; connect(&runner, &TaskRunner::output, this, [&](const QString &text) { output += text; });
        QSignalSpy done(&runner, &TaskRunner::finished);
        const auto plan = makePlan({task("child", "child", {sentinel})}, "child", root.path());
        QVERIFY(runner.start(plan)); QTRY_VERIFY(output.contains("child ready")); runner.stop(); QTRY_COMPARE(done.count(), 1);
        QVERIFY(done.first().at(1).toBool()); QTest::qWait(850); QVERIFY(!QFileInfo::exists(sentinel));
        done.clear(); QVERIFY(runner.start(plan)); runner.stop(); QTRY_COMPARE(done.count(), 1); QVERIFY(done.first().at(1).toBool());
    }
    void forceStopAndDestructionCleanUp()
    {
        QTemporaryDir root; const auto sentinel = root.filePath("orphan-must-not-write");
        QString output;
        {
            TaskRunner runner; connect(&runner, &TaskRunner::output, this, [&](const QString &text) { output += text; });
            QVERIFY(runner.start(makePlan({task("child", "child", {sentinel})}, "child", root.path())));
            QTRY_VERIFY(output.contains("child ready"));
        }
        QTest::qWait(850); QVERIFY(!QFileInfo::exists(sentinel));
#ifdef Q_OS_UNIX
        TaskRunner runner; output.clear(); connect(&runner, &TaskRunner::output, this, [&](const QString &text) { output += text; });
        QSignalSpy done(&runner, &TaskRunner::finished);
        QVERIFY(runner.start(makePlan({task("ignore", "ignore-term")}, "ignore", root.path())));
        QTRY_VERIFY(output.contains("ready\n")); runner.stop(); QTRY_COMPARE(done.count(), 1); QVERIFY(done.first().at(1).toBool());
#endif
    }
};

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (app.arguments().value(1) == "--task-fixture") return fixture(app.arguments().mid(2));
    TaskTests tests; return QTest::qExec(&tests, argc, argv);
}
#include "TaskTests.moc"
