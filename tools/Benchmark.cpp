#include "Editor.h"
#include "FileIO.h"
#include "Search.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QTextStream>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    const QString row = QStringLiteral("NyaEdit 你好 | item=0123456789 | a small native text editor\n");
    const QString text = row.repeated(150000);
    QTemporaryDir dir;
    QString error;
    QTextStream out(stdout);
    QElapsedTimer timer;
    out << "Qt " << qVersion() << "; UTF-8 bytes=" << text.toUtf8().size() << "; lines=150000\n";
    timer.start();
    if (!nya::saveFile(dir.filePath("bench.txt"), text, {}, nya::LineEnding::CRLF, &error)) { out << error; return 1; }
    out << "encode + atomic save (CRLF): " << timer.elapsed() << " ms\n";
    timer.restart(); const auto file = nya::loadFile(dir.filePath("bench.txt"));
    if (!file.ok() || file.text != text) return 2;
    out << "read + detect + decode + normalize: " << timer.elapsed() << " ms\n";
    timer.restart(); const auto matches = nya::Search::find(text, {"item"});
    out << "literal search (navigation cap " << matches.matches.size() << "): " << timer.elapsed() << " ms\n";
    int count = 0; timer.restart();
    const auto replaced = nya::Search::replaceAll(text, {"item"}, "entry", &count, &error);
    if (count != 150000 || replaced.isEmpty() || !error.isEmpty()) return 3;
    out << "replace all (" << count << " matches): " << timer.elapsed() << " ms\n";
    nya::Editor editor; editor.largeFile = true;
    timer.restart(); editor.setPlainText(file.text);
    out << "Qt document population (no syntax): " << timer.elapsed() << " ms\n";
    return 0;
}
