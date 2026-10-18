#include "VideoCanvas.h"

#include <QDebug>
#include <QDir>
#include <QPainter>
#include <QVideoFrame>
#include <QVideoSink>

VideoCanvas::VideoCanvas(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setAutoFillBackground(false);
    setMinimumSize(200, 160);

    _sink = new QVideoSink(this);
    connect(_sink, &QVideoSink::videoFrameChanged, this,
            [this](const QVideoFrame& frame) { onVideoFrame(frame); });
}

void VideoCanvas::clearFrame()
{
    _image = QImage();
    update();
}

void VideoCanvas::onVideoFrame(const QVideoFrame& frame)
{
    if (!frame.isValid())
    {
        return;
    }
    const QImage image = frame.toImage();
    if (image.isNull())
    {
        qDebug() << "video frame conversion failed" << frame.pixelFormat();
        return;
    }
    _frameRotation = static_cast<int>(frame.rotation()) % 360;
    if (_frameRotation < 0)
    {
        _frameRotation += 360;
    }
    const QVideoFrameFormat format = frame.surfaceFormat();
    _surfaceRotation = static_cast<int>(format.rotation()) % 360;
    if (_surfaceRotation < 0)
    {
        _surfaceRotation += 360;
    }
    _surfaceMirrored = format.isMirrored();
    if (_image.isNull() && qEnvironmentVariableIsSet("IPHONE_GALLERY_DEBUG"))
    {
        qDebug() << "video first frame" << frame.size() << "frame rotation"
                 << _frameRotation << "surface rotation" << _surfaceRotation
                 << "surface mirrored" << _surfaceMirrored << "image" << image.size();
    }
    ++_frameCount;
    if (qEnvironmentVariableIsSet("IPHONE_GALLERY_DEBUG")
        && (_frameCount == 20 || _frameCount == 50))
    {
        image.save(QDir::tempPath()
                       + QStringLiteral("/IphoneGallery_frame_%1.png").arg(_frameCount),
                   "PNG");
    }
    _image = image;
    update();
}

void VideoCanvas::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.fillRect(rect(), QColor(0, 0, 0));
    if (_image.isNull())
    {
        return;
    }
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    int rotation = _frameRotation % 360;
    if (_surfaceMirrored && (_surfaceRotation == 90 || _surfaceRotation == 270))
    {
        rotation = (rotation + 180) % 360;
    }
    const bool swapped = rotation == 90 || rotation == 270;
    const double displayWidth = swapped ? _image.height() : _image.width();
    const double displayHeight = swapped ? _image.width() : _image.height();
    const double scale = qMin(static_cast<double>(width()) / displayWidth,
                              static_cast<double>(height()) / displayHeight);

    painter.translate(width() / 2.0, height() / 2.0);
    painter.rotate(rotation);
    painter.scale(scale, scale);
    painter.drawImage(QPointF(-_image.width() / 2.0, -_image.height() / 2.0), _image);
}
