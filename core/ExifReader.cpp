#include "ExifReader.h"

#include <QByteArray>
#include <QFile>

namespace
{
constexpr int MaxExifScanBytes = 128 * 1024;

bool hasBytes(const QByteArray& data, qint64 offset, qint64 length)
{
    return offset >= 0 && length > 0 && offset + length <= data.size();
}

quint16 read16(const QByteArray& data, qint64 offset, bool littleEndian)
{
    const auto* p = reinterpret_cast<const uchar*>(data.constData() + offset);
    if (littleEndian)
    {
        return static_cast<quint16>(p[0] | (p[1] << 8));
    }
    return static_cast<quint16>((p[0] << 8) | p[1]);
}

quint32 read32(const QByteArray& data, qint64 offset, bool littleEndian)
{
    const auto* p = reinterpret_cast<const uchar*>(data.constData() + offset);
    if (littleEndian)
    {
        return static_cast<quint32>(p[0]) | (static_cast<quint32>(p[1]) << 8)
               | (static_cast<quint32>(p[2]) << 16) | (static_cast<quint32>(p[3]) << 24);
    }
    return (static_cast<quint32>(p[0]) << 24) | (static_cast<quint32>(p[1]) << 16)
           | (static_cast<quint32>(p[2]) << 8) | static_cast<quint32>(p[3]);
}

QDateTime parseExifDateTime(const QString& raw)
{
    const QString value = raw.trimmed();
    if (value.isEmpty())
    {
        return {};
    }

    static const QStringList formats{
        QStringLiteral("yyyy:MM:dd HH:mm:ss"),
        QStringLiteral("yyyy-MM-dd HH:mm:ss"),
        QStringLiteral("yyyy:MM:dd HH:mm:ss.zzz"),
        QStringLiteral("yyyy-MM-dd'T'HH:mm:ss"),
    };
    for (const QString& format : formats)
    {
        const QDateTime dateTime = QDateTime::fromString(value, format);
        if (dateTime.isValid())
        {
            return dateTime;
        }
    }
    return {};
}

struct TiffContext
{
    const QByteArray* data{nullptr};
    qint64 tiffBase{0};
    bool littleEndian{true};
};

QString readAsciiValue(const TiffContext& ctx, qint64 entryOffset)
{
    const QByteArray& data = *ctx.data;
    if (!hasBytes(data, entryOffset, 12))
    {
        return {};
    }

    const quint32 count = read32(data, entryOffset + 4, ctx.littleEndian);
    if (count == 0 || count > 4096)
    {
        return {};
    }

    qint64 valueOffset = entryOffset + 8;
    if (count > 4)
    {
        valueOffset = ctx.tiffBase + read32(data, entryOffset + 8, ctx.littleEndian);
    }
    if (!hasBytes(data, valueOffset, count))
    {
        return {};
    }

    QByteArray value = data.mid(static_cast<int>(valueOffset), static_cast<int>(count));
    const int terminator = value.indexOf('\0');
    if (terminator >= 0)
    {
        value.truncate(terminator);
    }
    return QString::fromLatin1(value).trimmed();
}

QDateTime readDateFromIfd(const TiffContext& ctx, qint64 ifdOffset, quint32 targetTag)
{
    const QByteArray& data = *ctx.data;
    const qint64 absolute = ctx.tiffBase + ifdOffset;
    if (!hasBytes(data, absolute, 2))
    {
        return {};
    }

    const quint16 entryCount = read16(data, absolute, ctx.littleEndian);
    if (entryCount == 0 || entryCount > 512)
    {
        return {};
    }

    for (quint16 i = 0; i < entryCount; ++i)
    {
        const qint64 entry = absolute + 2 + static_cast<qint64>(i) * 12;
        if (!hasBytes(data, entry, 12))
        {
            break;
        }
        const quint16 tag = read16(data, entry, ctx.littleEndian);
        if (tag == targetTag)
        {
            return parseExifDateTime(readAsciiValue(ctx, entry));
        }
    }
    return {};
}

quint32 readExifIfdOffset(const TiffContext& ctx, qint64 ifdOffset)
{
    const QByteArray& data = *ctx.data;
    const qint64 absolute = ctx.tiffBase + ifdOffset;
    if (!hasBytes(data, absolute, 2))
    {
        return 0;
    }

    const quint16 entryCount = read16(data, absolute, ctx.littleEndian);
    if (entryCount == 0 || entryCount > 512)
    {
        return 0;
    }

    for (quint16 i = 0; i < entryCount; ++i)
    {
        const qint64 entry = absolute + 2 + static_cast<qint64>(i) * 12;
        if (!hasBytes(data, entry, 12))
        {
            break;
        }
        const quint16 tag = read16(data, entry, ctx.littleEndian);
        if (tag == 0x8769)
        {
            return read32(data, entry + 8, ctx.littleEndian);
        }
    }
    return 0;
}
} // namespace

QDateTime ExifReader::readCaptureTime(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
    {
        return {};
    }

    const QByteArray data = file.read(MaxExifScanBytes);
    int searchPosition = 0;
    while (true)
    {
        const int exifMarker = data.indexOf("Exif\0\0", searchPosition);
        if (exifMarker < 0)
        {
            return {};
        }
        searchPosition = exifMarker + 6;

        const qint64 tiffBase = exifMarker + 6;
        if (!hasBytes(data, tiffBase, 8))
        {
            continue;
        }
        const QByteArray byteOrder = data.mid(static_cast<int>(tiffBase), 2);
        if (byteOrder != "II" && byteOrder != "MM")
        {
            continue;
        }

        const bool littleEndian = byteOrder == "II";
        const quint16 magic = read16(data, tiffBase + 2, littleEndian);
        if (magic != 42)
        {
            continue;
        }

        TiffContext ctx{&data, tiffBase, littleEndian};
        const quint32 ifd0Offset = read32(data, tiffBase + 4, littleEndian);
        if (ifd0Offset == 0)
        {
            continue;
        }

        QDateTime result;
        const quint32 exifIfdOffset = readExifIfdOffset(ctx, ifd0Offset);
        if (exifIfdOffset != 0)
        {
            result = readDateFromIfd(ctx, exifIfdOffset, 0x9003);
            if (!result.isValid())
            {
                result = readDateFromIfd(ctx, exifIfdOffset, 0x9004);
            }
        }
        if (!result.isValid())
        {
            result = readDateFromIfd(ctx, ifd0Offset, 0x0132);
        }
        if (result.isValid())
        {
            return result;
        }
    }
}
