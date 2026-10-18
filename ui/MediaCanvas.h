#ifndef MEDIACANVAS_H
#define MEDIACANVAS_H

#include <QImage>
#include <QPointF>
#include <QWidget>

class QTimer;

class MediaCanvas : public QWidget
{
    Q_OBJECT
public:
    explicit MediaCanvas(QWidget* parent = nullptr);
    ~MediaCanvas() override;

    void setImage(const QImage& image);
    void clearImage();
    void setLoading(bool loading);
    void setMessage(const QString& message);
    bool isLoading() const { return _loading; }

    void resetView();
    void setActualSize();
    void zoomBy(double factor);
    bool hasImage() const { return !_image.isNull(); }
    double zoom() const { return _scale; }

signals:
    void zoomChanged(double zoom);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    double fitScale() const;
    QPointF imageTopLeft() const;
    QRectF imageRect() const;
    void updateCursor();

    QImage _image;
    QString _message;
    double _scale{1.0};
    QPointF _offset;
    bool _fit{true};
    bool _dragging{false};
    QPoint _lastMousePos;
    bool _loading{false};
    QTimer* _spinnerTimer{nullptr};
    int _spinnerAngle{0};
};

#endif // MEDIACANVAS_H
