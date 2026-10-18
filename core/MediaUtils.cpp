#include "MediaUtils.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QProcess>
#include <QStandardPaths>
#include <QStringList>
#include <QtEndian>
#include <QtMath>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace
{
QString resolveFfmpeg();

struct BoxHeader
{
    qint64 size{-1};
    qint64 headerSize{0};
    QByteArray type;
};

BoxHeader readBoxHeader(QFile& file, qint64 offset, qint64 fileSize)
{
    BoxHeader header;
    if (offset < 0 || offset + 8 > fileSize || !file.seek(offset))
    {
        return header;
    }
    const QByteArray head = file.read(8);
    if (head.size() != 8)
    {
        return header;
    }
    const quint32 size32 = qFromBigEndian<quint32>(head.constData());
    header.type = head.mid(4, 4);
    header.headerSize = 8;
    if (size32 == 1)
    {
        const QByteArray extended = file.read(8);
        if (extended.size() != 8)
        {
            return header;
        }
        header.size = static_cast<qint64>(qFromBigEndian<quint64>(extended.constData()));
        header.headerSize = 16;
    }
    else if (size32 == 0)
    {
        header.size = fileSize - offset;
    }
    else
    {
        header.size = size32;
    }
    if (header.size < header.headerSize)
    {
        header.size = -1;
    }
    return header;
}

qint64 durationFromMvhd(QFile& file, qint64 offset, const BoxHeader& header)
{
    if (!file.seek(offset + header.headerSize))
    {
        return -1;
    }
    const QByteArray versionFlags = file.read(4);
    if (versionFlags.size() != 4)
    {
        return -1;
    }
    const int version = static_cast<quint8>(versionFlags.at(0));
    QByteArray payload;
    qint64 timescaleOffset = 0;
    qint64 durationOffset = 0;
    if (version == 1)
    {
        payload = file.read(28);
        timescaleOffset = 16;
        durationOffset = 20;
    }
    else
    {
        payload = file.read(16);
        timescaleOffset = 8;
        durationOffset = 12;
    }
    const int needed = static_cast<int>(durationOffset + (version == 1 ? 8 : 4));
    if (payload.size() < needed)
    {
        return -1;
    }
    const quint32 timescale =
        qFromBigEndian<quint32>(payload.constData() + timescaleOffset);
    const quint64 duration =
        version == 1 ? qFromBigEndian<quint64>(payload.constData() + durationOffset)
                     : qFromBigEndian<quint32>(payload.constData() + durationOffset);
    if (timescale == 0 || duration == 0)
    {
        return -1;
    }
    return static_cast<qint64>((static_cast<double>(duration) * 1000.0) / timescale);
}

qint64 parseMovDuration(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        return -1;
    }
    const qint64 fileSize = file.size();
    qint64 offset = 0;
    for (int guard = 0; guard < 512 && offset + 8 <= fileSize; ++guard)
    {
        const BoxHeader header = readBoxHeader(file, offset, fileSize);
        if (header.size < 8 || offset + header.size > fileSize)
        {
            return -1;
        }
        if (header.type == "moov")
        {
            const qint64 end = offset + header.size;
            qint64 childOffset = offset + header.headerSize;
            for (int childGuard = 0; childGuard < 256 && childOffset + 8 <= end; ++childGuard)
            {
                const BoxHeader child = readBoxHeader(file, childOffset, fileSize);
                if (child.size < 8 || childOffset + child.size > end)
                {
                    return -1;
                }
                if (child.type == "mvhd")
                {
                    return durationFromMvhd(file, childOffset, child);
                }
                childOffset += child.size;
            }
            return -1;
        }
        offset += header.size;
    }
    return -1;
}

int rotationFromTkhd(QFile& file, qint64 offset, const BoxHeader& header)
{
    if (!file.seek(offset + header.headerSize))
    {
        return 0;
    }
    const QByteArray versionFlags = file.read(4);
    if (versionFlags.size() != 4)
    {
        return 0;
    }
    const int version = static_cast<quint8>(versionFlags.at(0));
    const qint64 matrixOffset = version == 1 ? 52 : 40;
    if (!file.seek(offset + header.headerSize + matrixOffset))
    {
        return 0;
    }
    const QByteArray matrix = file.read(8);
    if (matrix.size() != 8)
    {
        return 0;
    }
    const qint32 a = qFromBigEndian<qint32>(matrix.constData());
    const qint32 b = qFromBigEndian<qint32>(matrix.constData() + 4);
    if (a == 0 && b == 0)
    {
        return 0;
    }
    const double degrees = qRadiansToDegrees(qAtan2(static_cast<double>(b),
                                                     static_cast<double>(a)));
    int snapped = qRound(degrees / 90.0) * 90;
    snapped %= 360;
    if (snapped < 0)
    {
        snapped += 360;
    }
    return snapped;
}

int parseMovRotation(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        return 0;
    }
    const qint64 fileSize = file.size();
    qint64 offset = 0;
    for (int guard = 0; guard < 512 && offset + 8 <= fileSize; ++guard)
    {
        const BoxHeader header = readBoxHeader(file, offset, fileSize);
        if (header.size < 8 || offset + header.size > fileSize)
        {
            return 0;
        }
        if (header.type == "moov")
        {
            const qint64 moovEnd = offset + header.size;
            qint64 childOffset = offset + header.headerSize;
            for (int childGuard = 0; childGuard < 256 && childOffset + 8 <= moovEnd;
                 ++childGuard)
            {
                const BoxHeader child = readBoxHeader(file, childOffset, fileSize);
                if (child.size < 8 || childOffset + child.size > moovEnd)
                {
                    return 0;
                }
                if (child.type == "trak")
                {
                    const qint64 trakEnd = childOffset + child.size;
                    qint64 trakChildOffset = childOffset + child.headerSize;
                    for (int trakGuard = 0; trakGuard < 128 && trakChildOffset + 8 <= trakEnd;
                         ++trakGuard)
                    {
                        const BoxHeader trakChild =
                            readBoxHeader(file, trakChildOffset, fileSize);
                        if (trakChild.size < 8 || trakChildOffset + trakChild.size > trakEnd)
                        {
                            break;
                        }
                        if (trakChild.type == "tkhd")
                        {
                            const int rotation =
                                rotationFromTkhd(file, trakChildOffset, trakChild);
                            if (rotation != 0)
                            {
                                return rotation;
                            }
                        }
                        trakChildOffset += trakChild.size;
                    }
                }
                childOffset += child.size;
            }
            return 0;
        }
        offset += header.size;
    }
    return 0;
}

QString resolveFfprobe()
{
    const QByteArray envPath = qgetenv("IPHONE_GALLERY_FFPROBE");
    if (!envPath.isEmpty())
    {
        const QString path = QString::fromLocal8Bit(envPath);
        if (QFileInfo::exists(path))
        {
            return QDir::toNativeSeparators(path);
        }
    }
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString local = appDir + QStringLiteral("/ffprobe.exe");
    if (QFileInfo::exists(local))
    {
        return QDir::toNativeSeparators(local);
    }
    const QString inPath = QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
    if (!inPath.isEmpty())
    {
        return QDir::toNativeSeparators(inPath);
    }
    const QString ffmpeg = resolveFfmpeg();
    if (!ffmpeg.isEmpty())
    {
        const QString beside = QFileInfo(ffmpeg).absolutePath() + QStringLiteral("/ffprobe.exe");
        if (QFileInfo::exists(beside))
        {
            return QDir::toNativeSeparators(beside);
        }
    }
    return {};
}

qint64 ffprobeDurationMs(const QString& path)
{
    const QString ffprobe = resolveFfprobe();
    if (ffprobe.isEmpty())
    {
        return -1;
    }
    QProcess process;
    process.setProgram(ffprobe);
    process.setArguments({QStringLiteral("-v"), QStringLiteral("error"),
                          QStringLiteral("-show_entries"), QStringLiteral("format=duration"),
                          QStringLiteral("-of"), QStringLiteral("default=nw=1:nk=1"),
                          QDir::toNativeSeparators(path)});
#ifdef Q_OS_WIN
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) {
        args->flags |= CREATE_NO_WINDOW;
    });
#endif
    process.start();
    if (!process.waitForStarted(3000))
    {
        return -1;
    }
    if (!process.waitForFinished(8000))
    {
        process.kill();
        process.waitForFinished(1000);
        return -1;
    }
    const QString output = QString::fromLatin1(process.readAllStandardOutput()).trimmed();
    bool ok = false;
    const double seconds = output.toDouble(&ok);
    if (!ok || seconds <= 0.0)
    {
        return -1;
    }
    return static_cast<qint64>(seconds * 1000.0);
}

QString resolveFfmpeg()
{
    const QByteArray envPath = qgetenv("IPHONE_GALLERY_FFMPEG");
    if (!envPath.isEmpty())
    {
        const QString path = QString::fromLocal8Bit(envPath);
        if (QFileInfo::exists(path))
        {
            return QDir::toNativeSeparators(path);
        }
    }

    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList localCandidates{
        appDir + QStringLiteral("/ffmpeg.exe"),
        appDir + QStringLiteral("/tools/ffmpeg.exe"),
        appDir + QStringLiteral("/ffmpeg/bin/ffmpeg.exe"),
    };
    for (const QString& candidate : localCandidates)
    {
        if (QFileInfo::exists(candidate))
        {
            return QDir::toNativeSeparators(candidate);
        }
    }

    const QString inPath = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (!inPath.isEmpty())
    {
        return QDir::toNativeSeparators(inPath);
    }

    const QStringList commonRoots{
        QStringLiteral("D:/tools"),
        QStringLiteral("C:/tools"),
        QStringLiteral("D:/ffmpeg"),
        QStringLiteral("C:/ffmpeg"),
    };
    for (const QString& root : commonRoots)
    {
        QDir dir(root);
        if (!dir.exists())
        {
            continue;
        }
        const QStringList subDirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString& subDir : subDirs)
        {
            const QString candidate = dir.filePath(subDir + QStringLiteral("/bin/ffmpeg.exe"));
            if (QFileInfo::exists(candidate))
            {
                return QDir::toNativeSeparators(candidate);
            }
        }
        const QString direct = dir.filePath(QStringLiteral("bin/ffmpeg.exe"));
        if (QFileInfo::exists(direct))
        {
            return QDir::toNativeSeparators(direct);
        }
    }

    return {};
}
} // namespace

QString MediaUtils::ffmpegPath()
{
    static const QString path = resolveFfmpeg();
    return path;
}

bool MediaUtils::isHeicFile(const QString& path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == QStringLiteral("heic") || suffix == QStringLiteral("heif");
}

bool MediaUtils::isImageFile(const QString& path)
{
    static const QStringList suffixes{
        QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("png"),
        QStringLiteral("heic"), QStringLiteral("heif"), QStringLiteral("gif"),
        QStringLiteral("bmp"), QStringLiteral("webp"), QStringLiteral("tif"),
        QStringLiteral("tiff"), QStringLiteral("dng"),
    };
    return suffixes.contains(QFileInfo(path).suffix().toLower());
}

bool MediaUtils::isVideoFile(const QString& path)
{
    static const QStringList suffixes{
        QStringLiteral("mov"), QStringLiteral("mp4"), QStringLiteral("m4v"),
        QStringLiteral("avi"), QStringLiteral("mkv"), QStringLiteral("3gp"),
    };
    return suffixes.contains(QFileInfo(path).suffix().toLower());
}

QString MediaUtils::formatBytes(qint64 bytes)
{
    static const QStringList units{QStringLiteral("B"), QStringLiteral("KB"),
                                   QStringLiteral("MB"), QStringLiteral("GB")};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < units.size() - 1)
    {
        value /= 1024.0;
        ++unit;
    }
    if (unit == 0)
    {
        return QStringLiteral("%1 %2").arg(static_cast<qint64>(value)).arg(units.at(unit));
    }
    return QStringLiteral("%1 %2").arg(value, 0, 'f', 1).arg(units.at(unit));
}

QString MediaUtils::formatDate(const QDateTime& dateTime)
{
    if (!dateTime.isValid())
    {
        return QStringLiteral("未知时间");
    }
    return dateTime.toString(QStringLiteral("yyyy年M月d日 HH:mm"));
}

QString MediaUtils::formatDuration(qint64 milliseconds)
{
    const qint64 totalSeconds = qMax<qint64>(0, (milliseconds + 500) / 1000);
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

qint64 MediaUtils::videoDurationMs(const QString& path)
{
    const qint64 parsed = parseMovDuration(path);
    if (parsed > 0)
    {
        return parsed;
    }
    return ffprobeDurationMs(path);
}

int MediaUtils::videoRotationDegrees(const QString& path)
{
    return parseMovRotation(path);
}

QImage MediaUtils::loadImageScaled(const QString& path, int maxEdge, bool* ok)
{
    if (ok)
    {
        *ok = false;
    }
    if (isHeicFile(path))
    {
        return {};
    }

    QImageReader reader(path);
    reader.setAutoTransform(true);
    QSize sourceSize = reader.size();
    if (sourceSize.isValid() && maxEdge > 0)
    {
        QSize target = sourceSize;
        if (target.width() > maxEdge || target.height() > maxEdge)
        {
            target.scale(maxEdge, maxEdge, Qt::KeepAspectRatio);
            reader.setScaledSize(target);
        }
    }

    QImage image = reader.read();
    if (image.isNull())
    {
        return {};
    }

    if (maxEdge > 0 && (image.width() > maxEdge || image.height() > maxEdge))
    {
        image = image.scaled(maxEdge, maxEdge, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    if (ok)
    {
        *ok = true;
    }
    return image;
}
