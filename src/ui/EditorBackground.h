#pragma once
#include <QDateTime>
#include <QFileSystemWatcher>
#include <QImage>
#include <QObject>
#include <QPixmap>
#include <QTimer>

class QPainter;
namespace nya {
struct BackgroundSettings {
    QString path;
    int opacity = 20;
    int blur = 0;
};

// Shared by all document views: decoding and Gaussian filtering never run while typing.
class EditorBackground : public QObject {
    Q_OBJECT
public:
    explicit EditorBackground(QObject *parent = nullptr);
    BackgroundSettings settings() const { return m_settings; }
    bool isActive() const { return !m_image.isNull() && m_settings.opacity > 0; }
    bool setSettings(BackgroundSettings settings);
    void checkFile();
    void paint(QPainter &painter, const QRect &rect) const;
signals:
    void changed();
    void fileUnavailable();
private:
    bool loadImage();
    void prepareImage();
    void clearUnavailable();
    void watchFile();
    BackgroundSettings m_settings;
    QImage m_source;
    QPixmap m_image;
    QDateTime m_modified;
    qint64 m_fileSize = 0;
    QFileSystemWatcher m_watcher;
    QTimer m_checkTimer;
    QTimer m_reloadTimer;
};
}
