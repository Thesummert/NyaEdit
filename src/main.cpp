#include "MainWindow.h"
#include "Theme.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileOpenEvent>
#include <QPointer>
#include <QTimer>
#include <QTemporaryDir>

class NyaApplication : public QApplication {
public:
    using QApplication::QApplication;
    QPointer<nya::MainWindow> window;
    QStringList pendingFiles;
protected:
    bool event(QEvent *event) override
    {
        if (event->type() == QEvent::FileOpen) {
            const auto path = static_cast<QFileOpenEvent *>(event)->file();
            if (window) window->openPath(path); else pendingFiles.append(path);
            return true;
        }
        return QApplication::event(event);
    }
};

int main(int argc, char *argv[])
{
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif
    NyaApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("NyaEdit"));
    app.setApplicationName(QStringLiteral("NyaEdit"));
    app.setApplicationVersion(QStringLiteral("0.1.0"));
    app.setWindowIcon(nya::Theme::appIcon());
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("NyaEdit — 轻量原生文本编辑器"));
    parser.addHelpOption(); parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("files"), QStringLiteral("要打开的文件或文件夹"), QStringLiteral("[files…]"));
    QCommandLineOption smoke(QStringLiteral("smoke-test"), QStringLiteral("启动界面并在 1 秒后退出（验证运行环境）"));
    parser.addOption(smoke);
    parser.process(app);
    QTemporaryDir smokeStorage;
    const auto paths = parser.isSet(smoke) ? nya::StoragePaths::underDirectory(smokeStorage.path()) : nya::StoragePaths{};
    nya::MainWindow window(nullptr, paths);
    app.window = &window; window.show();
    for (const auto &file : parser.positionalArguments() + app.pendingFiles) window.openPath(QDir::current().absoluteFilePath(file));
    if (parser.isSet(smoke)) QTimer::singleShot(1000, &app, &QCoreApplication::quit);
    return app.exec();
}
