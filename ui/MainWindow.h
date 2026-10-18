#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "ElaWindow.h"
#include "PhotoItem.h"

#include <QImage>
#include <QString>

class LibraryPage;
class PhotoViewer;
class ThumbnailService;
class ElaLineEdit;
class ElaIconButton;
class ElaToggleSwitch;
class QTimer;

class MainWindow : public ElaWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void setupWindow();
    void setupAppBar();
    void setupPages();
    void setupShortcuts();
    void openFolderDialog();
    void loadFolder(const QString& path);
    void refreshCurrentFolder();
    void applySearchText(const QString& text);
    void applyScanResult(const PhotoItemList& items);
    void updateUserCard();
    void requestCoverImage();
    void updateCoverImage(const QImage& image);
    void openViewer(const PhotoItemPtr& item, const PhotoItemList& list, int index);
    void deleteItem(const PhotoItemPtr& item);

    ThumbnailService* _thumbnails{nullptr};
    PhotoViewer* _viewer{nullptr};
    LibraryPage* _allPage{nullptr};
    LibraryPage* _livePage{nullptr};
    LibraryPage* _videoPage{nullptr};
    ElaLineEdit* _searchEdit{nullptr};
    ElaIconButton* _openButton{nullptr};
    ElaIconButton* _refreshButton{nullptr};
    ElaToggleSwitch* _liveSwitch{nullptr};
    QTimer* _searchTimer{nullptr};

    PhotoItemList _items;
    QString _rootPath;
    QString _coverKey;
    int _scanGeneration{0};
};

#endif // MAINWINDOW_H
