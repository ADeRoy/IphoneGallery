#include "PhotoViewer.h"

#include "ElaDef.h"
#include "ElaIcon.h"
#include "ElaIconButton.h"
#include "MediaCanvas.h"
#include "MediaUtils.h"
#include "ThumbnailService.h"
#include "VideoCanvas.h"

#include <QAudioOutput>
#include <QCloseEvent>
#include <QDebug>
#include <QFileInfo>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QMediaMetaData>
#include <QMediaPlayer>
#include <QMouseEvent>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QRegion>
#include <QResizeEvent>
#include <QScreen>
#include <QSettings>
#include <QSignalBlocker>
#include <QSizeGrip>
#include <QSlider>
#include <QStackedWidget>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWindow>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#endif

namespace
{
constexpr int ViewerImageEdge = 3072;
constexpr int InfoPanelWidth = 300;

const char* kStyleSheet = R"(
#PhotoViewer { background: #0F0F12; }
#ViewerTopBar { background: #121216; border-bottom: 1px solid #26262C; }
#ViewerBottomBar { background: #121216; border-top: 1px solid #26262C; }
#ViewerInfoPanel { background: #17171B; border-left: 1px solid #26262C; }
QLabel#ViewerTitle { color: #F5F5F7; font-size: 13px; font-weight: 600; }
QLabel#ViewerSub { color: #92929C; font-size: 12px; }
QLabel#ViewerLiveTag { color: #9A9AA5; font-size: 12px; }
QLabel#ViewerInfoText { color: #E9E9EE; font-size: 13px; }
QSlider#ViewerSeek::groove:horizontal { height: 3px; background: #3A3A44; border-radius: 1px; }
QSlider#ViewerSeek::sub-page:horizontal { background: #0A84FF; border-radius: 1px; }
QSlider#ViewerSeek::handle:horizontal {
    background: #FFFFFF; width: 12px; height: 12px; margin: -5px 0; border-radius: 6px;
}
)";

QString escaped(const QString& text)
{
    return text.toHtmlEscaped();
}
} // namespace

PhotoViewer::PhotoViewer(ThumbnailService* thumbnails, QWidget* parent)
    : QDialog(parent)
    , _thumbnails(thumbnails)
{
    setObjectName(QStringLiteral("PhotoViewer"));
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setWindowTitle(QStringLiteral("照片查看器"));
    setAttribute(Qt::WA_DeleteOnClose, false);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(640, 460);
    setStyleSheet(QString::fromUtf8(kStyleSheet));
    if (parentWidget())
    {
        setWindowIcon(parentWidget()->windowIcon());
    }

    QSettings settings(QStringLiteral("IphoneGallery"), QStringLiteral("IphoneGallery"));
    _liveEnabled = settings.value(QStringLiteral("livePlayback"), true).toBool();
    _stayOnTop = settings.value(QStringLiteral("viewerOnTop"), false).toBool();
    if (!restoreGeometry(settings.value(QStringLiteral("viewerGeometry")).toByteArray()))
    {
        resize(960, 680);
    }
    if (_stayOnTop)
    {
        setWindowFlag(Qt::WindowStaysOnTopHint, true);
    }

    _player = new QMediaPlayer(this);
    _audioOutput = new QAudioOutput(this);
    _player->setAudioOutput(_audioOutput);

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    _topBar = new QWidget(this);
    _topBar->setObjectName(QStringLiteral("ViewerTopBar"));
    _topBar->setFixedHeight(48);
    _topBar->installEventFilter(this);
    auto* topLayout = new QHBoxLayout(_topBar);
    topLayout->setContentsMargins(12, 0, 8, 0);
    topLayout->setSpacing(4);

    auto makeButton = [this](ElaIconType::IconName icon) {
        auto* button = new ElaIconButton(icon, 16, 34, 30, this);
        button->setBorderRadius(8);
        button->setCursor(Qt::PointingHandCursor);
        applyIconStyle(button);
        return button;
    };

    _dragArea = new QWidget(_topBar);
    _dragArea->installEventFilter(this);
    auto* dragLayout = new QHBoxLayout(_dragArea);
    dragLayout->setContentsMargins(2, 0, 0, 0);
    dragLayout->setSpacing(8);

    auto* iconLabel = new QLabel(_dragArea);
    iconLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    iconLabel->setPixmap(ElaIcon::getInstance()
                             ->getElaIcon(ElaIconType::Images, 15, QColor(200, 200, 208))
                             .pixmap(16, 16));
    iconLabel->setFixedSize(18, 18);

    _titleLabel = new QLabel(_dragArea);
    _titleLabel->setObjectName(QStringLiteral("ViewerTitle"));
    _titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    _titleLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    _counterLabel = new QLabel(_dragArea);
    _counterLabel->setObjectName(QStringLiteral("ViewerSub"));
    _counterLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

    dragLayout->addWidget(iconLabel);
    dragLayout->addWidget(_titleLabel);
    dragLayout->addWidget(_counterLabel);
    dragLayout->addStretch();

    _pinButton = makeButton(ElaIconType::Thumbtack);
    _pinButton->setToolTip(QStringLiteral("窗口置顶"));
    _pinButton->setIsSelected(_stayOnTop);
    _minimizeButton = makeButton(ElaIconType::WindowMinimize);
    _minimizeButton->setToolTip(QStringLiteral("最小化"));
    _maximizeButton = makeButton(ElaIconType::WindowMaximize);
    _maximizeButton->setToolTip(QStringLiteral("最大化"));
    _closeButton = makeButton(ElaIconType::Xmark);
    _closeButton->setToolTip(QStringLiteral("关闭 (Esc)"));

    topLayout->addWidget(_dragArea, 1);
    topLayout->addWidget(_pinButton);
    topLayout->addWidget(_minimizeButton);
    topLayout->addWidget(_maximizeButton);
    topLayout->addWidget(_closeButton);
    rootLayout->addWidget(_topBar);

    auto* content = new QWidget(this);
    auto* contentLayout = new QHBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);

    _mediaStack = new QStackedWidget(content);
    _canvas = new MediaCanvas(_mediaStack);
    _canvas->installEventFilter(this);
    _videoCanvas = new VideoCanvas(_mediaStack);
    _videoCanvas->installEventFilter(this);
    _player->setVideoSink(_videoCanvas->videoSink());
    _mediaStack->addWidget(_canvas);
    _mediaStack->addWidget(_videoCanvas);
    contentLayout->addWidget(_mediaStack, 1);

    _infoPanel = new QWidget(content);
    _infoPanel->setObjectName(QStringLiteral("ViewerInfoPanel"));
    _infoPanel->setMinimumWidth(0);
    _infoPanel->setMaximumWidth(0);
    _infoPanel->setVisible(false);
    auto* infoLayout = new QVBoxLayout(_infoPanel);
    infoLayout->setContentsMargins(22, 24, 22, 24);
    infoLayout->setSpacing(12);

    _infoText = new QLabel(_infoPanel);
    _infoText->setObjectName(QStringLiteral("ViewerInfoText"));
    _infoText->setWordWrap(true);
    _infoText->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    _infoText->setTextInteractionFlags(Qt::TextSelectableByMouse);
    infoLayout->addWidget(_infoText);
    infoLayout->addStretch();
    contentLayout->addWidget(_infoPanel);

    rootLayout->addWidget(content, 1);

    _bottomBar = new QWidget(this);
    _bottomBar->setObjectName(QStringLiteral("ViewerBottomBar"));
    _bottomBar->setFixedHeight(86);
    auto* bottomLayout = new QVBoxLayout(_bottomBar);
    bottomLayout->setContentsMargins(0, 0, 0, 0);
    bottomLayout->setSpacing(0);

    _progressRow = new QWidget(_bottomBar);
    auto* progressLayout = new QHBoxLayout(_progressRow);
    progressLayout->setContentsMargins(18, 6, 18, 0);
    progressLayout->setSpacing(8);

    _positionLabel = new QLabel(QStringLiteral("0:00"), _progressRow);
    _positionLabel->setObjectName(QStringLiteral("ViewerSub"));
    _positionLabel->setFixedWidth(44);
    _positionLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    _seekSlider = new QSlider(Qt::Horizontal, _progressRow);
    _seekSlider->setObjectName(QStringLiteral("ViewerSeek"));
    _seekSlider->setRange(0, 0);
    _seekSlider->setFixedHeight(20);

    _durationLabel = new QLabel(QStringLiteral("0:00"), _progressRow);
    _durationLabel->setObjectName(QStringLiteral("ViewerSub"));
    _durationLabel->setFixedWidth(44);
    _durationLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    progressLayout->addWidget(_positionLabel);
    progressLayout->addWidget(_seekSlider, 1);
    progressLayout->addWidget(_durationLabel);
    bottomLayout->addWidget(_progressRow);

    auto* controlsRow = new QWidget(_bottomBar);
    auto* controlsLayout = new QHBoxLayout(controlsRow);
    controlsLayout->setContentsMargins(14, 0, 10, 6);
    controlsLayout->setSpacing(6);

    _prevButton = makeButton(ElaIconType::ChevronLeft);
    _prevButton->setToolTip(QStringLiteral("上一张 (←)"));
    _playButton = makeButton(ElaIconType::Play);
    _playButton->setToolTip(QStringLiteral("播放 (空格)"));
    _nextButton = makeButton(ElaIconType::ChevronRight);
    _nextButton->setToolTip(QStringLiteral("下一张 (→)"));

    _liveTag = new QLabel(QStringLiteral("实况照片"), controlsRow);
    _liveTag->setObjectName(QStringLiteral("ViewerLiveTag"));
    _liveTag->setContentsMargins(6, 0, 6, 0);

    _zoomLabel = new QLabel(QStringLiteral("--"), controlsRow);
    _zoomLabel->setObjectName(QStringLiteral("ViewerSub"));
    _zoomButton = makeButton(ElaIconType::Expand);
    _zoomButton->setToolTip(QStringLiteral("适应窗口 (0)"));
    _infoButton = makeButton(ElaIconType::Info);
    _infoButton->setToolTip(QStringLiteral("显示信息 (F)"));
    _muteButton = makeButton(ElaIconType::Volume);
    _muteButton->setToolTip(QStringLiteral("静音"));

    _sizeGrip = new QSizeGrip(_bottomBar);
    _sizeGrip->setFixedSize(16, 16);
    _sizeGrip->setStyleSheet(QStringLiteral("background:transparent;"));

    controlsLayout->addWidget(_prevButton);
    controlsLayout->addWidget(_playButton);
    controlsLayout->addWidget(_nextButton);
    controlsLayout->addWidget(_liveTag);
    controlsLayout->addStretch();
    controlsLayout->addWidget(_zoomLabel);
    controlsLayout->addWidget(_zoomButton);
    controlsLayout->addWidget(_infoButton);
    controlsLayout->addWidget(_muteButton);
    controlsLayout->addWidget(_sizeGrip);
    bottomLayout->addWidget(controlsRow, 1);
    rootLayout->addWidget(_bottomBar);

    _infoAnimation = new QPropertyAnimation(_infoPanel, "maximumWidth", this);
    _infoAnimation->setDuration(220);
    _infoAnimation->setEasingCurve(QEasingCurve::OutCubic);
    connect(_infoAnimation, &QPropertyAnimation::finished, this, [this]() {
        if (_infoVisible)
        {
            _infoPanel->setMinimumWidth(InfoPanelWidth);
        }
        else
        {
            _infoPanel->setVisible(false);
        }
    });

    connect(_closeButton, &ElaIconButton::clicked, this, &QDialog::close);
    connect(_minimizeButton, &ElaIconButton::clicked, this, &QWidget::showMinimized);
    connect(_maximizeButton, &ElaIconButton::clicked, this, &PhotoViewer::toggleMaximize);
    connect(_pinButton, &ElaIconButton::clicked, this, &PhotoViewer::toggleStayOnTop);
    connect(_prevButton, &ElaIconButton::clicked, this, [this]() { navigate(-1); });
    connect(_nextButton, &ElaIconButton::clicked, this, [this]() { navigate(1); });
    connect(_playButton, &ElaIconButton::clicked, this, &PhotoViewer::togglePlayback);
    connect(_muteButton, &ElaIconButton::clicked, this, &PhotoViewer::toggleMute);
    connect(_infoButton, &ElaIconButton::clicked, this, &PhotoViewer::toggleInfoPanel);
    connect(_zoomButton, &ElaIconButton::clicked, _canvas, &MediaCanvas::resetView);
    connect(_canvas, &MediaCanvas::zoomChanged, this, [this](double zoom) {
        _zoomLabel->setText(QStringLiteral("%1%").arg(qRound(zoom * 100.0)));
    });

    connect(_player, &QMediaPlayer::playbackStateChanged, this,
            [this](QMediaPlayer::PlaybackState state) {
                qDebug() << "player state" << state;
                updatePlaybackUi();
            });
    connect(_player, &QMediaPlayer::mediaStatusChanged, this,
            [this](QMediaPlayer::MediaStatus status) {
                qDebug() << "player media status" << status;
                if (status == QMediaPlayer::EndOfMedia && _previewingOnce && !_liveHolding)
                {
                    QTimer::singleShot(0, this, [this]() {
                        if (_previewingOnce && !_liveHolding)
                        {
                            stopLivePlayback();
                        }
                    });
                }
            });
    connect(_player, &QMediaPlayer::hasVideoChanged, this, [this](bool hasVideo) {
        qDebug() << "player hasVideo" << hasVideo;
    });
    connect(_player, &QMediaPlayer::durationChanged, this, [this](qint64 duration) {
        _seekSlider->setRange(0, static_cast<int>(qMax<qint64>(0, duration)));
        _durationLabel->setText(formatTime(duration));
    });
    connect(_player, &QMediaPlayer::positionChanged, this, [this](qint64 position) {
        if (!_seeking && _seekSlider->isVisible())
        {
            _seekSlider->setValue(static_cast<int>(position));
            _positionLabel->setText(formatTime(position));
        }
    });
    connect(_player, &QMediaPlayer::metaDataChanged, this, [this]() {
        updateInfoPanel();
    });
    connect(_player, &QMediaPlayer::errorOccurred, this,
            [this](QMediaPlayer::Error, const QString& errorString) {
                qDebug() << "player error" << errorString;
                if (_items.isEmpty() || _index < 0)
                {
                    return;
                }
                const PhotoItemPtr item = _items.at(_index);
                if (item->isLivePhoto && item->hasImage())
                {
                    stopLivePlayback();
                    return;
                }
                if (!_canvas->hasImage())
                {
                    _canvas->clearImage();
                    _canvas->setMessage(QStringLiteral("无法播放此视频\n%1").arg(errorString));
                }
                _mediaStack->setCurrentWidget(_canvas);
                _livePlaying = false;
                _previewingOnce = false;
                updatePlaybackUi();
            });
    connect(_seekSlider, &QSlider::sliderPressed, this, [this]() { _seeking = true; });
    connect(_seekSlider, &QSlider::sliderMoved, this, [this](int value) {
        if (_videoOnly)
        {
            _player->setPosition(value);
            _positionLabel->setText(formatTime(value));
        }
    });
    connect(_seekSlider, &QSlider::sliderReleased, this, [this]() {
        _seeking = false;
        if (_videoOnly)
        {
            _player->setPosition(_seekSlider->value());
        }
    });

    if (_thumbnails)
    {
        connect(_thumbnails, &ThumbnailService::viewerImageReady, this,
                [this](const QString& key, const QImage& image) {
                    if (key != _viewerKey)
                    {
                        return;
                    }
                    _currentImageSize = image.size();
                    _canvas->setImage(image);
                    updateInfoPanel();
                });
        connect(_thumbnails, &ThumbnailService::imageFailed, this,
                [this](const QString& key) {
                    if (key != _viewerKey)
                    {
                        return;
                    }
                    _canvas->setLoading(false);
                    _canvas->setMessage(
                        QStringLiteral("无法载入这张照片\nHEIC 照片需要 FFmpeg 支持，请确认 ffmpeg 已安装。"));
                });
    }

    updatePlaybackUi();
    updateMaximizeButton();
}

PhotoViewer::~PhotoViewer() = default;

void PhotoViewer::applyIconStyle(ElaIconButton* button) const
{
    const QColor iconColor(236, 236, 242);
    const QColor hoverIconColor(255, 255, 255);
    const QColor hoverColor(255, 255, 255, 26);
    button->setLightIconColor(iconColor);
    button->setDarkIconColor(iconColor);
    button->setLightHoverIconColor(hoverIconColor);
    button->setDarkHoverIconColor(hoverIconColor);
    button->setLightHoverColor(hoverColor);
    button->setDarkHoverColor(hoverColor);
}

void PhotoViewer::openItems(const PhotoItemList& items, int index)
{
    if (items.isEmpty())
    {
        return;
    }
    _items = items;
    _index = qBound(0, index, items.size() - 1);
    qDebug() << "viewer open index" << _index << "of" << items.size();

    if (!isVisible())
    {
        QRect geometry = this->geometry();
        QScreen* screen = parentWidget() ? parentWidget()->screen()
                                         : QGuiApplication::primaryScreen();
        if (screen)
        {
            const QRect available = screen->availableGeometry();
            if (!available.intersects(geometry) || geometry.width() < minimumWidth()
                || geometry.height() < minimumHeight())
            {
                geometry.setSize(QSize(960, 680));
                geometry.moveCenter(available.center());
                setGeometry(geometry);
            }
        }
        showNormal();
        raise();
        activateWindow();
        setFocus(Qt::OtherFocusReason);
        QTimer::singleShot(0, this, [this]() { applyWindowCorners(); });
    }
    else
    {
        if (isMinimized())
        {
            showNormal();
        }
        raise();
        activateWindow();
    }
    setIndex(_index);
}

void PhotoViewer::setIndex(int index)
{
    if (_items.isEmpty())
    {
        return;
    }
    if (_liveHolding)
    {
        releaseMouse();
        _liveHolding = false;
    }
    stopLivePlayback();
    _index = qBound(0, index, _items.size() - 1);
    _viewerKey.clear();
    _currentImageSize = QSize();
    _liveHolding = false;
    _previewingOnce = false;

    const PhotoItemPtr item = _items.at(_index);
    _videoOnly = item->hasVideo() && !item->hasImage();
    _videoCanvas->clearFrame();
    qDebug() << "viewer setIndex" << _index << item->imagePath << item->videoPath
             << "live" << item->isLivePhoto << "videoOnly" << _videoOnly
             << "rotation" << item->rotationDegrees;

    const bool hasVideo = item->hasVideo();
    _playButton->setVisible(hasVideo);
    _liveTag->setVisible(item->isLivePhoto || _videoOnly);
    _muteButton->setVisible(hasVideo);
    _progressRow->setVisible(hasVideo);
    _bottomBar->setFixedHeight(hasVideo ? 86 : 58);
    _seekSlider->setEnabled(_videoOnly);
    _seekSlider->setRange(0, 0);
    _seekSlider->setValue(0);
    _positionLabel->setText(QStringLiteral("0:00"));
    _durationLabel->setText(QStringLiteral("0:00"));
    _canvas->setToolTip(item->isLivePhoto ? QStringLiteral("按住画面循环播放实况，松开显示照片")
                                           : QString());

    if (!hasVideo)
    {
        _player->setSource(QUrl());
    }

    if (_videoOnly)
    {
        setupVideoOnly();
    }
    else if (item->hasImage())
    {
        loadCurrentImage();
    }
    else
    {
        _mediaStack->setCurrentWidget(_canvas);
        _canvas->setMessage(QStringLiteral("没有可显示的内容"));
    }

    if (item->isLivePhoto && hasVideo && _liveEnabled)
    {
        startLivePreviewOnce();
    }

    updateTexts();
    updateInfoPanel();
    updatePlaybackUi();
    updateProgressUi();
}

void PhotoViewer::loadCurrentImage()
{
    const PhotoItemPtr item = _items.at(_index);
    if (!item->hasImage() || !_thumbnails)
    {
        return;
    }
    _mediaStack->setCurrentWidget(_canvas);
    _canvas->clearImage();
    _canvas->setLoading(true);
    _viewerKey = ThumbnailService::viewerKey(item, ViewerImageEdge);
    _thumbnails->requestViewerImage(item, ViewerImageEdge);
}

void PhotoViewer::setupVideoOnly()
{
    const PhotoItemPtr item = _items.at(_index);
    qDebug() << "setup video only" << item->videoPath;
    _videoCanvas->clearFrame();
    _mediaStack->setCurrentWidget(_videoCanvas);
    _player->setLoops(1);
    _player->setSource(QUrl::fromLocalFile(item->videoPath));
    _player->setPosition(0);
    _player->play();
}

void PhotoViewer::startLivePreviewOnce()
{
    const PhotoItemPtr item = _items.at(_index);
    if (!item || !item->isLivePhoto || !item->hasVideo())
    {
        return;
    }
    qDebug() << "start live preview once";
    _previewingOnce = true;
    _livePlaying = true;
    _videoCanvas->clearFrame();
    _player->setLoops(1);
    _player->setSource(QUrl::fromLocalFile(item->videoPath));
    _player->setPosition(0);
    _mediaStack->setCurrentWidget(_videoCanvas);
    _player->play();
    updatePlaybackUi();
    updateProgressUi();
}

void PhotoViewer::startLiveLoop()
{
    const PhotoItemPtr item = _items.at(_index);
    if (!item || !item->isLivePhoto || !item->hasVideo())
    {
        return;
    }
    qDebug() << "start live loop";
    _previewingOnce = false;
    _livePlaying = true;
    _videoCanvas->clearFrame();
    _player->setLoops(QMediaPlayer::Infinite);
    const QUrl source = QUrl::fromLocalFile(item->videoPath);
    if (_player->source() != source)
    {
        _player->setSource(source);
    }
    _player->setPosition(0);
    _mediaStack->setCurrentWidget(_videoCanvas);
    _player->play();
    updatePlaybackUi();
    updateProgressUi();
}

void PhotoViewer::stopLivePlayback()
{
    qDebug() << "stop live playback";
    _player->stop();
    _livePlaying = false;
    _previewingOnce = false;
    if (_mediaStack)
    {
        _mediaStack->setCurrentWidget(_canvas);
    }
    _seekSlider->setValue(0);
    _positionLabel->setText(QStringLiteral("0:00"));
    updatePlaybackUi();
    updateProgressUi();
}

void PhotoViewer::navigate(int delta)
{
    if (_items.size() <= 1)
    {
        return;
    }
    int next = _index + delta;
    if (next < 0)
    {
        next = _items.size() - 1;
    }
    else if (next >= _items.size())
    {
        next = 0;
    }
    setIndex(next);
}

void PhotoViewer::updateTexts()
{
    if (_index < 0 || _index >= _items.size())
    {
        return;
    }
    const PhotoItemPtr item = _items.at(_index);
    const QString fileName =
        QFileInfo(item->hasImage() ? item->imagePath : item->videoPath).fileName();
    const QFontMetrics metrics(_titleLabel->font());
    _titleLabel->setText(metrics.elidedText(fileName, Qt::ElideMiddle, 320));
    _titleLabel->setToolTip(fileName);
    _counterLabel->setText(QStringLiteral("%1 / %2").arg(_index + 1).arg(_items.size()));
    setWindowTitle(fileName);
    if (!_canvas->hasImage())
    {
        _zoomLabel->setText(QStringLiteral("--"));
    }
}

void PhotoViewer::updateInfoPanel()
{
    if (_index < 0 || _index >= _items.size())
    {
        return;
    }
    const PhotoItemPtr item = _items.at(_index);
    QString type;
    QString primaryFile;
    if (item->isLivePhoto)
    {
        type = QStringLiteral("实况照片");
        primaryFile = QFileInfo(item->imagePath).fileName();
    }
    else if (item->hasImage())
    {
        type = QStringLiteral("照片");
        primaryFile = QFileInfo(item->imagePath).fileName();
    }
    else
    {
        type = QStringLiteral("视频");
        primaryFile = QFileInfo(item->videoPath).fileName();
    }

    QString dimensions;
    if (_currentImageSize.isValid())
    {
        dimensions = QStringLiteral("%1 × %2").arg(_currentImageSize.width()).arg(
            _currentImageSize.height());
    }
    else if (item->hasVideo())
    {
        const QSize videoSize = _player->metaData().value(QMediaMetaData::Resolution).toSize();
        if (videoSize.isValid())
        {
            dimensions = QStringLiteral("%1 × %2").arg(videoSize.width()).arg(videoSize.height());
        }
    }

    QStringList rows;
    rows << QStringLiteral("<div style='font-size:16px;font-weight:600;color:#F5F5F7;'>%1</div>")
                .arg(escaped(primaryFile));
    auto addRow = [&rows](const QString& label, const QString& value) {
        if (value.isEmpty())
        {
            return;
        }
        rows << QStringLiteral(
                    "<div style='margin-top:14px;color:#8F8F99;font-size:11px;'>%1</div>"
                    "<div style='margin-top:2px;color:#E9E9EE;font-size:13px;'>%2</div>")
                    .arg(escaped(label), escaped(value));
    };

    addRow(QStringLiteral("类型"), type);
    addRow(item->captureTime.isValid() ? QStringLiteral("拍摄时间")
                                       : QStringLiteral("文件时间"),
           MediaUtils::formatDate(item->dateTime()));
    addRow(QStringLiteral("尺寸"), dimensions);
    addRow(QStringLiteral("大小"),
           MediaUtils::formatBytes(item->imageBytes + item->videoBytes));
    addRow(QStringLiteral("位置"), item->folderPath);
    if (item->isLivePhoto)
    {
        addRow(QStringLiteral("实况短片"), QFileInfo(item->videoPath).fileName());
    }
    if (item->hasVideo() && item->rotationDegrees != 0)
    {
        addRow(QStringLiteral("视频旋转"),
               QStringLiteral("%1°").arg(item->rotationDegrees));
    }

    _infoText->setText(rows.join(QString()));
}

void PhotoViewer::updatePlaybackUi()
{
    const bool hasVideo = _index >= 0 && _index < _items.size()
                          && _items.at(_index)->hasVideo();
    const bool playing = _player->playbackState() == QMediaPlayer::PlayingState;
    _playButton->setVisible(hasVideo);
    if (hasVideo)
    {
        _playButton->setAwesome(playing ? ElaIconType::Pause : ElaIconType::Play);
        _playButton->setToolTip(playing ? QStringLiteral("暂停 (空格)")
                                        : QStringLiteral("播放 (空格)"));
    }
    if (_index >= 0 && _index < _items.size())
    {
        const PhotoItemPtr item = _items.at(_index);
        if (item->isLivePhoto)
        {
            _liveTag->setText(_livePlaying ? QStringLiteral("实况播放中")
                                           : QStringLiteral("实况照片"));
        }
        else if (_videoOnly)
        {
            _liveTag->setText(QStringLiteral("视频"));
        }
    }
}

void PhotoViewer::updateProgressUi()
{
    if (_index < 0 || _index >= _items.size())
    {
        return;
    }
    const PhotoItemPtr item = _items.at(_index);
    const bool hasVideo = item->hasVideo();
    _progressRow->setVisible(hasVideo);
    _bottomBar->setFixedHeight(hasVideo ? 86 : 58);
    _seekSlider->setEnabled(_videoOnly);
    if (!hasVideo)
    {
        return;
    }

    const qint64 duration = _player->duration();
    _durationLabel->setText(formatTime(duration));
    if (_videoOnly)
    {
        const qint64 position = _player->position();
        _seekSlider->setValue(static_cast<int>(position));
        _positionLabel->setText(formatTime(position));
    }
    else if (_livePlaying)
    {
        const qint64 position = _player->position();
        _seekSlider->setValue(static_cast<int>(position));
        _positionLabel->setText(formatTime(position));
    }
    else
    {
        _seekSlider->setValue(0);
        _positionLabel->setText(QStringLiteral("0:00"));
    }
}

void PhotoViewer::togglePlayback()
{
    if (_items.isEmpty() || _index < 0)
    {
        return;
    }
    const PhotoItemPtr item = _items.at(_index);
    if (item->isLivePhoto)
    {
        if (_livePlaying)
        {
            stopLivePlayback();
        }
        else
        {
            startLiveLoop();
        }
        return;
    }
    if (!_videoOnly)
    {
        return;
    }
    if (_player->playbackState() == QMediaPlayer::PlayingState)
    {
        _player->pause();
    }
    else
    {
        if (_player->mediaStatus() == QMediaPlayer::EndOfMedia)
        {
            _player->setPosition(0);
        }
        _player->play();
    }
}

void PhotoViewer::toggleMute()
{
    _muted = !_muted;
    _audioOutput->setMuted(_muted);
    _muteButton->setIsSelected(_muted);
    _muteButton->setToolTip(_muted ? QStringLiteral("取消静音") : QStringLiteral("静音"));
}

void PhotoViewer::setLivePlaybackEnabled(bool enabled)
{
    _liveEnabled = enabled;
    QSettings settings(QStringLiteral("IphoneGallery"), QStringLiteral("IphoneGallery"));
    settings.setValue(QStringLiteral("livePlayback"), enabled);
    emit livePlaybackEnabledChanged(enabled);

    if (_items.isEmpty() || _index < 0)
    {
        return;
    }
    const PhotoItemPtr item = _items.at(_index);
    if (!item->isLivePhoto || !item->hasVideo())
    {
        return;
    }
    if (enabled)
    {
        if (!_livePlaying)
        {
            startLivePreviewOnce();
        }
    }
    else if (_livePlaying)
    {
        stopLivePlayback();
    }
}

void PhotoViewer::toggleInfoPanel()
{
    _infoVisible = !_infoVisible;
    _infoButton->setIsSelected(_infoVisible);
    _infoAnimation->stop();
    if (_infoVisible)
    {
        _infoPanel->setMinimumWidth(0);
        _infoPanel->setVisible(true);
        _infoAnimation->setStartValue(_infoPanel->maximumWidth());
        _infoAnimation->setEndValue(InfoPanelWidth);
    }
    else
    {
        _infoPanel->setMinimumWidth(0);
        _infoAnimation->setStartValue(_infoPanel->width());
        _infoAnimation->setEndValue(0);
    }
    _infoAnimation->start();
}

void PhotoViewer::toggleMaximize()
{
    if (isMaximized())
    {
        showNormal();
    }
    else
    {
        showMaximized();
    }
    updateMaximizeButton();
    QTimer::singleShot(0, this, [this]() { applyWindowCorners(); });
}

void PhotoViewer::updateMaximizeButton()
{
    if (!_maximizeButton)
    {
        return;
    }
    _maximizeButton->setAwesome(isMaximized() ? ElaIconType::WindowRestore
                                              : ElaIconType::WindowMaximize);
    _maximizeButton->setToolTip(isMaximized() ? QStringLiteral("还原")
                                              : QStringLiteral("最大化"));
    applyIconStyle(_maximizeButton);
}

void PhotoViewer::toggleStayOnTop()
{
    _stayOnTop = !_stayOnTop;
    const QRect geometry = this->geometry();
    const bool maximized = isMaximized();
    setWindowFlag(Qt::WindowStaysOnTopHint, _stayOnTop);
    if (maximized)
    {
        showMaximized();
    }
    else
    {
        setGeometry(geometry);
        show();
    }
    _pinButton->setIsSelected(_stayOnTop);
    _pinButton->setToolTip(_stayOnTop ? QStringLiteral("取消置顶") : QStringLiteral("窗口置顶"));
    QSettings settings(QStringLiteral("IphoneGallery"), QStringLiteral("IphoneGallery"));
    settings.setValue(QStringLiteral("viewerOnTop"), _stayOnTop);
    applyWindowCorners();
    raise();
    activateWindow();
}

void PhotoViewer::applyWindowCorners()
{
#ifdef Q_OS_WIN
    const int preference = 2; // DWMWCP_ROUND
    const HRESULT result = DwmSetWindowAttribute(
        reinterpret_cast<HWND>(winId()), 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */,
        &preference, sizeof(preference));
    _dwmCorners = SUCCEEDED(result);
#endif
    if (_dwmCorners)
    {
        clearMask();
        return;
    }
    if (isMaximized() || isFullScreen())
    {
        clearMask();
        return;
    }
    QPainterPath path;
    path.addRoundedRect(QRectF(rect()), 14, 14);
    setMask(QRegion(path.toFillPolygon().toPolygon()));
}

void PhotoViewer::saveWindowState()
{
    QSettings settings(QStringLiteral("IphoneGallery"), QStringLiteral("IphoneGallery"));
    if (!isMaximized() && !isMinimized())
    {
        settings.setValue(QStringLiteral("viewerGeometry"), saveGeometry());
    }
    settings.setValue(QStringLiteral("viewerOnTop"), _stayOnTop);
    settings.setValue(QStringLiteral("livePlayback"), _liveEnabled);
}

QString PhotoViewer::formatTime(qint64 milliseconds) const
{
    const qint64 totalSeconds = qMax<qint64>(0, milliseconds / 1000);
    const qint64 hours = totalSeconds / 3600;
    const qint64 minutes = (totalSeconds % 3600) / 60;
    const qint64 seconds = totalSeconds % 60;
    if (hours > 0)
    {
        return QStringLiteral("%1:%2:%3")
            .arg(hours)
            .arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(seconds, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2")
        .arg(minutes)
        .arg(seconds, 2, 10, QLatin1Char('0'));
}

bool PhotoViewer::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == _canvas || watched == _videoCanvas)
    {
        if (event->type() == QEvent::MouseButtonPress)
        {
            auto* mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() == Qt::LeftButton && _index >= 0 && _index < _items.size()
                && _items.at(_index)->isLivePhoto && _items.at(_index)->hasVideo())
            {
                _liveHolding = true;
                grabMouse();
                startLiveLoop();
                return true;
            }
        }
        else if (event->type() == QEvent::MouseButtonRelease)
        {
            auto* mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() == Qt::LeftButton && _liveHolding)
            {
                _liveHolding = false;
                releaseMouse();
                stopLivePlayback();
                return true;
            }
        }
        else if (event->type() == QEvent::MouseButtonDblClick)
        {
            auto* mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() == Qt::LeftButton && _items.size() > 0 && _index >= 0
                && _items.at(_index)->isLivePhoto)
            {
                return true;
            }
        }
    }

    if (watched == _dragArea || watched == _topBar)
    {
        if (event->type() == QEvent::MouseButtonPress)
        {
            auto* mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() == Qt::LeftButton)
            {
                if (isMaximized())
                {
                    showNormal();
                }
                if (windowHandle())
                {
                    windowHandle()->startSystemMove();
                }
                return true;
            }
        }
        else if (event->type() == QEvent::MouseButtonDblClick)
        {
            auto* mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() == Qt::LeftButton)
            {
                toggleMaximize();
                return true;
            }
        }
    }

    return QDialog::eventFilter(watched, event);
}

void PhotoViewer::mouseReleaseEvent(QMouseEvent* event)
{
    if (_liveHolding && event->button() == Qt::LeftButton)
    {
        _liveHolding = false;
        releaseMouse();
        stopLivePlayback();
        event->accept();
        return;
    }
    QDialog::mouseReleaseEvent(event);
}

void PhotoViewer::keyPressEvent(QKeyEvent* event)
{
    switch (event->key())
    {
    case Qt::Key_Escape:
        if (_livePlaying || _liveHolding)
        {
            if (_liveHolding)
            {
                releaseMouse();
            }
            _liveHolding = false;
            stopLivePlayback();
        }
        else if (_infoVisible)
        {
            toggleInfoPanel();
        }
        else
        {
            close();
        }
        event->accept();
        return;
    case Qt::Key_Left:
    case Qt::Key_PageUp:
        navigate(-1);
        event->accept();
        return;
    case Qt::Key_Right:
    case Qt::Key_PageDown:
        navigate(1);
        event->accept();
        return;
    case Qt::Key_Space:
        togglePlayback();
        event->accept();
        return;
    case Qt::Key_F:
    case Qt::Key_I:
        toggleInfoPanel();
        event->accept();
        return;
    case Qt::Key_0:
        _canvas->resetView();
        event->accept();
        return;
    case Qt::Key_1:
        _canvas->setActualSize();
        event->accept();
        return;
    case Qt::Key_Plus:
    case Qt::Key_Equal:
        _canvas->zoomBy(1.2);
        event->accept();
        return;
    case Qt::Key_Minus:
        _canvas->zoomBy(1.0 / 1.2);
        event->accept();
        return;
    case Qt::Key_M:
        toggleMute();
        event->accept();
        return;
    default:
        break;
    }
    QDialog::keyPressEvent(event);
}

void PhotoViewer::closeEvent(QCloseEvent* event)
{
    saveWindowState();
    if (_liveHolding)
    {
        releaseMouse();
    }
    _liveHolding = false;
    stopLivePlayback();
    _player->setSource(QUrl());
    _items.clear();
    _index = -1;
    _viewerKey.clear();
    _canvas->clearImage();
    QDialog::closeEvent(event);
}

void PhotoViewer::resizeEvent(QResizeEvent* event)
{
    QDialog::resizeEvent(event);
    if (!_dwmCorners)
    {
        applyWindowCorners();
    }
}

void PhotoViewer::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
#ifdef Q_OS_WIN
    const HWND hwnd = reinterpret_cast<HWND>(winId());
    const LONG_PTR style = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
    SetWindowLongPtr(hwnd, GWL_EXSTYLE, style | WS_EX_APPWINDOW);
#endif
    applyWindowCorners();
    updateMaximizeButton();
}

void PhotoViewer::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::WindowStateChange)
    {
        updateMaximizeButton();
        if (!_dwmCorners)
        {
            applyWindowCorners();
        }
    }
    else if (event->type() == QEvent::ActivationChange && !isActiveWindow() && _liveHolding)
    {
        releaseMouse();
        _liveHolding = false;
        stopLivePlayback();
    }
    QDialog::changeEvent(event);
}
