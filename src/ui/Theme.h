#pragma once
#include <QColor>
#include <QIcon>

class QApplication;
namespace nya {
class Theme {
public:
    static void apply(QApplication &app, bool dark);
    static QIcon icon(const QString &name, const QColor &color = QColor("#a9b6c8"));
    static QIcon appIcon();
};
}
