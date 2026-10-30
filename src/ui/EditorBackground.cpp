#include "EditorBackground.h"
#include <QApplication>
#include <QFileInfo>
#include <QImageReader>
#include <QPainter>
#include <QVector>
#include <cmath>

namespace nya {
namespace {
// Separable Gaussian convolution on premultiplied pixels keeps transparent edges clean.
QImage gaussianBlur(QImage image, double sigma)
{
    image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const int radius = int(std::ceil(3 * sigma));
    QVector<double> weights(2 * radius + 1);
    double sum = 0;
    for (int i = -radius; i <= radius; ++i) {
        const double weight = std::exp(-i * i / (2 * sigma * sigma));
        weights[i + radius] = weight; sum += weight;
    }
    for (double &weight : weights) weight /= sum;
    QImage temporary(image.size(), image.format());
    if (temporary.isNull()) return {};
    for (int pass = 0; pass < 2; ++pass) {
        for (int y = 0; y < image.height(); ++y) {
            auto *output = reinterpret_cast<QRgb *>(temporary.scanLine(y));
            for (int x = 0; x < image.width(); ++x) {
                double red = 0, green = 0, blue = 0, alpha = 0;
                for (int i = -radius; i <= radius; ++i) {
                    const int sx = pass == 0 ? qBound(0, x + i, image.width() - 1) : x;
                    const int sy = pass == 1 ? qBound(0, y + i, image.height() - 1) : y;
                    const QRgb pixel = reinterpret_cast<const QRgb *>(image.constScanLine(sy))[sx];
                    const double weight = weights[i + radius];
                    red += qRed(pixel) * weight; green += qGreen(pixel) * weight;
                    blue += qBlue(pixel) * weight; alpha += qAlpha(pixel) * weight;
                }
                output[x] = qRgba(qRound(red), qRound(green), qRound(blue), qRound(alpha));
            }
        }
        image.swap(temporary);
    }
    return image;
}
}

EditorBackground::EditorBackground(QObject *parent) : QObject(parent)
{
    // Debounce image writes / atomic replacements, which can emit several file events.
    m_reloadTimer.setSingleShot(true); m_reloadTimer.setInterval(120);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] { m_reloadTimer.start(); });
    connect(&m_reloadTimer, &QTimer::timeout, this, [this] {
        if (m_settings.path.isEmpty()) return;
        if (!loadImage()) { clearUnavailable(); return; }
        watchFile(); prepareImage(); emit changed();
    });
    // Also catches lost parent directories, removed symlink targets and missed watch events.
    m_checkTimer.setInterval(2000);
    connect(&m_checkTimer, &QTimer::timeout, this, &EditorBackground::checkFile);
    connect(qApp, &QApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if (state == Qt::ApplicationActive) checkFile();
    });
}

bool EditorBackground::setSettings(BackgroundSettings settings)
{
    settings.opacity = qBound(0, settings.opacity, 100);
    settings.blur = qBound(0, settings.blur, 40);
    if (!settings.path.isEmpty()) settings.path = QFileInfo(settings.path).absoluteFilePath();
    const bool pathChanged = settings.path != m_settings.path;
    const bool blurChanged = settings.blur != m_settings.blur;
    m_settings = settings;
    if (!settings.path.isEmpty()) {
        const QFileInfo info(settings.path);
        if (!info.isFile() || !info.isReadable()) { clearUnavailable(); return false; }
    }
    if (pathChanged || m_source.isNull()) {
        m_reloadTimer.stop();
        if (settings.path.isEmpty()) { m_source = {}; m_image = {}; }
        else if (!loadImage()) { clearUnavailable(); return false; }
        watchFile();
    }
    if (pathChanged || blurChanged || m_image.isNull()) prepareImage();
    if (m_settings.path.isEmpty()) { m_checkTimer.stop(); }
    else { m_checkTimer.start(); }
    emit changed();
    return true;
}

bool EditorBackground::loadImage()
{
    const QFileInfo info(m_settings.path);
    if (!info.isFile() || !info.isReadable()) return false;
    QImageReader reader(m_settings.path); reader.setAutoTransform(true);
    const QSize size = reader.size();
    // Bound the persistent source and filter buffers even for very large photographs.
    if (size.isValid() && qMax(size.width(), size.height()) > 2048)
        reader.setScaledSize(size.scaled(2048, 2048, Qt::KeepAspectRatio));
    QImage image = reader.read();
    if (image.isNull()) return false;
    if (qMax(image.width(), image.height()) > 2048)
        image = image.scaled(2048, 2048, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    m_source = image;
    m_modified = info.lastModified(); m_fileSize = info.size();
    return true;
}

void EditorBackground::prepareImage()
{
    if (m_source.isNull()) { m_image = {}; return; }
    QImage image = m_source;
    if (m_settings.blur > 0) {
        // Filter at a reduced resolution for larger radii, then let the painter upscale.
        // Strength is relative to a 1024-pixel image, so it is stable across source sizes.
        const double sigma = m_settings.blur * qMax(image.width(), image.height()) / 1024.0;
        const double reduction = qMax(1.0, sigma / 2.0);
        const QSize size(qMax(1, qRound(image.width() / reduction)), qMax(1, qRound(image.height() / reduction)));
        image = image.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        image = gaussianBlur(image, sigma / reduction);
    }
    m_image = QPixmap::fromImage(image);
}

void EditorBackground::watchFile()
{
    if (!m_watcher.files().isEmpty()) m_watcher.removePaths(m_watcher.files());
    if (!m_settings.path.isEmpty()) m_watcher.addPath(m_settings.path);
}

void EditorBackground::checkFile()
{
    if (m_settings.path.isEmpty()) return;
    const QFileInfo info(m_settings.path);
    if (!info.isFile() || !info.isReadable()) clearUnavailable();
    else if ((info.lastModified() != m_modified || info.size() != m_fileSize) && !m_reloadTimer.isActive())
        m_reloadTimer.start();
}

void EditorBackground::clearUnavailable()
{
    m_settings.path.clear(); m_source = {}; m_image = {};
    m_checkTimer.stop(); m_reloadTimer.stop(); watchFile();
    emit changed(); emit fileUnavailable();
}

void EditorBackground::paint(QPainter &painter, const QRect &rect) const
{
    if (!isActive() || rect.isEmpty()) return;
    const double scale = qMax(double(rect.width()) / m_image.width(), double(rect.height()) / m_image.height());
    const QSizeF size(m_image.width() * scale, m_image.height() * scale);
    const QRectF target(QPointF(rect.x() + (rect.width() - size.width()) / 2,
                               rect.y() + (rect.height() - size.height()) / 2), size);
    painter.save(); painter.setClipRect(rect, Qt::IntersectClip);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.setOpacity(m_settings.opacity / 100.0); painter.drawPixmap(target, m_image, m_image.rect());
    painter.restore();
}
}
