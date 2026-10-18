#ifndef THUMBNAILSERVICE_H
#define THUMBNAILSERVICE_H

#include "PhotoItem.h"

#include <QCache>
#include <QImage>
#include <QObject>
#include <QSet>
#include <QString>

class QThreadPool;

class ThumbnailService : public QObject
{
    Q_OBJECT
public:
    explicit ThumbnailService(QObject* parent = nullptr);
    ~ThumbnailService() override;

    static QString thumbnailKey(const PhotoItemPtr& item, int edge);
    static QString viewerKey(const PhotoItemPtr& item, int edge);

    QImage cachedThumbnail(const QString& key) const;
    bool isThumbnailPending(const QString& key) const;

    void requestThumbnail(const PhotoItemPtr& item, int edge = 512);
    void requestViewerImage(const PhotoItemPtr& item, int maxEdge = 3072);

signals:
    void thumbnailReady(const QString& key, const QImage& image);
    void viewerImageReady(const QString& key, const QImage& image);
    void imageFailed(const QString& key);

private:
    QString cacheDirectory() const;
    QString diskPathForKey(const QString& key, bool viewer) const;
    void enqueue(const PhotoItemPtr& item, const QString& key, int edge, bool viewer);
    QImage generateThumbnail(const PhotoItemPtr& item, int edge) const;
    QImage generateViewerImage(const PhotoItemPtr& item, int maxEdge) const;
    bool runFfmpeg(const QStringList& arguments, int timeoutMs = 45000) const;
    bool fetchHeicFrame(const QString& input, int edge, const QString& output) const;
    bool fetchVideoFrame(const QString& input, int edge, const QString& output) const;
    void storeImage(const QImage& image, const QString& output) const;

    QThreadPool* _pool{nullptr};
    mutable QCache<QString, QImage> _memory;
    QSet<QString> _pendingThumbnails;
    QSet<QString> _pendingViewer;
    QString _cacheRoot;
};

#endif // THUMBNAILSERVICE_H
