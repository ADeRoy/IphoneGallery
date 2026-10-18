#include "MainWindow.h"

#include "ElaDef.h"
#include "ElaIcon.h"
#include "ElaIconButton.h"
#include "ElaLineEdit.h"
#include "ElaText.h"
#include "ElaTheme.h"
#include "ElaToggleSwitch.h"
#include "LibraryPage.h"
#include "MediaUtils.h"
#include "PhotoLibrary.h"
#include "PhotoViewer.h"
#include "ThumbnailService.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QIcon>
#include <QLineEdit>
#include <QLinearGradient>
#include <QMimeData>
#include <QPainter>
#include <QSettings>
#include <QShortcut>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QtConcurrent>

namespace
{
QPixmap makeAppIconPixmap(int size)
{
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QLinearGradient gradient(0, 0, size, size);
    gradient.setColorAt(0.0, QColor(10, 132, 255));
    gradient.setColorAt(1.0, QColor(94, 92, 230));
    painter.setPen(Qt::NoPen);
    painter.setBrush(gradient);
    painter.drawRoundedRect(QRectF(0, 0, size, size), size * 0.235, size * 0.235);

    const int iconSize = qRound(size * 0.56);
    const QIcon icon = ElaIcon::getInstance()->getElaIcon(ElaIconType::Images, iconSize,
                                                         QColor(255, 255, 255));
    icon.paint(&painter, QRect(0, 0, size, size), Qt::AlignCenter);
    return pixmap;
}

QString settingsPath()
{
    return QStringLiteral("IphoneGallery");
}
} // namespace

MainWindow::MainWindow(QWidget* parent)
    : ElaWindow(parent)
{
    QSettings settings(settingsPath(), settingsPath());
    const QString theme = settings.value(QStringLiteral("theme"), QStringLiteral("light")).toString();
    eTheme->setThemeMode(theme == QStringLiteral("light") ? ElaThemeType::Light
                                                         : ElaThemeType::Dark);

    _thumbnails = new ThumbnailService(this);
    _viewer = new PhotoViewer(_thumbnails, this);

    setupWindow();
    setupAppBar();
    setupPages();
    setupShortcuts();

    connect(_thumbnails, &ThumbnailService::thumbnailReady, this,
            [this](const QString& key, const QImage& image) {
                if (key == _coverKey)
                {
                    updateCoverImage(image);
                }
            });
    connect(_openButton, &ElaIconButton::clicked, this, &MainWindow::openFolderDialog);
    connect(_refreshButton, &ElaIconButton::clicked, this, &MainWindow::refreshCurrentFolder);
    connect(this, &ElaWindow::userInfoCardClicked, this, &MainWindow::openFolderDialog);
    connect(_viewer, &PhotoViewer::livePlaybackEnabledChanged, this, [this](bool enabled) {
        if (_liveSwitch)
        {
            QSignalBlocker blocker(_liveSwitch);
            _liveSwitch->setIsToggled(enabled);
        }
    });

    _searchEdit->setText(QString());
    for (LibraryPage* page : {_allPage, _livePage, _videoPage})
    {
        page->setLoading(false);
        connect(page, &LibraryPage::itemActivated, this, &MainWindow::openViewer);
        connect(page, &LibraryPage::openFolderRequested, this, &MainWindow::openFolderDialog);
        connect(page, &LibraryPage::deleteRequested, this, &MainWindow::deleteItem);
    }
    updateUserCard();

    QString startupFolder;
    const QStringList arguments = QCoreApplication::arguments();
    for (int i = 1; i < arguments.size(); ++i)
    {
        if ((arguments.at(i) == QStringLiteral("--folder")
             || arguments.at(i) == QStringLiteral("-f"))
            && i + 1 < arguments.size())
        {
            startupFolder = arguments.at(++i);
        }
        else if (QFileInfo(arguments.at(i)).isDir())
        {
            startupFolder = arguments.at(i);
        }
    }

    if (startupFolder.isEmpty())
    {
        startupFolder = settings.value(QStringLiteral("lastFolder")).toString();
    }
    if (!startupFolder.isEmpty() && QFileInfo(startupFolder).isDir())
    {
        QTimer::singleShot(0, this, [this, startupFolder]() { loadFolder(startupFolder); });
    }
    else
    {
        requestCoverImage();
    }
}

MainWindow::~MainWindow()
{
    QSettings settings(settingsPath(), settingsPath());
    settings.setValue(QStringLiteral("theme"),
                      eTheme->getThemeMode() == ElaThemeType::Dark
                          ? QStringLiteral("dark")
                          : QStringLiteral("light"));
}

void MainWindow::setupWindow()
{
    setWindowTitle(QStringLiteral("iPhone 图库"));
    setWindowIcon(QIcon(makeAppIconPixmap(128)));
    setMinimumSize(1040, 700);
    resize(1400, 900);
    setAcceptDrops(true);
    setNavigationBarWidth(238);
    setIsAllowPageOpenInNewWindow(false);
    setStackSwitchMode(ElaWindowType::StackSwitchMode::Scale);
    setUserInfoCardPixmap(makeAppIconPixmap(96));
    setUserInfoCardTitle(QStringLiteral("iPhone 图库"));
    setUserInfoCardSubTitle(QStringLiteral("打开本地照片文件夹"));
    moveToCenter();
}

void MainWindow::setupAppBar()
{
    auto* barWidget = new QWidget(this);
    auto* layout = new QHBoxLayout(barWidget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    _openButton = new ElaIconButton(ElaIconType::FolderOpen, 16, 34, 30, barWidget);
    _openButton->setToolTip(QStringLiteral("打开照片文件夹 (Ctrl+O)"));
    _openButton->setBorderRadius(8);

    _searchEdit = new ElaLineEdit(barWidget);
    _searchEdit->setFixedWidth(292);
    _searchEdit->setPlaceholderText(QStringLiteral("搜索照片、实况或视频"));
    _searchEdit->setIsClearButtonEnable(true);
    const QIcon searchIcon = ElaIcon::getInstance()->getElaIcon(
        ElaIconType::MagnifyingGlass, 14,
        eTheme->getThemeColor(eTheme->getThemeMode(), ElaThemeType::BasicDetailsText));
    _searchEdit->addAction(searchIcon, QLineEdit::LeadingPosition);

    _refreshButton = new ElaIconButton(ElaIconType::ArrowsRotate, 16, 34, 30, barWidget);
    _refreshButton->setToolTip(QStringLiteral("重新扫描 (F5)"));
    _refreshButton->setBorderRadius(8);

    auto* liveControl = new QWidget(barWidget);
    auto* liveLayout = new QHBoxLayout(liveControl);
    liveLayout->setContentsMargins(4, 0, 0, 0);
    liveLayout->setSpacing(6);
    auto* liveLabel = new ElaText(QStringLiteral("实况"), 12, liveControl);
    liveLabel->setStyleSheet(QStringLiteral("color:#8A8A93; background:transparent;"));
    _liveSwitch = new ElaToggleSwitch(liveControl);
    _liveSwitch->setFixedSize(40, 22);
    _liveSwitch->setToolTip(
        QStringLiteral("实况照片：开启时打开后自动播放一次；按住画面可循环播放"));
    _liveSwitch->setIsToggled(_viewer->livePlaybackEnabled());
    liveLayout->addWidget(liveLabel);
    liveLayout->addWidget(_liveSwitch);

    layout->addWidget(_openButton);
    layout->addWidget(_searchEdit);
    layout->addWidget(_refreshButton);
    layout->addWidget(liveControl);

    setCustomWidget(ElaAppBarType::MiddleArea, barWidget);

    connect(_liveSwitch, &ElaToggleSwitch::toggled, this, [this](bool checked) {
        _viewer->setLivePlaybackEnabled(checked);
    });

    _searchTimer = new QTimer(this);
    _searchTimer->setSingleShot(true);
    _searchTimer->setInterval(180);
    connect(_searchTimer, &QTimer::timeout, this, [this]() {
        applySearchText(_searchEdit->text());
    });
    connect(_searchEdit, &QLineEdit::textChanged, this, [this](const QString&) {
        _searchTimer->start();
    });
}

void MainWindow::setupPages()
{
    _allPage = new LibraryPage(LibraryPage::Mode::All, _thumbnails);
    _livePage = new LibraryPage(LibraryPage::Mode::Live, _thumbnails);
    _videoPage = new LibraryPage(LibraryPage::Mode::Video, _thumbnails);

    addPageNode(QStringLiteral("图库"), _allPage, ElaIconType::Images);
    addPageNode(QStringLiteral("实况照片"), _livePage, ElaIconType::PhotoFilm);
    addPageNode(QStringLiteral("视频"), _videoPage, ElaIconType::Video);

    QString openKey;
    QString refreshKey;
    addFooterNode(QStringLiteral("打开文件夹"), nullptr, openKey, 0, ElaIconType::FolderOpen);
    addFooterNode(QStringLiteral("重新扫描"), nullptr, refreshKey, 0, ElaIconType::ArrowsRotate);
    connect(this, &ElaWindow::navigationNodeClicked, this,
            [this, openKey, refreshKey](ElaNavigationType::NavigationNodeType, QString nodeKey) {
                if (nodeKey == openKey)
                {
                    openFolderDialog();
                }
                else if (nodeKey == refreshKey)
                {
                    refreshCurrentFolder();
                }
            });

    const QString pageKey = _allPage->property("ElaPageKey").toString();
    if (!pageKey.isEmpty())
    {
        navigation(pageKey);
    }
}

void MainWindow::setupShortcuts()
{
    auto* openShortcut = new QShortcut(QKeySequence::Open, this);
    connect(openShortcut, &QShortcut::activated, this, &MainWindow::openFolderDialog);

    auto* findShortcut = new QShortcut(QKeySequence::Find, this);
    connect(findShortcut, &QShortcut::activated, this, [this]() {
        _searchEdit->setFocus(Qt::ShortcutFocusReason);
        _searchEdit->selectAll();
    });

    auto* refreshShortcut = new QShortcut(QKeySequence(Qt::Key_F5), this);
    connect(refreshShortcut, &QShortcut::activated, this, &MainWindow::refreshCurrentFolder);

    auto* clearShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(clearShortcut, &QShortcut::activated, this, [this]() {
        if (!_searchEdit->text().isEmpty())
        {
            _searchEdit->clear();
        }
    });
}

void MainWindow::openFolderDialog()
{
    QString startPath = _rootPath;
    if (startPath.isEmpty())
    {
        startPath = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    }
    if (startPath.isEmpty())
    {
        startPath = QDir::homePath();
    }

    const QString path = QFileDialog::getExistingDirectory(
        this, QStringLiteral("选择 iPhone 照片文件夹"), startPath);
    if (!path.isEmpty())
    {
        loadFolder(path);
    }
}

void MainWindow::loadFolder(const QString& path)
{
    const QFileInfo info(path);
    if (!info.isDir())
    {
        return;
    }

    _rootPath = info.absoluteFilePath();
    qDebug() << "loadFolder" << _rootPath;
    QSettings settings(settingsPath(), settingsPath());
    settings.setValue(QStringLiteral("lastFolder"), _rootPath);

    if (_viewer->isVisible())
    {
        _viewer->close();
    }

    _searchEdit->clear();
    _items.clear();
    _coverKey.clear();
    ++_scanGeneration;

    for (LibraryPage* page : {_allPage, _livePage, _videoPage})
    {
        page->setLoading(true, _rootPath);
    }
    setUserInfoCardTitle(QFileInfo(_rootPath).fileName().isEmpty()
                             ? QStringLiteral("iPhone 图库")
                             : QFileInfo(_rootPath).fileName());
    setUserInfoCardSubTitle(QStringLiteral("正在扫描文件夹…"));
    setWindowTitle(QStringLiteral("iPhone 图库"));

    auto* watcher = new QFutureWatcher<PhotoItemList>(this);
    const int generation = _scanGeneration;
    const QString rootPath = _rootPath;
    connect(watcher, &QFutureWatcher<PhotoItemList>::finished, this,
            [this, watcher, generation]() {
                const PhotoItemList items = watcher->result();
                watcher->deleteLater();
                if (generation != _scanGeneration)
                {
                    return;
                }
                applyScanResult(items);
            });
    watcher->setFuture(QtConcurrent::run(
        [rootPath]() { return PhotoLibrary::scanDirectory(rootPath); }));
}

void MainWindow::refreshCurrentFolder()
{
    if (!_rootPath.isEmpty())
    {
        loadFolder(_rootPath);
    }
    else
    {
        openFolderDialog();
    }
}

void MainWindow::applySearchText(const QString& text)
{
    for (LibraryPage* page : {_allPage, _livePage, _videoPage})
    {
        page->setSearchText(text);
    }
}

void MainWindow::applyScanResult(const PhotoItemList& items)
{
    _items = items;
    qDebug() << "scan finished items" << items.size();
    for (LibraryPage* page : {_allPage, _livePage, _videoPage})
    {
        page->setItems(items);
        page->setLoading(false);
    }

    updateUserCard();
    requestCoverImage();

    const QString pageKey = _allPage->property("ElaPageKey").toString();
    if (!pageKey.isEmpty())
    {
        navigation(pageKey);
    }
}

void MainWindow::updateUserCard()
{
    qint64 totalBytes = 0;
    qint64 live = 0;
    for (const PhotoItemPtr& item : _items)
    {
        totalBytes += item->imageBytes + item->videoBytes;
        if (item->isLivePhoto)
        {
            ++live;
        }
    }

    QString title = QStringLiteral("iPhone 图库");
    QString subtitle = QStringLiteral("打开本地照片文件夹");
    if (!_rootPath.isEmpty())
    {
        const QFileInfo info(_rootPath);
        title = info.fileName().isEmpty() ? info.absoluteFilePath() : info.fileName();
        subtitle = QStringLiteral("%1 个项目 · %2")
                       .arg(_items.size())
                       .arg(MediaUtils::formatBytes(totalBytes));
        if (!_items.isEmpty())
        {
            subtitle += QStringLiteral(" · %1 实况").arg(live);
        }
    }
    setWindowTitle(QStringLiteral("iPhone 图库"));

    setUserInfoCardTitle(title);
    setUserInfoCardSubTitle(subtitle);
}

void MainWindow::requestCoverImage()
{
    _coverKey.clear();
    for (const PhotoItemPtr& item : _items)
    {
        if (!item->hasImage() && !item->hasVideo())
        {
            continue;
        }
        _coverKey = ThumbnailService::thumbnailKey(item, 512);
        _thumbnails->requestThumbnail(item, 512);
        return;
    }
    setUserInfoCardPixmap(makeAppIconPixmap(96));
}

void MainWindow::updateCoverImage(const QImage& image)
{
    if (image.isNull())
    {
        return;
    }
    QPixmap pixmap = QPixmap::fromImage(image.scaled(160, 160, Qt::KeepAspectRatioByExpanding,
                                                     Qt::SmoothTransformation));
    const int x = qMax(0, (pixmap.width() - 160) / 2);
    const int y = qMax(0, (pixmap.height() - 160) / 2);
    pixmap = pixmap.copy(x, y, 160, 160);
    setUserInfoCardPixmap(pixmap);
}

void MainWindow::openViewer(const PhotoItemPtr& item, const PhotoItemList& list, int index)
{
    Q_UNUSED(item)
    _viewer->openItems(list, index);
}

void MainWindow::deleteItem(const PhotoItemPtr& item)
{
    if (!item)
    {
        return;
    }

    QStringList paths;
    if (item->hasImage())
    {
        paths.append(item->imagePath);
    }
    if (item->hasVideo())
    {
        paths.append(item->videoPath);
    }
    paths.removeDuplicates();

    QStringList failed;
    bool moved = false;
    for (const QString& path : paths)
    {
        if (path.isEmpty() || !QFileInfo::exists(path))
        {
            continue;
        }
        if (QFile::moveToTrash(path))
        {
            moved = true;
        }
        else
        {
            failed.append(path);
        }
    }

    if (!failed.isEmpty())
    {
        QMessageBox::warning(
            this, QStringLiteral("删除失败"),
            QStringLiteral("以下文件无法移入回收站：\n%1").arg(failed.join(QLatin1Char('\n'))));
        refreshCurrentFolder();
        return;
    }
    if (!moved)
    {
        QMessageBox::warning(this, QStringLiteral("删除失败"),
                             QStringLiteral("文件不存在或已被删除。"));
        refreshCurrentFolder();
        return;
    }

    for (int i = _items.size() - 1; i >= 0; --i)
    {
        const PhotoItemPtr& current = _items.at(i);
        if (current.get() == item.get()
            || (current->imagePath == item->imagePath
                && current->videoPath == item->videoPath))
        {
            _items.removeAt(i);
            break;
        }
    }

    for (LibraryPage* page : {_allPage, _livePage, _videoPage})
    {
        page->removeItem(item);
    }
    updateUserCard();
    requestCoverImage();
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (!event->mimeData()->hasUrls())
    {
        return;
    }
    for (const QUrl& url : event->mimeData()->urls())
    {
        if (url.isLocalFile() && QFileInfo(url.toLocalFile()).isDir())
        {
            event->acceptProposedAction();
            return;
        }
    }
}

void MainWindow::dropEvent(QDropEvent* event)
{
    for (const QUrl& url : event->mimeData()->urls())
    {
        if (!url.isLocalFile())
        {
            continue;
        }
        const QFileInfo info(url.toLocalFile());
        if (info.isDir())
        {
            loadFolder(info.absoluteFilePath());
            event->acceptProposedAction();
            return;
        }
    }
}
