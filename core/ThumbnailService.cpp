#include "ThumbnailService.h"

#include "MediaUtils.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRunnable>
#include <QStandardPaths>
#include <QThreadPool>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace
{
QString fileStamp(const QString& path)
{
    const QFileInfo info(path);
    return path + QLatin1Char('|') + QString::number(info.size()) + QLatin1Char('|')
           + QString::number(info.lastModified().toMSecsSinceEpoch());
}

QString makeHash(const QString& source)
{
    return QString::fromLatin1(QCryptographicHash::hash(source.toUtf8(), QCryptographicHash::Md5).toHex());
}

bool atomicRename(const QString& from, const QString& to)
{
    QFile::remove(to);
    return QFile::rename(from, to);
}
} // namespace

ThumbnailService::ThumbnailService(QObject* parent)
    : QObject(parent)
    , _pool(new QThreadPool(this))
{
    _pool->setMaxThreadCount(4);
    _memory.setMaxCost(320 * 1024);
    _cacheRoot = cacheDirectory();
}

ThumbnailService::~ThumbnailService()
{
    _pool->waitForDone(3000);
}

QString ThumbnailService::thumbnailKey(const PhotoItemPtr& item, int edge)
{
    if (!item)
    {
        return {};
    }
    const QString source = fileStamp(item->hasImage() ? item->imagePath : item->videoPath)
                           + QStringLiteral("|thumb-v2|") + QString::number(edge);
    return makeHash(source);
}

QString ThumbnailService::viewerKey(const PhotoItemPtr& item, int edge)
{
    if (!item)
    {
        return {};
    }
    const QString source = fileStamp(item->hasImage() ? item->imagePath : item->videoPath)
                           + QStringLiteral("|view-v2|") + QString::number(edge);
    return makeHash(source);
}

QString ThumbnailService::cacheDirectory() const
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (base.isEmpty())
    {
        base = QDir::tempPath() + QStringLiteral("/IphoneGallery");
    }
    QDir dir(base);
    dir.mkpath(QStringLiteral("thumbs"));
    dir.mkpath(QStringLiteral("viewer"));
    return dir.absolutePath();
}

QString ThumbnailService::diskPathForKey(const QString& key, bool viewer) const
{
    return _cacheRoot + (viewer ? QStringLiteral("/viewer/") : QStringLiteral("/thumbs/")) + key
           + QStringLiteral(".jpg");
}

QImage ThumbnailService::cachedThumbnail(const QString& key) const
{
    if (key.isEmpty())
    {
        return {};
    }
    if (QImage* image = _memory.object(key))
    {
        return *image;
    }
    return {};
}

bool ThumbnailService::isThumbnailPending(const QString& key) const
{
    return _pendingThumbnails.contains(key);
}

void ThumbnailService::requestThumbnail(const PhotoItemPtr& item, int edge)
{
    if (!item)
    {
        return;
    }
    const QString key = thumbnailKey(item, edge);
    if (key.isEmpty() || _pendingThumbnails.contains(key))
    {
        return;
    }

    if (QImage* image = _memory.object(key))
    {
        emit thumbnailReady(key, *image);
        return;
    }

    _pendingThumbnails.insert(key);
    enqueue(item, key, edge, false);
}

void ThumbnailService::requestViewerImage(const PhotoItemPtr& item, int maxEdge)
{
    if (!item)
    {
        return;
    }
    const QString key = viewerKey(item, maxEdge);
    if (key.isEmpty() || _pendingViewer.contains(key))
    {
        return;
    }
    if (QImage* image = _memory.object(key))
    {
        emit viewerImageReady(key, *image);
        return;
    }

    _pendingViewer.insert(key);
    enqueue(item, key, maxEdge, true);
}

void ThumbnailService::enqueue(const PhotoItemPtr& item, const QString& key, int edge, bool viewer)
{
    QThreadPool* pool = _pool;
    auto* self = this;
    pool->start(QRunnable::create([self, item, key, edge, viewer]() {
        QImage image;
        const QString diskPath = self->diskPathForKey(key, viewer);
        if (QFileInfo::exists(diskPath))
        {
            image = QImage(diskPath);
        }
        if (image.isNull())
        {
            image = viewer ? self->generateViewerImage(item, edge)
                           : self->generateThumbnail(item, edge);
            if (!image.isNull())
            {
                self->storeImage(image, diskPath);
            }
        }

        QMetaObject::invokeMethod(
            self,
            [self, key, image, viewer]() {
                if (viewer)
                {
                    self->_pendingViewer.remove(key);
                }
                else
                {
                    self->_pendingThumbnails.remove(key);
                }

                if (image.isNull())
                {
                    emit self->imageFailed(key);
                    return;
                }

                const int cost = qMax(1, (image.width() * image.height() * 4) / 1024);
                self->_memory.insert(key, new QImage(image), cost);
                if (viewer)
                {
                    emit self->viewerImageReady(key, image);
                }
                else
                {
                    emit self->thumbnailReady(key, image);
                }
            },
            Qt::QueuedConnection);
    }));
}

QImage ThumbnailService::generateThumbnail(const PhotoItemPtr& item, int edge) const
{
    if (item->hasImage())
    {
        if (MediaUtils::isHeicFile(item->imagePath))
        {
            const QString tempPath = _cacheRoot + QStringLiteral("/thumbs/.tmp_") + QString::number(quintptr(QThread::currentThreadId())) + QStringLiteral(".jpg");
            if (fetchHeicFrame(item->imagePath, edge, tempPath))
            {
                QImage image(tempPath);
                QFile::remove(tempPath);
                if (!image.isNull())
                {
                    return image;
                }
            }
        }
        else
        {
            bool ok = false;
            const QImage image = MediaUtils::loadImageScaled(item->imagePath, edge, &ok);
            if (ok && !image.isNull())
            {
                return image;
            }
        }
    }

    if (item->hasVideo())
    {
        const QString tempPath = _cacheRoot + QStringLiteral("/thumbs/.tmpv_") + QString::number(quintptr(QThread::currentThreadId())) + QStringLiteral(".jpg");
        if (fetchVideoFrame(item->videoPath, edge, tempPath))
        {
            QImage image(tempPath);
            QFile::remove(tempPath);
            if (!image.isNull())
            {
                return image;
            }
        }
    }
    return {};
}

QImage ThumbnailService::generateViewerImage(const PhotoItemPtr& item, int maxEdge) const
{
    if (item->hasImage())
    {
        if (MediaUtils::isHeicFile(item->imagePath))
        {
            const QString tempPath = _cacheRoot + QStringLiteral("/viewer/.tmp_") + QString::number(quintptr(QThread::currentThreadId())) + QStringLiteral(".jpg");
            if (fetchHeicFrame(item->imagePath, maxEdge, tempPath))
            {
                QImage image(tempPath);
                QFile::remove(tempPath);
                if (!image.isNull())
                {
                    return image;
                }
            }
        }
        else
        {
            bool ok = false;
            const QImage image = MediaUtils::loadImageScaled(item->imagePath, maxEdge, &ok);
            if (ok && !image.isNull())
            {
                return image;
            }
        }
    }

    if (item->hasVideo())
    {
        const QString tempPath = _cacheRoot + QStringLiteral("/viewer/.tmpv_") + QString::number(quintptr(QThread::currentThreadId())) + QStringLiteral(".jpg");
        if (fetchVideoFrame(item->videoPath, maxEdge, tempPath))
        {
            QImage image(tempPath);
            QFile::remove(tempPath);
            if (!image.isNull())
            {
                return image;
            }
        }
    }
    return {};
}

bool ThumbnailService::fetchHeicFrame(const QString& input, int edge, const QString& output) const
{
    const QString filter =
        QStringLiteral("[0:g:0]scale=%1:%1:force_original_aspect_ratio=decrease[out]").arg(edge);
    const QStringList args{
        QStringLiteral("-hide_banner"),
        QStringLiteral("-v"), QStringLiteral("error"),
        QStringLiteral("-y"),
        QStringLiteral("-i"), QDir::toNativeSeparators(input),
        QStringLiteral("-frames:v"), QStringLiteral("1"),
        QStringLiteral("-filter_complex"), filter,
        QStringLiteral("-map"), QStringLiteral("[out]"),
        QStringLiteral("-q:v"), QStringLiteral("3"),
        QDir::toNativeSeparators(output),
    };
    if (runFfmpeg(args))
    {
        return true;
    }

    const QString fallbackPath = output + QStringLiteral(".full.jpg");
    const QStringList fallbackArgs{
        QStringLiteral("-hide_banner"),
        QStringLiteral("-v"), QStringLiteral("error"),
        QStringLiteral("-y"),
        QStringLiteral("-i"), QDir::toNativeSeparators(input),
        QStringLiteral("-frames:v"), QStringLiteral("1"),
        QStringLiteral("-q:v"), QStringLiteral("2"),
        QDir::toNativeSeparators(fallbackPath),
    };
    if (!runFfmpeg(fallbackArgs))
    {
        QFile::remove(fallbackPath);
        return false;
    }

    QImage image(fallbackPath);
    QFile::remove(fallbackPath);
    if (image.isNull())
    {
        return false;
    }
    if (image.width() > edge || image.height() > edge)
    {
        image = image.scaled(edge, edge, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    return image.save(output, "JPG", 90);
}

bool ThumbnailService::fetchVideoFrame(const QString& input, int edge, const QString& output) const
{
    const QString filter =
        QStringLiteral("scale=%1:%1:force_original_aspect_ratio=decrease").arg(edge);
    const QStringList args{
        QStringLiteral("-hide_banner"),
        QStringLiteral("-v"), QStringLiteral("error"),
        QStringLiteral("-y"),
        QStringLiteral("-ss"), QStringLiteral("0.2"),
        QStringLiteral("-i"), QDir::toNativeSeparators(input),
        QStringLiteral("-frames:v"), QStringLiteral("1"),
        QStringLiteral("-vf"), filter,
        QStringLiteral("-q:v"), QStringLiteral("3"),
        QDir::toNativeSeparators(output),
    };
    return runFfmpeg(args);
}

bool ThumbnailService::runFfmpeg(const QStringList& arguments, int timeoutMs) const
{
    const QString ffmpeg = MediaUtils::ffmpegPath();
    if (ffmpeg.isEmpty())
    {
        return false;
    }

    QProcess process;
    process.setProgram(ffmpeg);
    process.setArguments(arguments);
    process.setProcessChannelMode(QProcess::MergedChannels);
#ifdef Q_OS_WIN
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) {
        args->flags |= CREATE_NO_WINDOW;
    });
#endif
    process.start();
    if (!process.waitForStarted(5000))
    {
        return false;
    }
    if (!process.waitForFinished(timeoutMs))
    {
        process.kill();
        process.waitForFinished(1000);
        return false;
    }
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

void ThumbnailService::storeImage(const QImage& image, const QString& output) const
{
    if (image.isNull())
    {
        return;
    }
    const QFileInfo info(output);
    QDir().mkpath(info.absolutePath());
    const QString temp = output + QStringLiteral(".tmp");
    if (image.save(temp, "JPG", 90))
    {
        atomicRename(temp, output);
    }
    else
    {
        QFile::remove(temp);
    }
}
