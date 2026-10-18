#include "PhotoGridView.h"

#include "ElaDef.h"
#include "ElaIcon.h"
#include "ElaMenu.h"
#include "ElaScrollBar.h"
#include "ElaTheme.h"
#include "MediaUtils.h"
#include "PhotoLibrary.h"
#include "ThumbnailService.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDebug>
#include <QDir>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QProcess>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QTimer>

namespace
{
constexpr int ThumbnailEdge = 512;

QColor themeColor(ElaThemeType::ThemeColor color)
{
    return eTheme->getThemeColor(eTheme->getThemeMode(), color);
}

QFont sectionTitleFont(const QFont& base)
{
    QFont font = base;
    font.setPixelSize(19);
    font.setWeight(QFont::DemiBold);
    return font;
}

QFont sectionCountFont(const QFont& base)
{
    QFont font = base;
    font.setPixelSize(12);
    font.setWeight(QFont::Normal);
    return font;
}
} // namespace

PhotoGridView::PhotoGridView(QWidget* parent)
    : QAbstractScrollArea(parent)
{
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setFocusPolicy(Qt::StrongFocus);
    viewport()->setMouseTracking(true);
    viewport()->setAttribute(Qt::WA_OpaquePaintEvent, false);
    viewport()->setAutoFillBackground(false);

    _images.setMaxCost(200 * 1024);
    _pixmaps.setMaxCost(140 * 1024);

    new ElaScrollBar(verticalScrollBar(), this);
    verticalScrollBar()->setSingleStep(48);

    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this]() {
        viewport()->update();
        requestVisibleThumbnails();
    });
    connect(eTheme, &ElaTheme::themeModeChanged, this, [this]() {
        viewport()->update();
    });
}

PhotoGridView::~PhotoGridView() = default;

void PhotoGridView::setThumbnailService(ThumbnailService* service)
{
    if (_thumbService == service)
    {
        return;
    }
    if (_thumbService)
    {
        disconnect(_thumbService, nullptr, this, nullptr);
    }
    _thumbService = service;
    if (_thumbService)
    {
        connect(_thumbService, &ThumbnailService::thumbnailReady, this,
                [this](const QString& key, const QImage& image) {
                    _requested.remove(key);
                    _failed.remove(key);
                    const int cost = qMax(1, (image.width() * image.height() * 4) / 1024);
                    _images.insert(key, new QImage(image), cost);
                    cacheTilePixmap(key, image);
                    viewport()->update();
                });
        connect(_thumbService, &ThumbnailService::imageFailed, this,
                [this](const QString& key) {
                    if (_requested.contains(key))
                    {
                        _requested.remove(key);
                        _failed.insert(key);
                        viewport()->update();
                    }
                });
    }
    requestVisibleThumbnails();
}

void PhotoGridView::setItems(const PhotoItemList& items)
{
    _items = items;
    _requested.clear();
    _failed.clear();
    _currentIndex = -1;
    _hoverIndex = -1;
    rebuildSections();
    relayout();
    verticalScrollBar()->setValue(0);
    viewport()->update();
    requestVisibleThumbnails();
}

void PhotoGridView::removeItem(const PhotoItemPtr& item)
{
    if (!item)
    {
        return;
    }
    int index = -1;
    for (int i = 0; i < _items.size(); ++i)
    {
        const PhotoItemPtr& current = _items.at(i);
        if (current.get() == item.get()
            || (current->imagePath == item->imagePath
                && current->videoPath == item->videoPath))
        {
            index = i;
            break;
        }
    }
    if (index < 0)
    {
        return;
    }

    const int scroll = verticalScrollBar()->value();
    _items.removeAt(index);
    if (_currentIndex == index)
    {
        _currentIndex = -1;
    }
    else if (_currentIndex > index)
    {
        --_currentIndex;
    }
    _hoverIndex = -1;
    rebuildSections();
    relayout();
    verticalScrollBar()->setValue(qMin(scroll, verticalScrollBar()->maximum()));
    viewport()->update();
    requestVisibleThumbnails();
}

void PhotoGridView::setThumbSize(int size)
{
    size = qBound(96, size, 300);
    if (_thumbSize == size)
    {
        return;
    }
    _thumbSize = size;
    relayout();
    viewport()->update();
    requestVisibleThumbnails();
}

int PhotoGridView::columnsForWidth(int width) const
{
    const int available = qMax(1, width - 2 * _margin);
    const int columns = (available + _gap) / (_thumbSize + _gap);
    return qMax(1, columns);
}

void PhotoGridView::rebuildSections()
{
    _sections.clear();
    if (_items.isEmpty())
    {
        return;
    }

    int index = 0;
    while (index < _items.size())
    {
        const QString monthKey = _items.at(index)->monthKey;
        int count = 1;
        while (index + count < _items.size() && _items.at(index + count)->monthKey == monthKey)
        {
            ++count;
        }

        Section section;
        section.monthKey = monthKey;
        section.title = PhotoLibrary::monthTitle(monthKey);
        section.firstIndex = index;
        section.count = count;
        _sections.append(section);
        index += count;
    }
}

void PhotoGridView::relayout()
{
    _itemRects.clear();
    _itemRects.resize(_items.size());
    _pixmaps.clear();

    const int width = viewport()->width();
    _columns = columnsForWidth(width);
    const int available = qMax(1, width - 2 * _margin);
    _cellSize = qMax(96, (available - (_columns - 1) * _gap) / _columns);
    _cellSize = qMin(_cellSize, 360);

    int y = _margin;
    for (Section& section : _sections)
    {
        section.headerTop = y;
        y += _headerHeight;
        section.gridTop = y;
        section.rows = (section.count + _columns - 1) / _columns;

        for (int i = 0; i < section.count; ++i)
        {
            const int row = i / _columns;
            const int column = i % _columns;
            const QRect rect(_margin + column * (_cellSize + _gap),
                             y + row * (_cellSize + _gap), _cellSize, _cellSize);
            _itemRects[section.firstIndex + i] = rect;
        }

        y += section.rows * _cellSize + qMax(0, section.rows - 1) * _gap;
        y += _sectionGap;
    }

    _contentHeight = y + _margin - _sectionGap;
    verticalScrollBar()->setRange(0, qMax(0, _contentHeight - viewport()->height()));
    verticalScrollBar()->setPageStep(viewport()->height());
    verticalScrollBar()->setSingleStep(qMax(32, _cellSize / 4));
}

bool PhotoGridView::viewportEvent(QEvent* event)
{
    switch (event->type())
    {
    case QEvent::Paint:
    {
        QPainter painter(viewport());
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        paintGrid(painter);
        return true;
    }
    case QEvent::Resize:
    {
        relayout();
        viewport()->update();
        requestVisibleThumbnails();
        break;
    }
    case QEvent::MouseButtonPress:
    {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::LeftButton)
        {
            const int index = itemIndexAt(mouse->position().toPoint());
            qDebug() << "grid press" << mouse->position() << "index" << index;
            if (index >= 0)
            {
                setCurrentIndex(index);
                setFocus(Qt::MouseFocusReason);
            }
        }
        break;
    }
    case QEvent::MouseButtonDblClick:
    {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::LeftButton)
        {
            const int index = itemIndexAt(mouse->position().toPoint());
            qDebug() << "grid double click" << mouse->position() << "index" << index;
            if (index >= 0)
            {
                setCurrentIndex(index);
                emit itemActivated(_items.at(index), _items, index);
            }
        }
        return true;
    }
    case QEvent::MouseMove:
    {
        auto* mouse = static_cast<QMouseEvent*>(event);
        updateHover(mouse->position().toPoint());
        break;
    }
    case QEvent::Leave:
    {
        if (_hoverIndex >= 0)
        {
            _hoverIndex = -1;
            viewport()->update();
        }
        break;
    }
    case QEvent::ContextMenu:
    {
        auto* contextEvent = static_cast<QContextMenuEvent*>(event);
        const int index = itemIndexAt(contextEvent->pos());
        if (index < 0 || index >= _items.size())
        {
            return true;
        }
        setCurrentIndex(index);
        const PhotoItemPtr item = _items.at(index);
        const QString mediaPath = item->hasVideo() ? item->videoPath : item->imagePath;

        ElaMenu menu(this);
        QAction* openFolderAction =
            menu.addElaIconAction(ElaIconType::FolderOpen,
                                  QStringLiteral("打开文件所在目录"));
        QAction* copyPathAction =
            menu.addElaIconAction(ElaIconType::Copy, QStringLiteral("复制完整路径"));
        menu.addSeparator();
        QAction* openFileAction =
            menu.addElaIconAction(ElaIconType::Image, QStringLiteral("在查看器中打开"));
        menu.addSeparator();
        QAction* deleteAction =
            menu.addElaIconAction(ElaIconType::Trash, QStringLiteral("删除到回收站"));
        QAction* chosen = menu.exec(contextEvent->globalPos());
        if (chosen == openFolderAction)
        {
            QProcess::startDetached(
                QStringLiteral("explorer.exe"),
                QStringList{QStringLiteral("/select,"),
                            QDir::toNativeSeparators(mediaPath)});
        }
        else if (chosen == copyPathAction)
        {
            QGuiApplication::clipboard()->setText(mediaPath);
        }
        else if (chosen == openFileAction)
        {
            emit itemActivated(item, _items, index);
        }
        else if (chosen == deleteAction)
        {
            emit deleteRequested(item);
        }
        return true;
    }
    default:
        break;
    }
    return QAbstractScrollArea::viewportEvent(event);
}

void PhotoGridView::paintGrid(QPainter& painter)
{
    const QColor background = themeColor(ElaThemeType::BasicBase);
    painter.fillRect(viewport()->rect(), background);

    if (_items.isEmpty())
    {
        return;
    }

    const int scrollY = verticalScrollBar()->value();
    const int top = scrollY;
    const int bottom = scrollY + viewport()->height();

    painter.save();
    painter.translate(0, -scrollY);

    for (const Section& section : _sections)
    {
        if (section.gridTop > bottom)
        {
            break;
        }
        const int sectionBottom =
            section.gridTop + section.rows * _cellSize + qMax(0, section.rows - 1) * _gap;
        if (sectionBottom < top - _sectionGap)
        {
            continue;
        }

        if (section.headerTop + _headerHeight >= top)
        {
            paintSectionHeader(painter, section);
        }

        const int stride = _cellSize + _gap;
        int firstRow = qMax(0, (top - section.gridTop) / stride - 1);
        int lastRow = qMin(section.rows - 1, (bottom - section.gridTop) / stride + 1);
        for (int row = firstRow; row <= lastRow; ++row)
        {
            for (int column = 0; column < _columns; ++column)
            {
                const int offset = row * _columns + column;
                if (offset >= section.count)
                {
                    break;
                }
                const int index = section.firstIndex + offset;
                const QRect rect = _itemRects.at(index);
                if (!rect.intersects(QRect(0, top, viewport()->width(), viewport()->height())))
                {
                    continue;
                }
                paintTile(painter, rect, _items.at(index), index == _hoverIndex,
                          index == _currentIndex);
            }
        }
    }
    painter.restore();
}

void PhotoGridView::paintSectionHeader(QPainter& painter, const Section& section)
{
    const QRect rect(_margin, section.headerTop, viewport()->width() - 2 * _margin,
                     _headerHeight);

    painter.save();
    painter.setPen(themeColor(ElaThemeType::BasicText));
    painter.setFont(sectionTitleFont(font()));
    const QRect titleRect = rect.adjusted(0, 0, 0, -18);
    painter.drawText(titleRect, Qt::AlignLeft | Qt::AlignBottom, section.title);

    painter.setPen(themeColor(ElaThemeType::BasicDetailsText));
    painter.setFont(sectionCountFont(font()));
    painter.drawText(rect.adjusted(0, 0, 0, -3), Qt::AlignRight | Qt::AlignBottom,
                     QStringLiteral("%1 个项目").arg(section.count));
    painter.restore();
}

void PhotoGridView::paintTile(QPainter& painter, const QRect& rect, const PhotoItemPtr& item,
                              bool hovered, bool selected)
{
    const int radius = qMin(12, rect.width() / 12);
    QPainterPath clipPath;
    clipPath.addRoundedRect(QRectF(rect), radius, radius);

    painter.save();
    painter.setClipPath(clipPath);

    const QString key = ThumbnailService::thumbnailKey(item, ThumbnailEdge);
    const QPixmap pixmap = tilePixmap(key, _cellSize);
    if (pixmap.isNull())
    {
        drawPlaceholder(painter, rect, item);
    }
    else
    {
        painter.drawPixmap(rect, pixmap);
    }

    if (hovered)
    {
        painter.fillRect(rect, QColor(255, 255, 255, 16));
    }

    drawBadges(painter, rect, item);

    if (hovered && item->dateTime().isValid())
    {
        QFont dateFont = painter.font();
        dateFont.setPixelSize(10);
        dateFont.setWeight(QFont::DemiBold);
        painter.setFont(dateFont);
        const QString dateText =
            item->dateTime().toString(QStringLiteral("yyyy/MM/dd HH:mm"));
        const QFontMetrics metrics(dateFont);
        const QRect dateRect(rect.left() + 10, rect.top() + 10,
                             metrics.horizontalAdvance(dateText) + 16, 21);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, 138));
        painter.drawRoundedRect(dateRect, 10.5, 10.5);
        painter.setPen(QColor(255, 255, 255, 235));
        painter.drawText(dateRect, Qt::AlignCenter, dateText);
    }

    if (hovered)
    {
        painter.setBrush(Qt::NoBrush);
        QPen pen(themeColor(ElaThemeType::BasicBorderHover));
        pen.setWidthF(1.4);
        painter.setPen(pen);
        painter.drawRoundedRect(QRectF(rect).adjusted(0.7, 0.7, -0.7, -0.7), radius, radius);
    }

    if (selected)
    {
        painter.setBrush(Qt::NoBrush);
        QPen pen(QColor(10, 132, 255));
        pen.setWidth(3);
        painter.setPen(pen);
        painter.drawRoundedRect(QRectF(rect).adjusted(1.5, 1.5, -1.5, -1.5), radius, radius);

        const QRect checkRect(rect.right() - 32, rect.top() + 10, 22, 22);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(10, 132, 255));
        painter.drawEllipse(checkRect);
        QPen checkPen(Qt::white);
        checkPen.setWidthF(2.0);
        checkPen.setCapStyle(Qt::RoundCap);
        painter.setPen(checkPen);
        painter.drawLine(checkRect.left() + 6, checkRect.center().y(),
                         checkRect.left() + 9, checkRect.bottom() - 6);
        painter.drawLine(checkRect.left() + 9, checkRect.bottom() - 6,
                         checkRect.right() - 5, checkRect.top() + 7);
    }

    painter.restore();
}

void PhotoGridView::drawPlaceholder(QPainter& painter, const QRect& rect,
                                    const PhotoItemPtr& item)
{
    painter.fillRect(rect, themeColor(ElaThemeType::BasicBaseDeep));

    painter.save();
    painter.setOpacity(0.42);
    const int iconSize = qBound(28, rect.width() / 5, 42);
    const QRect iconRect(rect.center().x() - iconSize / 2,
                         rect.center().y() - iconSize / 2 - 6, iconSize, iconSize);
    const QIcon icon = ElaIcon::getInstance()->getElaIcon(
        item->hasVideo() && !item->hasImage() ? ElaIconType::Video : ElaIconType::Images);
    icon.paint(&painter, iconRect, Qt::AlignCenter, QIcon::Normal, QIcon::Off);
    painter.restore();

    if (_failed.contains(ThumbnailService::thumbnailKey(item, ThumbnailEdge)))
    {
        painter.save();
        painter.setPen(themeColor(ElaThemeType::BasicDetailsText));
        QFont font = painter.font();
        font.setPixelSize(11);
        painter.setFont(font);
        painter.drawText(rect.adjusted(8, 0, -8, -10), Qt::AlignHCenter | Qt::AlignBottom,
                         item->isHeic ? QStringLiteral("需要 FFmpeg")
                                      : QStringLiteral("无法预览"));
        painter.restore();
    }
}

void PhotoGridView::drawBadges(QPainter& painter, const QRect& rect, const PhotoItemPtr& item)
{
    painter.save();
    QFont badgeFont = painter.font();
    badgeFont.setPixelSize(10);
    badgeFont.setWeight(QFont::DemiBold);
    painter.setFont(badgeFont);

    const bool isVideoOnly = item->hasVideo() && !item->hasImage();

    if (isVideoOnly)
    {
        const int diameter = 42;
        const QRect playRect(rect.center().x() - diameter / 2,
                             rect.center().y() - diameter / 2, diameter, diameter);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, 118));
        painter.drawEllipse(playRect);

        QPainterPath triangle;
        const QPointF center = playRect.center();
        triangle.moveTo(center.x() - 4, center.y() - 7);
        triangle.lineTo(center.x() + 8, center.y());
        triangle.lineTo(center.x() - 4, center.y() + 7);
        triangle.closeSubpath();
        painter.setBrush(QColor(255, 255, 255, 232));
        painter.drawPath(triangle);
    }

    QString badgeText;
    if (item->isLivePhoto)
    {
        badgeText = QStringLiteral("实况");
    }
    else if (isVideoOnly)
    {
        badgeText = QStringLiteral("视频");
    }

    if (!badgeText.isEmpty())
    {
        const QFontMetrics metrics(badgeFont);
        const int textWidth = metrics.horizontalAdvance(badgeText);
        const QRect badgeRect(rect.left() + 10, rect.bottom() - 30, textWidth + 24, 21);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, 132));
        painter.drawRoundedRect(badgeRect, 10.5, 10.5);

        painter.setBrush(QColor(255, 255, 255, 230));
        painter.drawEllipse(QPointF(badgeRect.left() + 11, badgeRect.center().y() - 2.5), 3.0,
                            3.0);

        painter.setPen(QColor(255, 255, 255, 235));
        painter.drawText(badgeRect.adjusted(19, 0, -4, 0), Qt::AlignLeft | Qt::AlignVCenter,
                         badgeText);
    }

    painter.restore();
}

QPixmap PhotoGridView::tilePixmap(const QString& key, int cellSize)
{
    const QString pixmapKey = key + QLatin1Char('#') + QString::number(cellSize);
    if (QPixmap* pixmap = _pixmaps.object(pixmapKey))
    {
        return *pixmap;
    }
    if (QImage* image = _images.object(key))
    {
        cacheTilePixmap(key, *image);
        if (QPixmap* pixmap = _pixmaps.object(pixmapKey))
        {
            return *pixmap;
        }
    }
    return {};
}

void PhotoGridView::cacheTilePixmap(const QString& key, const QImage& image)
{
    if (image.isNull() || _cellSize <= 0)
    {
        return;
    }
    QPixmap pixmap = QPixmap::fromImage(image.scaled(_cellSize, _cellSize,
                                                     Qt::KeepAspectRatioByExpanding,
                                                     Qt::SmoothTransformation));
    const int x = qMax(0, (pixmap.width() - _cellSize) / 2);
    const int y = qMax(0, (pixmap.height() - _cellSize) / 2);
    pixmap = pixmap.copy(x, y, _cellSize, _cellSize);
    const QString pixmapKey = key + QLatin1Char('#') + QString::number(_cellSize);
    _pixmaps.insert(pixmapKey, new QPixmap(pixmap),
                    qMax(1, (_cellSize * _cellSize * 4) / 1024));
}

void PhotoGridView::requestVisibleThumbnails()
{
    if (!_thumbService || _items.isEmpty() || !isVisible())
    {
        return;
    }

    const QVector<int> indices = visibleItemIndices(1);
    for (int index : indices)
    {
        if (index < 0 || index >= _items.size())
        {
            continue;
        }
        const PhotoItemPtr& item = _items.at(index);
        const QString key = ThumbnailService::thumbnailKey(item, ThumbnailEdge);
        if (_images.contains(key) || _failed.contains(key) || _requested.contains(key))
        {
            continue;
        }
        _requested.insert(key);
        _thumbService->requestThumbnail(item, ThumbnailEdge);
    }
}

QVector<int> PhotoGridView::visibleItemIndices(int extraRows) const
{
    QVector<int> result;
    if (_items.isEmpty())
    {
        return result;
    }

    const int scrollY = verticalScrollBar()->value();
    const int top = scrollY;
    const int bottom = scrollY + viewport()->height();
    const int stride = _cellSize + _gap;

    for (const Section& section : _sections)
    {
        const int sectionBottom =
            section.gridTop + section.rows * _cellSize + qMax(0, section.rows - 1) * _gap;
        if (sectionBottom < top - stride || section.gridTop > bottom + stride)
        {
            continue;
        }
        int firstRow = qMax(0, (top - section.gridTop) / stride - extraRows);
        int lastRow = qMin(section.rows - 1, (bottom - section.gridTop) / stride + extraRows);
        for (int row = firstRow; row <= lastRow; ++row)
        {
            for (int column = 0; column < _columns; ++column)
            {
                const int offset = row * _columns + column;
                if (offset >= section.count)
                {
                    break;
                }
                result.append(section.firstIndex + offset);
            }
        }
    }
    return result;
}

int PhotoGridView::itemIndexAt(const QPoint& point) const
{
    const QPoint contentPoint(point.x(), point.y() + verticalScrollBar()->value());
    const QVector<int> indices = visibleItemIndices(0);
    for (int index : indices)
    {
        if (_itemRects.at(index).contains(contentPoint))
        {
            return index;
        }
    }
    return -1;
}

void PhotoGridView::ensureItemVisible(int index)
{
    if (index < 0 || index >= _itemRects.size())
    {
        return;
    }
    const QRect rect = _itemRects.at(index);
    const int scrollY = verticalScrollBar()->value();
    if (rect.top() < scrollY + _margin)
    {
        verticalScrollBar()->setValue(qMax(0, rect.top() - _margin - 8));
    }
    else if (rect.bottom() > scrollY + viewport()->height() - _margin)
    {
        verticalScrollBar()->setValue(rect.bottom() - viewport()->height() + _margin + 8);
    }
}

void PhotoGridView::setCurrentIndex(int index)
{
    if (index < 0 || index >= _items.size() || index == _currentIndex)
    {
        return;
    }
    _currentIndex = index;
    emit selectionChanged(_items.at(index));
    viewport()->update();
}

void PhotoGridView::updateHover(const QPoint& point)
{
    const int index = itemIndexAt(point);
    if (index == _hoverIndex)
    {
        return;
    }
    _hoverIndex = index;
    viewport()->setCursor(index >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
    if (index >= 0 && index < _items.size())
    {
        viewport()->setToolTip(
            MediaUtils::formatDate(_items.at(index)->dateTime()));
    }
    else
    {
        viewport()->setToolTip(QString());
    }
    viewport()->update();
}

void PhotoGridView::scrollToMonth(const QString& monthKey)
{
    for (const Section& section : _sections)
    {
        if (section.monthKey == monthKey)
        {
            verticalScrollBar()->setValue(qMax(0, section.headerTop - 12));
            return;
        }
    }
}

PhotoItemPtr PhotoGridView::currentItem() const
{
    if (_currentIndex >= 0 && _currentIndex < _items.size())
    {
        return _items.at(_currentIndex);
    }
    return {};
}

void PhotoGridView::keyPressEvent(QKeyEvent* event)
{
    if (_items.isEmpty())
    {
        QAbstractScrollArea::keyPressEvent(event);
        return;
    }

    const int pageRows = qMax(1, viewport()->height() / (_cellSize + _gap));
    int index = _currentIndex;
    if (index < 0)
    {
        index = 0;
    }

    switch (event->key())
    {
    case Qt::Key_Left:
        index = qMax(0, index - 1);
        break;
    case Qt::Key_Right:
        index = qMin(_items.size() - 1, index + 1);
        break;
    case Qt::Key_Up:
        index = qMax(0, index - _columns);
        break;
    case Qt::Key_Down:
        index = qMin(_items.size() - 1, index + _columns);
        break;
    case Qt::Key_PageUp:
        index = qMax(0, index - _columns * pageRows);
        break;
    case Qt::Key_PageDown:
        index = qMin(_items.size() - 1, index + _columns * pageRows);
        break;
    case Qt::Key_Home:
        index = 0;
        break;
    case Qt::Key_End:
        index = _items.size() - 1;
        break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Space:
        if (index >= 0 && index < _items.size())
        {
            setCurrentIndex(index);
            emit itemActivated(_items.at(index), _items, index);
        }
        return;
    default:
        QAbstractScrollArea::keyPressEvent(event);
        return;
    }

    setCurrentIndex(index);
    ensureItemVisible(index);
    event->accept();
}

void PhotoGridView::resizeEvent(QResizeEvent* event)
{
    QAbstractScrollArea::resizeEvent(event);
    relayout();
}

void PhotoGridView::showEvent(QShowEvent* event)
{
    QAbstractScrollArea::showEvent(event);
    QTimer::singleShot(0, this, [this]() {
        viewport()->update();
        requestVisibleThumbnails();
    });
}
