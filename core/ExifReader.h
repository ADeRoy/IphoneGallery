#ifndef EXIFREADER_H
#define EXIFREADER_H

#include <QDateTime>
#include <QString>

namespace ExifReader
{
QDateTime readCaptureTime(const QString& filePath);
}

#endif // EXIFREADER_H
