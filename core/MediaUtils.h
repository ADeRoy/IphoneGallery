#ifndef MEDIAUTILS_H
#define MEDIAUTILS_H

#include <QDateTime>
#include <QImage>
#include <QString>

namespace MediaUtils
{
QString ffmpegPath();
bool isHeicFile(const QString& path);
bool isImageFile(const QString& path);
bool isVideoFile(const QString& path);
QString formatBytes(qint64 bytes);
QString formatDate(const QDateTime& dateTime);
QString formatDuration(qint64 milliseconds);
qint64 videoDurationMs(const QString& path);
int videoRotationDegrees(const QString& path);
QImage loadImageScaled(const QString& path, int maxEdge, bool* ok = nullptr);
}

#endif // MEDIAUTILS_H
