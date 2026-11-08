#pragma once
#include <QImage>
#include <QString>
#include <QFont>
#include <QColor>
#include <QList>

namespace nya {
struct MathExpression { QString token; QString source; QString original; bool display; };
QString extractMarkdownMath(const QString &source, QList<MathExpression> &expressions);
void resetMarkdownMath();
QImage renderMarkdownMath(const QString &source, const QFont &font, const QColor &color, bool display = false);
}
