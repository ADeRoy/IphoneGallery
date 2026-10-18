#ifndef PHOTOGRIDVIEW_H
#define PHOTOGRIDVIEW_H

#include "PhotoItem.h"

#include <QAbstractScrollArea>
#include <QCache>
#include <QImage>
#include <QSet>
#include <QVector>

class ThumbnailService;
class QPainter;

class PhotoGridView : public QAbstractScrollArea
{
    Q_OBJECT
public:
    explicit PhotoGridView(QWidget* parent = nullptr);
    ~PhotoGridView() override;

    void setThumbnailService(ThumbnailService* service);
    void setItems(const PhotoItemList& items);
    void removeItem(const PhotoItemPtr& item);
    const PhotoItemList& items() const { return _items; }

    void setThumbSize(int size);
    int thumbSize() const { return _thumbSize; }

    void scrollToMonth(const QString& monthKey);
    PhotoItemPtr currentItem() const;

signals:
    void itemActivated(const PhotoItemPtr& item, const PhotoItemList& list, int index);
    void selectionChanged(const PhotoItemPtr& item);
    void deleteRequested(const PhotoItemPtr& item);

protected:
    bool viewportEvent(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    struct Section
    {
        QString monthKey;
        QString title;
        int firstIndex{0};
        int count{0};
        int headerTop{0};
        int gridTop{0};
        int rows{0};
    };

    void rebuildSections();
    void relayout();
    void paintGrid(QPainter& painter);
    void paintSectionHeader(QPainter& painter, const Section& section);
    void paintTile(QPainter& painter, const QRect& rect, const PhotoItemPtr& item, bool hovered,
                   bool selected);
    void drawPlaceholder(QPainter& painter, const QRect& rect, const PhotoItemPtr& item);
    void drawBadges(QPainter& painter, const QRect& rect, const PhotoItemPtr& item);

    QPixmap tilePixmap(const QString& key, int cellSize);
    void cacheTilePixmap(const QString& key, const QImage& image);
    void requestVisibleThumbnails();
    QVector<int> visibleItemIndices(int extraRows = 1) const;
    int itemIndexAt(const QPoint& point) const;
    void ensureItemVisible(int index);
    void setCurrentIndex(int index);
    void updateHover(const QPoint& point);
    int columnsForWidth(int width) const;

    ThumbnailService* _thumbService{nullptr};
    PhotoItemList _items;
    QVector<Section> _sections;
    QVector<QRect> _itemRects;
    QCache<QString, QImage> _images;
    QCache<QString, QPixmap> _pixmaps;
    QSet<QString> _failed;
    QSet<QString> _requested;
    int _thumbSize{168};
    int _cellSize{168};
    int _columns{1};
    int _contentHeight{0};
    int _margin{28};
    int _gap{8};
    int _headerHeight{56};
    int _sectionGap{18};
    int _currentIndex{-1};
    int _hoverIndex{-1};
};

#endif // PHOTOGRIDVIEW_H
