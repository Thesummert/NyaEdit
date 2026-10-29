// Developer utility: render both themes without writing the user's editor preferences.
#include "Editor.h"
#include "FontSettingsDialog.h"
#include "AnimationSettingsDialog.h"
#include "MainWindow.h"
#include "Theme.h"
#include <QAction>
#include <QApplication>
#include <QBuffer>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QMenu>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>

static bool renderTabContextMenu(nya::MainWindow &window, const QString &output)
{
    auto *bar = window.findChild<QTabWidget *>()->tabBar();
    bool saved = false;
    QTimer capture; capture.setSingleShot(true);
    QObject::connect(&capture, &QTimer::timeout, &window, [&] {
        if (auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget())) {
            menu->setActiveAction(menu->actions().first()); saved = menu->grab().save(output); menu->close();
        }
    });
    capture.start(0);
    const auto position = bar->tabRect(0).center();
    QMetaObject::invokeMethod(bar, "customContextMenuRequested", Qt::DirectConnection, Q_ARG(QPoint, position));
    return saved;
}

// Export native icon assets from the same resource used by the application.
static bool exportIcons(const QString &output)
{
    const auto icon = nya::Theme::appIcon();
    if (!icon.pixmap(512, 512).save(output + "/logo.png")) return false;
    const QString iconset = output + "/NyaEdit.iconset";
    if (!QDir().mkpath(iconset)) return false;
    for (int size : {16, 32, 128, 256, 512}) {
        if (!icon.pixmap(size, size).save(iconset + QStringLiteral("/icon_%1x%1.png").arg(size))) return false;
        if (!icon.pixmap(size * 2, size * 2).save(iconset + QStringLiteral("/icon_%1x%1@2x.png").arg(size))) return false;
    }
    // ICO supports PNG payloads; write all sizes without an external image conversion dependency.
    const QVector<int> sizes{16, 24, 32, 48, 64, 128, 256};
    QVector<QByteArray> images;
    for (int size : sizes) {
        QByteArray image;
        QBuffer buffer(&image);
        if (!buffer.open(QIODevice::WriteOnly) || !icon.pixmap(size, size).save(&buffer, "PNG")) return false;
        images.append(image);
    }
    QFile file(output + "/logo.ico");
    if (!file.open(QIODevice::WriteOnly)) return false;
    QDataStream stream(&file); stream.setByteOrder(QDataStream::LittleEndian);
    stream << quint16(0) << quint16(1) << quint16(sizes.size());
    quint32 offset = 6 + 16 * quint32(sizes.size());
    for (int i = 0; i < sizes.size(); ++i) {
        const auto size = quint8(sizes[i] == 256 ? 0 : sizes[i]);
        stream << size << size << quint8(0) << quint8(0) << quint16(1) << quint16(32)
               << quint32(images[i].size()) << offset;
        offset += quint32(images[i].size());
    }
    for (const auto &image : images) stream.writeRawData(image.constData(), int(image.size()));
    if (stream.status() != QDataStream::Ok) return false;
    file.close();
    // Modern ICNS stores PNG representations in big-endian typed chunks.
    const QVector<int> macSizes{16, 32, 64, 128, 256, 512, 1024};
    const QVector<QByteArray> types{"icp4", "icp5", "icp6", "ic07", "ic08", "ic09", "ic10"};
    QVector<QByteArray> macImages;
    quint32 total = 8;
    for (int size : macSizes) {
        QByteArray image;
        QBuffer buffer(&image);
        if (!buffer.open(QIODevice::WriteOnly) || !icon.pixmap(size, size).save(&buffer, "PNG")) return false;
        total += 8 + quint32(image.size());
        macImages.append(image);
    }
    QFile macFile(output + "/logo.icns");
    if (!macFile.open(QIODevice::WriteOnly)) return false;
    QDataStream macStream(&macFile); macStream.setByteOrder(QDataStream::BigEndian);
    macStream.writeRawData("icns", 4); macStream << total;
    for (int i = 0; i < macImages.size(); ++i) {
        macStream.writeRawData(types[i].constData(), 4);
        macStream << quint32(macImages[i].size() + 8);
        macStream.writeRawData(macImages[i].constData(), int(macImages[i].size()));
    }
    return macStream.status() == QDataStream::Ok;
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir fixture;
    app.setOrganizationName("NyaEditPreview"); app.setApplicationName("NyaEditPreview");
    const QString output = QDir::current().absoluteFilePath(argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString("build/previews"));
    if (!QDir().mkpath(output)) return 1;
    if (!exportIcons(output)) return 1;
    const QString root = fixture.filePath("NyaEdit"); QDir().mkpath(root);
    const QString source = QStringLiteral(R"(// NyaEdit · 轻盈落笔，专注文本。
// A small native editor. A little more room to think.

#include <iostream>
#include <string>
#include <vector>

namespace nya {

struct Document {
    std::string name;
    std::string encoding = "UTF-8";
    bool modified = false;
};

// Every good idea begins with a blank page.
void welcome(const std::string& name) {
    const std::vector<std::string> features = {
        "自动识别编码，安心打开每一份文档",
        "查找与替换，让重复的工作更简单",
        "原生 C++ 核心，轻快而专注"
    };

    std::cout << "Hello, " << name << '\n';
    for (const auto& feature : features) {
        std::cout << "  → " << feature << '\n';
    }
}

} // namespace nya

int main() {
    nya::welcome("NyaEdit");
    return 0;
}
)");
    QString error;
    if (!nya::saveFile(root + "/welcome.cpp", source, {}, nya::LineEnding::LF, &error)) return 1;
    nya::saveFile(root + "/settings.json", "{\n  \"editor\": \"NyaEdit\",\n  \"theme\": \"dark\"\n}\n", {}, nya::LineEnding::LF, &error);
    nya::saveFile(root + "/notes.md", "# NyaEdit\n\nMake space for your next idea.\n", {}, nya::LineEnding::LF, &error);
    const auto paths = nya::StoragePaths::underDirectory(fixture.path());
    {
        nya::AppStorage storage(paths);
        if (!storage.initialize() || !storage.record(root, nya::RecentKind::Folder)
            || !storage.record(root + "/notes.md", nya::RecentKind::File)
            || !storage.record(root + "/welcome.cpp", nya::RecentKind::File)) return 1;
    }
    nya::MainWindow window(nullptr, paths);
    window.resize(1280, 960); window.show();
    QTimer::singleShot(300, &app, [&] {
        window.grab().save(output + "/welcome.png");
        window.openFolder(root); window.openPath(root + "/welcome.cpp");
    });
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, &app, [&] {
        auto *tabs = window.findChild<QTabWidget *>();
        auto *editor = qobject_cast<nya::Editor *>(tabs->currentWidget());
        if (!editor || editor->loading || tabs->count() != 1) return;
        poll.stop();
        window.findChild<QAction *>("actionFind")->trigger();
        window.findChild<QLineEdit *>("findInput")->setText("std");
        QTimer::singleShot(500, &app, [&] {
            window.grab().save(output + "/dark.png");
            window.findChild<QAction *>("actionTheme")->trigger();
            QTimer::singleShot(300, &app, [&] {
                window.grab().save(output + "/light.png");
                window.findChild<QAction *>("actionTheme")->trigger();
                auto *input = window.findChild<QLineEdit *>("fileSearchInput");
                input->setFocus(); input->setText("welcome");
                QTimer::singleShot(600, &app, [&] {
                    window.grab().save(output + "/file-search.png");
                    auto *tabs = window.findChild<QTabWidget *>();
                    auto *editor = qobject_cast<nya::Editor *>(tabs->currentWidget());
                    auto *dialog = new nya::FontSettingsDialog(editor->editorFont(), &window);
                    dialog->show();
                    QTimer::singleShot(200, &app, [&, dialog, editor] {
                        dialog->grab().save(output + "/font-settings.png"); dialog->hide(); dialog->deleteLater();
                        editor->setFocus();
                        auto cursor = editor->textCursor();
                        const int start = editor->contents().indexOf("    const std::vector");
                        cursor.setPosition(start); cursor.setPosition(start + 170, QTextCursor::KeepAnchor); editor->setTextCursor(cursor);
                        QTimer::singleShot(250, &app, [&, editor] {
                            window.grab().save(output + "/dark-selection.png");
                            window.findChild<QAction *>("actionTheme")->trigger();
                            QTimer::singleShot(250, &app, [&, editor] {
                                window.grab().save(output + "/light-selection.png");
                                auto *animationDialog = new nya::AnimationSettingsDialog(editor->animationSettings(), &window);
                                animationDialog->show();
                                QTimer::singleShot(200, &app, [&, animationDialog] {
                                    animationDialog->grab().save(output + "/animation-settings.png"); animationDialog->hide();
                                    if (!renderTabContextMenu(window, output + "/context-menu-light.png")) { app.exit(3); return; }
                                    window.findChild<QAction *>("actionTheme")->trigger();
                                    if (!renderTabContextMenu(window, output + "/context-menu-dark.png")) { app.exit(3); return; }
                                    app.quit();
                                });
                            });
                        });
                    });
                });
            });
        });
    });
    poll.start(50);
    QTimer::singleShot(15000, &app, [&] { app.exit(2); });
    return app.exec();
}
