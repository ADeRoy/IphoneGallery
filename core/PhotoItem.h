#ifndef PHOTOITEM_H
#define PHOTOITEM_H

#include <QDateTime>
#include <QFileInfo>
#include <QString>
#include <QVector>
#include <memory>

struct PhotoItem
{
    QString baseName;
    QString imagePath;
    QString videoPath;
    QString folderPath;
    QString monthKey;
    QDateTime captureTime;
    QDateTime fileTime;
    qint64 imageBytes{0};
    qint64 videoBytes{0};
    qint64 durationMs{0};
    int rotationDegrees{0};
    int width{0};
    int height{0};
    bool isHeic{false};
    bool isLivePhoto{false};

    bool hasImage() const { return !imagePath.isEmpty(); }
    bool hasVideo() const { return !videoPath.isEmpty(); }

    QDateTime dateTime() const
    {
        if (captureTime.isValid())
        {
            return captureTime;
        }
        if (fileTime.isValid())
        {
            return fileTime;
        }
        return QDateTime::fromSecsSinceEpoch(0);
    }

    QString displayName() const
    {
        return baseName.isEmpty() ? QFileInfo(imagePath).fileName() : baseName;
    }

    QString title() const
    {
        const QString fileName = hasImage() ? QFileInfo(imagePath).fileName()
                                            : QFileInfo(videoPath).fileName();
        return QFileInfo(fileName).completeBaseName();
    }
};

using PhotoItemPtr = std::shared_ptr<const PhotoItem>;
using PhotoItemList = QVector<PhotoItemPtr>;

Q_DECLARE_METATYPE(PhotoItemPtr)

#endif // PHOTOITEM_H
