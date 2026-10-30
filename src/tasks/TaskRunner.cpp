#include "TaskRunner.h"
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QUuid>
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#include <QTextCodec>
#else
#include <QStringConverter>
#endif
#ifdef Q_OS_UNIX
#include <pwd.h>
#include <signal.h>
#include <unistd.h>
#elif defined(Q_OS_WIN)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace nya {
class TaskOutputDecoder {
public:
    QString decode(const QByteArray &bytes)
    {
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        return m_decoder->toUnicode(bytes);
#else
        return m_decoder(bytes);
#endif
    }
private:
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    std::unique_ptr<QTextDecoder> m_decoder{QTextCodec::codecForName("UTF-8")->makeDecoder()};
#else
    QStringDecoder m_decoder{QStringDecoder::Utf8};
#endif
};

class TaskProcess : public QProcess {
public:
    explicit TaskProcess(QObject *parent) : QProcess(parent)
    {
#if defined(Q_OS_UNIX) && QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        setChildProcessModifier([] { if (::setsid() < 0) ::_exit(127); });
#endif
    }
    ~TaskProcess() override { killTree(true); }
    bool trackProcess()
    {
        m_group = processId();
#ifdef Q_OS_WIN
        m_job = CreateJobObjectW(nullptr, nullptr);
        if (m_job) {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
            limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            const auto process = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, DWORD(m_group));
            const bool attached = process && SetInformationJobObject(m_job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))
                && AssignProcessToJobObject(m_job, process);
            if (process) CloseHandle(process);
            if (!attached) { CloseHandle(m_job); m_job = nullptr; }
        }
#endif
#ifdef Q_OS_WIN
        return m_job != nullptr;
#else
        return true;
#endif
    }
    void killTree(bool force)
    {
#ifdef Q_OS_UNIX
        if (m_group > 0) ::kill(-pid_t(m_group), force ? SIGKILL : SIGTERM);
#elif defined(Q_OS_WIN)
        if (m_job) { TerminateJobObject(m_job, 130); CloseHandle(m_job); m_job = nullptr; }
#endif
        if (state() != QProcess::NotRunning) {
            if (force) kill(); else terminate();
        }
        if (force) m_group = 0;
    }
protected:
#if defined(Q_OS_UNIX) && QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    void setupChildProcess() override { if (::setsid() < 0) ::_exit(127); }
#endif
private:
    qint64 m_group = 0;
#ifdef Q_OS_WIN
    HANDLE m_job = nullptr;
#endif
};

TaskRunner::TaskRunner(QObject *parent) : QObject(parent)
{
    m_killTimer.setSingleShot(true); m_killTimer.setInterval(1500);
    m_environmentTimer.setSingleShot(true); m_environmentTimer.setInterval(8000);
    connect(&m_killTimer, &QTimer::timeout, this, [this] { if (m_process) m_process->killTree(true); });
}

TaskRunner::~TaskRunner()
{
    if (m_process) {
        disconnect(m_process, nullptr, this, nullptr);
        m_process->killTree(true);
        m_process->waitForFinished(1000);
    }
}

bool TaskRunner::start(const QVector<TaskInvocation> &plan)
{
    if (m_running || plan.isEmpty()) return false;
    m_plan = plan; m_index = 0; m_stopping = false; m_running = true;
    emit runningChanged(true);
    // Defer startup so callers can finish updating the output panel first.
    QTimer::singleShot(0, this, &TaskRunner::startNext);
    return true;
}

bool TaskRunner::start(const TaskConfiguration &configuration, const QString &label, const TaskContext &context)
{
    if (m_running || !configuration.ok()) return false;
    m_plan.clear(); m_index = 0; m_stopping = false; m_running = true;
    emit runningChanged(true);
    QTimer::singleShot(0, this, [this, configuration, label, context] {
        if (m_stopping) { finish(130); return; }
        resolveEnvironment(configuration, label, context);
    });
    return true;
}

void TaskRunner::preparePlan(const TaskConfiguration &configuration, const QString &label, TaskContext context)
{
    if (m_stopping) { finish(130); return; }
    QString error;
    m_plan = configuration.plan(label, context, &error);
    if (m_plan.isEmpty()) {
        emit output(error.isEmpty() ? QStringLiteral("任务没有可执行的命令。\n") : error + '\n');
        finish(-1); return;
    }
    QTimer::singleShot(0, this, &TaskRunner::startNext);
}

void TaskRunner::resolveEnvironment(const TaskConfiguration &configuration, const QString &label, const TaskContext &context)
{
#ifdef Q_OS_UNIX
    // GUI launchers do not inherit shell startup files. Read the exported environment
    // once per run, before variable expansion, for both process and shell tasks.
    QString shell = context.environment.value("SHELL");
    if (!QDir::isAbsolutePath(shell) || !QFileInfo(shell).isExecutable()) {
        struct passwd account{}; struct passwd *result = nullptr;
        QByteArray buffer(16 * 1024, '\0');
        if (::getpwuid_r(::geteuid(), &account, buffer.data(), size_t(buffer.size()), &result) == 0 && result && account.pw_shell)
            shell = QString::fromLocal8Bit(account.pw_shell);
    }
    if (!QDir::isAbsolutePath(shell) || !QFileInfo(shell).isExecutable()) {
#ifdef Q_OS_MAC
        shell = QStringLiteral("/bin/zsh");
#else
        shell = QStringLiteral("/bin/sh");
#endif
    }
    emit output(QStringLiteral("正在加载终端环境…\n"));
    const auto marker = QUuid::createUuid().toByteArray(QUuid::Id128);
    const QByteArray begin = marker + "-begin" + QByteArray(1, '\0'), end = marker + "-end" + QByteArray(1, '\0');
    const QString script = QStringLiteral("printf '%s\\000' '%1-begin'; /usr/bin/env -0; printf '%s\\000' '%1-end'")
        .arg(QString::fromLatin1(marker));
    auto *process = new TaskProcess(this); m_process = process;
    process->setWorkingDirectory(QDir::homePath()); process->setProcessEnvironment(context.environment);
    auto bytes = std::make_shared<QByteArray>();
    auto complete = [this, process, bytes, begin, end, configuration, label, context](bool successful) mutable {
        if (m_process != process) return;
        m_environmentTimer.stop(); m_killTimer.stop();
        process->killTree(true); m_process = nullptr; process->deleteLater();
        if (m_stopping) { finish(130); return; }
        const int first = bytes->indexOf(begin), last = first < 0 ? -1 : bytes->indexOf(end, first + begin.size());
        QProcessEnvironment environment;
        if (successful && first >= 0 && last >= 0) {
            const auto entries = bytes->mid(first + begin.size(), last - first - begin.size()).split('\0');
            for (const auto &entry : entries) {
                const auto separator = entry.indexOf('=');
                if (separator > 0) environment.insert(QString::fromLocal8Bit(entry.left(separator)), QString::fromLocal8Bit(entry.mid(separator + 1)));
            }
        }
        TaskContext resolved = context;
        if (environment.contains("PATH")) resolved.environment = environment;
        else emit output(QStringLiteral("未能读取终端环境，使用应用环境继续运行。可在 options.env.PATH 中指定工具路径。\n"));
        preparePlan(configuration, label, resolved);
    };
    connect(process, &QProcess::started, this, [this, process] {
        process->trackProcess(); process->closeWriteChannel();
        if (m_stopping) { process->killTree(false); m_killTimer.start(); }
    });
    connect(process, &QProcess::readyReadStandardOutput, this, [process, bytes] {
        const auto output = process->readAllStandardOutput();
        if (bytes->size() + output.size() <= 1024 * 1024) bytes->append(output);
        else process->killTree(true);
    });
    // Startup messages (including prompts and errors) are not task output and may
    // contain private environment values. Drain stderr without displaying it.
    connect(process, &QProcess::readyReadStandardError, this, [process] { process->readAllStandardError(); });
    connect(process, &QProcess::errorOccurred, this, [complete](QProcess::ProcessError error) mutable {
        if (error == QProcess::FailedToStart) complete(false);
    });
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
        [process, bytes, complete](int code, QProcess::ExitStatus status) mutable {
            const auto remaining = process->readAllStandardOutput();
            if (bytes->size() + remaining.size() <= 1024 * 1024) bytes->append(remaining);
            complete(code == 0 && status == QProcess::NormalExit);
        });
    connect(&m_environmentTimer, &QTimer::timeout, process, [process] { process->killTree(true); });
    m_environmentTimer.start();
    process->start(shell, {QStringLiteral("-ilc"), script});
#else
    preparePlan(configuration, label, context);
#endif
}

void TaskRunner::startNext()
{
    if (!m_running) return;
    if (m_stopping) { finish(130); return; }
    if (m_index >= m_plan.size()) { finish(0); return; }
    const auto task = m_plan.at(m_index++);
    emit taskStarted(task.label);
    emit output(QStringLiteral("\n▶ %1\n%2\n工作目录：%3\n\n").arg(task.label, task.displayCommand, task.workingDirectory));
    if (!QFileInfo(task.workingDirectory).isDir()) {
        emit output(QStringLiteral("无法启动：工作目录不存在：%1\n").arg(task.workingDirectory)); finish(-1); return;
    }
    QString program = task.program;
    if (QDir::isRelativePath(program) && !program.contains('/') && !program.contains('\\')) {
        QStringList paths;
        for (const auto &path : task.environment.value("PATH").split(QDir::listSeparator()))
            paths.append(QDir(task.workingDirectory).absoluteFilePath(path));
        program = QStandardPaths::findExecutable(program, paths);
        if (program.isEmpty()) {
            emit output(QStringLiteral("无法启动：PATH 中未找到可执行文件：%1\n请确认已安装该工具，或在 task.json 中使用绝对路径 / options.env.PATH 指定工具目录。\n").arg(task.program)); finish(-1); return;
        }
    }
    auto *process = new TaskProcess(this); m_process = process;
    m_decoder = std::make_unique<TaskOutputDecoder>();
    process->setProcessChannelMode(QProcess::MergedChannels);
    process->setWorkingDirectory(task.workingDirectory); process->setProcessEnvironment(task.environment);
    connect(process, &QProcess::readyReadStandardOutput, this, &TaskRunner::readOutput);
    connect(process, &QProcess::started, this, [this, process] {
        if (!process->trackProcess()) {
            emit output(QStringLiteral("无法管理任务进程组，任务已终止。\n")); process->killTree(true); return;
        }
        process->closeWriteChannel();
        if (m_stopping) { process->killTree(false); m_killTimer.start(); }
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (process != m_process || error != QProcess::FailedToStart) return;
        emit output(QStringLiteral("无法启动：%1\n").arg(process->errorString()));
        m_process = nullptr; process->deleteLater(); finish(-1);
    });
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
        [this, process](int code, QProcess::ExitStatus status) {
            if (process != m_process) return;
            readOutput(); m_killTimer.stop(); process->killTree(true);
            m_process = nullptr; process->deleteLater();
            const int exitCode = status == QProcess::NormalExit ? code : -1;
            emit output(QStringLiteral("\n■ 退出码：%1%2\n").arg(exitCode).arg(status == QProcess::CrashExit && !m_stopping ? QStringLiteral("（进程异常退出）") : QString()));
            if (m_stopping || exitCode != 0) finish(m_stopping ? 130 : exitCode);
            else QTimer::singleShot(0, this, &TaskRunner::startNext);
        });
    process->start(program, task.arguments);
}

void TaskRunner::readOutput()
{
    if (!m_process || !m_decoder) return;
    // Keep a single event-loop turn bounded even when a compiler emits a large log.
    while (m_process->bytesAvailable() > 0) {
        const auto text = m_decoder->decode(m_process->read(64 * 1024));
        if (!text.isEmpty()) emit output(text);
    }
}

void TaskRunner::stop()
{
    if (!m_running || m_stopping) return;
    m_stopping = true; emit output(QStringLiteral("\n正在停止任务…\n"));
    if (m_process) { m_process->killTree(false); m_killTimer.start(); }
}

void TaskRunner::finish(int exitCode)
{
    if (!m_running) return;
    m_killTimer.stop(); m_environmentTimer.stop(); m_running = false;
    const bool canceled = m_stopping;
    m_plan.clear(); m_decoder.reset();
    emit output(canceled ? QStringLiteral("\n任务已停止。\n") : exitCode == 0 ? QStringLiteral("\n任务全部完成。\n") : QStringLiteral("\n任务失败，已停止后续步骤。\n"));
    emit runningChanged(false); emit finished(exitCode, canceled);
}
}
