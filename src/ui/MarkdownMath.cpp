#include "MarkdownMath.h"
#include <QPainter>
#include <QFile>
#include <QJSEngine>
#include <QSvgRenderer>
#include <QUuid>
#include <cmath>

static void initializeMathResources() { Q_INIT_RESOURCE(mathjax); }

namespace nya {
QString extractMarkdownMath(const QString &source, QList<MathExpression> &expressions)
{
    QString result;
    bool fence = false;
    QChar fenceChar;
    int fenceLength = 0;
    int inlineTicks = 0;
    for (int i = 0; i < source.size();) {
        const bool lineStart = i == 0 || source[i - 1] == '\n';
        if (lineStart) {
            if (!fence && !inlineTicks && (source.mid(i, 4) == QStringLiteral("    ") || source[i] == '\t')) {
                int lineEnd = source.indexOf('\n', i);
                if (lineEnd < 0) lineEnd = source.size();
                result += source.mid(i, lineEnd - i); i = lineEnd; continue;
            }
            int p = i;
            while (p < source.size() && p - i < 3 && source[p] == ' ') ++p;
            if (p < source.size() && (source[p] == '`' || source[p] == '~')) {
                int end = p; while (end < source.size() && source[end] == source[p]) ++end;
                int count = end - p;
                if (count >= 3 && (!fence || (source[p] == fenceChar && count >= fenceLength))) {
                    if (!fence) { fence = true; fenceChar = source[p]; fenceLength = count; }
                    else fence = false;
                    int lineEnd = source.indexOf('\n', end);
                    if (lineEnd < 0) lineEnd = source.size();
                    result += source.mid(i, lineEnd - i); i = lineEnd; continue;
                }
            }
        }
        if (!fence && source[i] == '`') {
            int end = i; while (end < source.size() && source[end] == '`') ++end;
            int count = end - i;
            if (!inlineTicks) inlineTicks = count;
            else if (inlineTicks == count) inlineTicks = 0;
            result += source.mid(i, count); i = end; continue;
        }
        if (!fence && !inlineTicks && source[i] == '\\' && i + 1 < source.size()
            && (source[i + 1] == '(' || source[i + 1] == '[')) {
            const bool display = source[i + 1] == '[';
            const QString closing = display ? QStringLiteral("\\]") : QStringLiteral("\\)");
            const int end = source.indexOf(closing, i + 2);
            if (end > i + 2 && end - i <= 16386
                && (display || !source.mid(i + 2, end - i - 2).contains('\n'))) {
                MathExpression expression;
                expression.token = QStringLiteral("NYAMATH%1TOKEN").arg(QUuid::createUuid().toString(QUuid::Id128));
                expression.source = source.mid(i + 2, end - i - 2);
                expression.original = source.mid(i, end + 2 - i);
                expression.display = display;
                expressions.append(expression);
                result += expression.token;
                i = end + 2; continue;
            }
        }
        if (source[i] == '\\' && i + 1 < source.size()) {
            result += source.mid(i, 2); i += 2; continue;
        }
        if (!fence && !inlineTicks && source[i] == '$') {
            const bool display = i + 1 < source.size() && source[i + 1] == '$';
            const QString delimiter = display ? QStringLiteral("$$") : QStringLiteral("$");
            const int start = i + delimiter.size();
            int end = start;
            for (; end < source.size(); ++end) {
                if (!display && source[end] == '\n') break;
                if (source[end] == '\\') { ++end; continue; }
                if (source.mid(end, delimiter.size()) == delimiter
                    && (display || end + 1 == source.size() || source[end + 1] != '$')) break;
            }
            if (end < source.size() && source.mid(end, delimiter.size()) == delimiter
                && end > start && end - start <= 16384 && !source.mid(start, end - start).trimmed().isEmpty()) {
                MathExpression expression;
                expression.token = QStringLiteral("NYAMATH%1TOKEN").arg(QUuid::createUuid().toString(QUuid::Id128));
                expression.source = source.mid(start, end - start);
                expression.original = source.mid(i, end + delimiter.size() - i);
                expression.display = display;
                expressions.append(expression);
                result += expression.token;
                i = end + delimiter.size(); continue;
            }
        }
        result += source[i++];
    }
    return result;
}

namespace {
QJSEngine &mathEngine()
{
    static QJSEngine engine;
    static bool initialized = false;
    if (!initialized) {
        initialized = true;
        initializeMathResources();
        engine.evaluate(QStringLiteral("var globalThis = this; var console = {log:function(){}, warn:function(){}, error:function(){}};"));
        QFile script(QStringLiteral(":/mathjax/tex-svg.js"));
        if (script.open(QIODevice::ReadOnly)) engine.evaluate(QString::fromUtf8(script.readAll()));
    }
    return engine;
}
}
void resetMarkdownMath()
{
    auto &engine = mathEngine();
    QJSValue reset = engine.globalObject().property(QStringLiteral("nyaReset"));
    if (reset.isCallable()) reset.call();
}
QImage renderMarkdownMath(const QString &source, const QFont &font, const QColor &color, bool display)
{
    auto &engine = mathEngine();
    QJSValue render = engine.globalObject().property(QStringLiteral("nyaRender"));
    if (!render.isCallable()) return {};
    const QJSValue result = render.call({source, display});
    if (result.isError() || !result.isString()) return {};
    QString svg = result.toString();
    if (svg.contains(QStringLiteral("data-mjx-error"))) return {};
    svg.replace(QStringLiteral("currentColor"), color.name(QColor::HexRgb));
    QSvgRenderer renderer(svg.toUtf8());
    const QRectF bounds = renderer.viewBoxF();
    if (!renderer.isValid() || bounds.isEmpty()) return {};
    const qreal size = font.pointSizeF() > 0 ? font.pointSizeF() * 96.0 / 72.0 : 16.0;
    const qreal width = bounds.width() * size / 1000.0;
    const qreal height = bounds.height() * size / 1000.0;
    if (width > 8192 || height > 4096 || width <= 0 || height <= 0) return {};
    const qreal scale = 2;
    QImage image(qMax(1, int(std::ceil(width * scale))), qMax(1, int(std::ceil(height * scale))), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    image.setDevicePixelRatio(scale);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    renderer.render(&painter, QRectF(0, 0, width, height));
    return image;
}
}
