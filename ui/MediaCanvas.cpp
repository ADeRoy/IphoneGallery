#include "MediaCanvas.h"

#include <QDebug>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QWheelEvent>
#include <QtMath>

MediaCanvas::MediaCanvas(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(200, 160);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent, true);

    _spinnerTimer = new QTimer(this);
    _spinnerTimer->setInterval(16);
    connect(_spinnerTimer, &QTimer::timeout, this, [this]() {
        _spinnerAngle = (_spinnerAngle + 5) % 360;
        update();
    });
}

MediaCanvas::~MediaCanvas() = default;

void MediaCanvas::setImage(const QImage& image)
{
    _image = image;
    qDebug() << "canvas setImage" << image.size() << "widget" << size() << "dpr"
             << devicePixelRatioF() << "fit" << fitScale();
    _message.clear();
    _loading = false;
    if (_spinnerTimer->isActive())
    {
        _spinnerTimer->stop();
    }
    updateCursor();
    resetView();
}

void MediaCanvas::clearImage()
{
    _image = QImage();
    _message.clear();
    update();
}

void MediaCanvas::setMessage(const QString& message)
{
    _message = message;
    _loading = false;
    if (_spinnerTimer->isActive())
    {
        _spinnerTimer->stop();
    }
    update();
}

void MediaCanvas::setLoading(bool loading)
{
    if (_loading == loading)
    {
        return;
    }
    _loading = loading;
    if (loading)
    {
        _spinnerTimer->start();
    }
    else
    {
        _spinnerTimer->stop();
    }
    update();
}

double MediaCanvas::fitScale() const
{
    if (_image.isNull() || width() <= 2 || height() <= 2)
    {
        return 1.0;
    }
    const double sx = static_cast<double>(width() - 64) / _image.width();
    const double sy = static_cast<double>(height() - 64) / _image.height();
    return qMax(0.01, qMin(1.0, qMin(sx, sy)));
}

QPointF MediaCanvas::imageTopLeft() const
{
    if (_image.isNull())
    {
        return {};
    }
    const QSizeF size(_image.width() * _scale, _image.height() * _scale);
    return QPointF((width() - size.width()) / 2.0, (height() - size.height()) / 2.0)
           + _offset;
}

QRectF MediaCanvas::imageRect() const
{
    if (_image.isNull())
    {
        return {};
    }
    return QRectF(imageTopLeft(), QSizeF(_image.width() * _scale, _image.height() * _scale));
}

void MediaCanvas::resetView()
{
    _fit = true;
    _offset = QPointF();
    _scale = fitScale();
    emit zoomChanged(_scale);
    updateCursor();
    update();
}

void MediaCanvas::setActualSize()
{
    if (_image.isNull())
    {
        return;
    }
    _fit = false;
    _offset = QPointF();
    _scale = 1.0;
    emit zoomChanged(_scale);
    updateCursor();
    update();
}

void MediaCanvas::zoomBy(double factor)
{
    if (_image.isNull())
    {
        return;
    }
    const double oldScale = _scale;
    const double minScale = qMin(fitScale(), 0.1);
    const double newScale = qBound(minScale, oldScale * factor, 8.0);
    if (qFuzzyCompare(oldScale, newScale))
    {
        return;
    }
    const QPointF center(width() / 2.0, height() / 2.0);
    _offset += (center - center - _offset) * (1.0 - newScale / oldScale);
    _scale = newScale;
    _fit = false;
    emit zoomChanged(_scale);
    updateCursor();
    update();
}

void MediaCanvas::updateCursor()
{
    if (_image.isNull())
    {
        setCursor(Qt::ArrowCursor);
    }
    else if (_dragging)
    {
        setCursor(Qt::ClosedHandCursor);
    }
    else if (_scale > fitScale() + 0.001)
    {
        setCursor(Qt::OpenHandCursor);
    }
    else
    {
        setCursor(Qt::ArrowCursor);
    }
}

void MediaCanvas::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.fillRect(rect(), QColor(15, 15, 18));
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    if (_image.isNull())
    {
        const QPoint center = rect().center();
        if (_loading)
        {
            const int radius = 17;
            QPen pen(QColor(255, 255, 255, 80));
            pen.setWidthF(2.2);
            pen.setCapStyle(Qt::RoundCap);
            painter.setPen(pen);
            painter.setBrush(Qt::NoBrush);
            painter.drawArc(QRectF(center.x() - radius, center.y() - radius, radius * 2,
                                   radius * 2),
                            -_spinnerAngle * 16, 110 * 16);
        }
        else if (!_message.isEmpty())
        {
            painter.setPen(QColor(160, 160, 170));
            QFont font = painter.font();
            font.setPixelSize(14);
            painter.setFont(font);
            painter.drawText(rect().adjusted(40, 40, -40, -40), Qt::AlignCenter, _message);
        }
        return;
    }

    const QRectF target = imageRect();
    painter.drawImage(target, _image);

    if (_scale > 2.5)
    {
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    }
}

void MediaCanvas::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (_fit)
    {
        _scale = fitScale();
        emit zoomChanged(_scale);
    }
    updateCursor();
}

void MediaCanvas::wheelEvent(QWheelEvent* event)
{
    if (_image.isNull())
    {
        QWidget::wheelEvent(event);
        return;
    }

    const double oldScale = _scale;
    const double factor = event->angleDelta().y() > 0 ? 1.16 : 1.0 / 1.16;
    const double minScale = qMin(fitScale(), 0.1);
    const double newScale = qBound(minScale, oldScale * factor, 8.0);
    if (qFuzzyCompare(oldScale, newScale))
    {
        event->accept();
        return;
    }

    const QPointF cursor = event->position();
    const QPointF center(width() / 2.0, height() / 2.0);
    _offset += (cursor - center - _offset) * (1.0 - newScale / oldScale);
    _scale = newScale;
    _fit = false;
    emit zoomChanged(_scale);
    updateCursor();
    update();
    event->accept();
}

void MediaCanvas::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && !_image.isNull())
    {
        _dragging = true;
        _lastMousePos = event->pos();
        updateCursor();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void MediaCanvas::mouseMoveEvent(QMouseEvent* event)
{
    if (_dragging)
    {
        const QPoint delta = event->pos() - _lastMousePos;
        _lastMousePos = event->pos();
        _offset += delta;
        _fit = false;
        update();
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void MediaCanvas::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && _dragging)
    {
        _dragging = false;
        updateCursor();
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void MediaCanvas::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && !_image.isNull())
    {
        if (_scale > fitScale() + 0.01 || !_fit)
        {
            resetView();
        }
        else
        {
            setActualSize();
        }
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}
