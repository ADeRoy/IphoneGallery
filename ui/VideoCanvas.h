#ifndef VIDEOCANVAS_H
#define VIDEOCANVAS_H

#include <QImage>
#include <QWidget>

class QVideoSink;
class QVideoFrame;

class VideoCanvas : public QWidget
{
    Q_OBJECT
public:
    explicit VideoCanvas(QWidget* parent = nullptr);

    QVideoSink* videoSink() const { return _sink; }
    void clearFrame();
    bool hasFrame() const { return !_image.isNull(); }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void onVideoFrame(const QVideoFrame& frame);

    QVideoSink* _sink{nullptr};
    QImage _image;
    int _frameRotation{0};
    int _surfaceRotation{0};
    bool _surfaceMirrored{false};
    int _frameCount{0};
};

#endif // VIDEOCANVAS_H
