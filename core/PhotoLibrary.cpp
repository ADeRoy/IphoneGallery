#include "PhotoLibrary.h"

#include "ExifReader.h"
#include "MediaUtils.h"

#include <QDate>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QTime>
#include <algorithm>

namespace
{
constexpr qint64 LivePhotoMaxDurationMs = 3000;

struct FileRecord
{
    QString path;
    QString folder;
    QString baseName;
    qint64 size{0};
    qint64 durationMs{0};
    int rotationDegrees{0};
    QDateTime modified;
};

QString pairingKey(const QString& folder, const QString& baseName)
{
    return folder.toLower() + QLatin1Char('|') + baseName.toLower();
}

bool isAuxiliaryFile(const QFileInfo& info)
{
    const QString name = info.fileName();
    if (name.startsWith(QLatin1Char('.')))
    {
        return true;
    }
    const QString suffix = info.suffix().toLower();
    return suffix == QStringLiteral("aae") || suffix == QStringLiteral("thm")
           || suffix == QStringLiteral("xmp");
}

QString normalizedMonth(const QDateTime& dateTime)
{
    if (!dateTime.isValid())
    {
        return {};
    }
    return dateTime.toString(QStringLiteral("yyyy-MM"));
}

QDateTime combineFolderMonthWithTime(const QString& monthKey, const QDateTime& time)
{
    if (monthKey.isEmpty())
    {
        return time;
    }
    const QStringList parts = monthKey.split(QLatin1Char('-'));
    if (parts.size() != 2)
    {
        return time;
    }
    const int year = parts.at(0).toInt();
    const int month = parts.at(1).toInt();
    if (year <= 0 || month <= 0 || month > 12)
    {
        return time;
    }
    QDate date(year, month, 1);
    QTime clock(12, 0);
    if (time.isValid())
    {
        clock = time.time();
        if (time.date().year() == year && time.date().month() == month)
        {
            date = time.date();
        }
        else
        {
            const int day = qMin(time.date().day(), date.daysInMonth());
            date = QDate(year, month, day);
        }
    }
    return QDateTime(date, clock);
}

bool itemDateLessThan(const PhotoItemPtr& left, const PhotoItemPtr& right)
{
    const QDateTime leftTime = left->dateTime();
    const QDateTime rightTime = right->dateTime();
    if (leftTime != rightTime)
    {
        return leftTime < rightTime;
    }
    return QString::localeAwareCompare(left->displayName(), right->displayName()) < 0;
}
} // namespace

QString PhotoLibrary::folderMonthKey(const QString& folderPath)
{
    const QString name = QFileInfo(folderPath).fileName();
    static const QRegularExpression pattern(QStringLiteral("^(\\d{4})(\\d{2})"));
    const QRegularExpressionMatch match = pattern.match(name);
    if (!match.hasMatch())
    {
        return {};
    }
    const int year = match.captured(1).toInt();
    const int month = match.captured(2).toInt();
    if (year < 1990 || year > 2100 || month < 1 || month > 12)
    {
        return {};
    }
    return QStringLiteral("%1-%2").arg(year).arg(month, 2, 10, QLatin1Char('0'));
}

QString PhotoLibrary::monthTitle(const QString& monthKey)
{
    const QStringList parts = monthKey.split(QLatin1Char('-'));
    if (parts.size() == 2)
    {
        const int year = parts.at(0).toInt();
        const int month = parts.at(1).toInt();
        if (year > 0 && month >= 1 && month <= 12)
        {
            static const QStringList monthNames{
                QStringLiteral("一月"), QStringLiteral("二月"), QStringLiteral("三月"),
                QStringLiteral("四月"), QStringLiteral("五月"), QStringLiteral("六月"),
                QStringLiteral("七月"), QStringLiteral("八月"), QStringLiteral("九月"),
                QStringLiteral("十月"), QStringLiteral("十一月"), QStringLiteral("十二月"),
            };
            return QStringLiteral("%1年%2").arg(year).arg(monthNames.at(month - 1));
        }
    }
    return QStringLiteral("未知时间");
}

PhotoItemList PhotoLibrary::scanDirectory(const QString& rootPath)
{
    PhotoItemList items;
    if (rootPath.isEmpty())
    {
        return items;
    }

    QHash<QString, int> liveCandidates;
    QVector<FileRecord> imageRecords;
    QVector<FileRecord> videos;
    QSet<int> consumedVideos;
    int durationKnown = 0;

    QDirIterator iterator(rootPath, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (iterator.hasNext())
    {
        iterator.next();
        const QFileInfo info = iterator.fileInfo();
        if (isAuxiliaryFile(info))
        {
            continue;
        }

        const QString path = info.absoluteFilePath();
        FileRecord record;
        record.path = path;
        record.folder = info.absolutePath();
        record.baseName = info.completeBaseName();
        record.size = info.size();
        record.modified = info.lastModified();

        if (MediaUtils::isVideoFile(path))
        {
            record.durationMs = MediaUtils::videoDurationMs(path);
            record.rotationDegrees = MediaUtils::videoRotationDegrees(path);
            if (record.durationMs > 0)
            {
                ++durationKnown;
            }
            const QString key = pairingKey(record.folder, record.baseName);
            const bool isMov = info.suffix().compare(QStringLiteral("mov"), Qt::CaseInsensitive) == 0;
            const int videoIndex = videos.size();
            videos.append(record);
            if (isMov && record.durationMs > 0
                && record.durationMs <= LivePhotoMaxDurationMs && !liveCandidates.contains(key))
            {
                liveCandidates.insert(key, videoIndex);
            }
        }
        else if (MediaUtils::isImageFile(path))
        {
            imageRecords.append(record);
        }
    }

    int exifCount = 0;
    int liveCount = 0;
    for (const FileRecord& image : imageRecords)
    {
        auto item = std::make_shared<PhotoItem>();
        item->baseName = image.baseName;
        item->imagePath = image.path;
        item->folderPath = image.folder;
        item->imageBytes = image.size;
        item->fileTime = image.modified;
        item->isHeic = MediaUtils::isHeicFile(image.path);
        item->captureTime = ExifReader::readCaptureTime(image.path);
        if (item->captureTime.isValid())
        {
            ++exifCount;
        }

        const QString key = pairingKey(image.folder, image.baseName);
        if (liveCandidates.contains(key))
        {
            const int videoIndex = liveCandidates.value(key);
            if (!consumedVideos.contains(videoIndex) && videoIndex >= 0
                && videoIndex < videos.size())
            {
                const FileRecord video = videos.at(videoIndex);
                item->videoPath = video.path;
                item->videoBytes = video.size;
                item->durationMs = video.durationMs;
                item->rotationDegrees = video.rotationDegrees;
                item->isLivePhoto = true;
                ++liveCount;
                consumedVideos.insert(videoIndex);
                if (!item->captureTime.isValid())
                {
                    item->captureTime = video.modified;
                }
            }
        }

        QString monthKey = normalizedMonth(item->captureTime);
        if (monthKey.isEmpty())
        {
            monthKey = folderMonthKey(image.folder);
            if (!monthKey.isEmpty() && item->fileTime.isValid())
            {
                item->fileTime = combineFolderMonthWithTime(monthKey, item->fileTime);
            }
        }
        if (monthKey.isEmpty())
        {
            monthKey = normalizedMonth(item->fileTime);
        }
        item->monthKey = monthKey;
        items.append(item);
    }

    for (int videoIndex = 0; videoIndex < videos.size(); ++videoIndex)
    {
        if (consumedVideos.contains(videoIndex))
        {
            continue;
        }
        const FileRecord& video = videos.at(videoIndex);

        auto item = std::make_shared<PhotoItem>();
        item->baseName = video.baseName;
        item->videoPath = video.path;
        item->folderPath = video.folder;
        item->videoBytes = video.size;
        item->durationMs = video.durationMs;
        item->rotationDegrees = video.rotationDegrees;
        item->fileTime = video.modified;

        QString monthKey = folderMonthKey(video.folder);
        if (!monthKey.isEmpty() && item->fileTime.isValid())
        {
            item->fileTime = combineFolderMonthWithTime(monthKey, item->fileTime);
        }
        if (monthKey.isEmpty())
        {
            monthKey = normalizedMonth(item->fileTime);
        }
        item->monthKey = monthKey;
        items.append(item);
    }

    qDebug() << "library scan items" << items.size() << "images" << imageRecords.size()
             << "videos" << videos.size() << "durationKnown" << durationKnown << "live"
             << liveCount << "exif" << exifCount;
    std::sort(items.begin(), items.end(), itemDateLessThan);
    return items;
}
