#ifndef LIBRARYPAGE_H
#define LIBRARYPAGE_H

#include "PhotoItem.h"

#include <QWidget>

class ElaText;
class ElaComboBox;
class ElaSlider;
class ElaProgressRing;
class PhotoGridView;
class ThumbnailService;
class ElaPushButton;
class QLabel;
class QStackedWidget;

class LibraryPage : public QWidget
{
    Q_OBJECT
public:
    enum class Mode
    {
        All,
        Live,
        Video
    };

    explicit LibraryPage(Mode mode, ThumbnailService* thumbnails, QWidget* parent = nullptr);
    ~LibraryPage() override;

    void setItems(const PhotoItemList& items);
    void removeItem(const PhotoItemPtr& item);
    void setSearchText(const QString& text);
    void setLoading(bool loading, const QString& folderPath = QString());
    void refresh();

    Mode mode() const { return _mode; }

protected:
    void paintEvent(QPaintEvent* event) override;

signals:
    void itemActivated(const PhotoItemPtr& item, const PhotoItemList& list, int index);
    void openFolderRequested();
    void deleteRequested(const PhotoItemPtr& item);

private:
    void rebuildMonthCombo();
    void updateSummary();
    void updateEmptyState();
    void applySort();

    Mode _mode;
    ThumbnailService* _thumbnails{nullptr};
    PhotoItemList _sourceItems;
    PhotoItemList _visibleItems;
    QString _searchText;
    int _sortMode{0};
    bool _isLoading{false};

    ElaText* _titleLabel{nullptr};
    QLabel* _summaryLabel{nullptr};
    ElaComboBox* _monthCombo{nullptr};
    ElaComboBox* _sortCombo{nullptr};
    ElaSlider* _sizeSlider{nullptr};
    PhotoGridView* _grid{nullptr};
    QStackedWidget* _stack{nullptr};
    QWidget* _emptyPage{nullptr};
    QWidget* _loadingPage{nullptr};
    ElaText* _emptyTitle{nullptr};
    QLabel* _emptyHint{nullptr};
    ElaPushButton* _emptyButton{nullptr};
    ElaText* _loadingTitle{nullptr};
    QLabel* _loadingHint{nullptr};
    ElaProgressRing* _progressRing{nullptr};
};

#endif // LIBRARYPAGE_H
