#ifndef PHOTOVIEWER_H
#define PHOTOVIEWER_H

#include "PhotoItem.h"

#include <QDialog>
#include <QSize>
#include <QString>

class ElaIconButton;
class MediaCanvas;
class ThumbnailService;
class QAudioOutput;
class QLabel;
class QMediaPlayer;
class QPropertyAnimation;
class QSizeGrip;
class QSlider;
class QStackedWidget;
class VideoCanvas;
class QWidget;

class PhotoViewer : public QDialog
{
    Q_OBJECT
public:
    explicit PhotoViewer(ThumbnailService* thumbnails, QWidget* parent = nullptr);
    ~PhotoViewer() override;

    void openItems(const PhotoItemList& items, int index);
    bool livePlaybackEnabled() const { return _liveEnabled; }
    void setLivePlaybackEnabled(bool enabled);

signals:
    void livePlaybackEnabledChanged(bool enabled);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    void setIndex(int index);
    void navigate(int delta);
    void loadCurrentImage();
    void updateTexts();
    void updateInfoPanel();
    void updatePlaybackUi();
    void updateProgressUi();
    void toggleInfoPanel();
    void startLivePreviewOnce();
    void startLiveLoop();
    void stopLivePlayback();
    void setupVideoOnly();
    void togglePlayback();
    void toggleMute();
    void toggleMaximize();
    void toggleStayOnTop();
    void updateMaximizeButton();
    void applyWindowCorners();
    void saveWindowState();
    bool isLivePlaying() const { return _livePlaying; }
    QString formatTime(qint64 milliseconds) const;
    void applyIconStyle(ElaIconButton* button) const;

    ThumbnailService* _thumbnails{nullptr};
    PhotoItemList _items;
    int _index{-1};
    QString _viewerKey;
    QSize _currentImageSize;

    QStackedWidget* _mediaStack{nullptr};
    MediaCanvas* _canvas{nullptr};
    VideoCanvas* _videoCanvas{nullptr};
    QMediaPlayer* _player{nullptr};
    QAudioOutput* _audioOutput{nullptr};

    QWidget* _topBar{nullptr};
    QWidget* _dragArea{nullptr};
    QWidget* _progressRow{nullptr};
    QWidget* _bottomBar{nullptr};
    QWidget* _infoPanel{nullptr};
    QLabel* _titleLabel{nullptr};
    QLabel* _counterLabel{nullptr};
    QLabel* _zoomLabel{nullptr};
    QLabel* _positionLabel{nullptr};
    QLabel* _durationLabel{nullptr};
    QLabel* _liveTag{nullptr};
    QLabel* _infoText{nullptr};
    QSlider* _seekSlider{nullptr};
    QSizeGrip* _sizeGrip{nullptr};

    ElaIconButton* _closeButton{nullptr};
    ElaIconButton* _minimizeButton{nullptr};
    ElaIconButton* _maximizeButton{nullptr};
    ElaIconButton* _pinButton{nullptr};
    ElaIconButton* _prevButton{nullptr};
    ElaIconButton* _nextButton{nullptr};
    ElaIconButton* _playButton{nullptr};
    ElaIconButton* _muteButton{nullptr};
    ElaIconButton* _infoButton{nullptr};
    ElaIconButton* _zoomButton{nullptr};

    QPropertyAnimation* _infoAnimation{nullptr};
    bool _infoVisible{false};
    bool _livePlaying{false};
    bool _liveEnabled{true};
    bool _videoOnly{false};
    bool _muted{false};
    bool _seeking{false};
    bool _liveHolding{false};
    bool _previewingOnce{false};
    bool _stayOnTop{false};
    bool _dwmCorners{false};
};

#endif // PHOTOVIEWER_H
