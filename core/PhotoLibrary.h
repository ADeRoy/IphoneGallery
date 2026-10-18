#ifndef PHOTOLIBRARY_H
#define PHOTOLIBRARY_H

#include "PhotoItem.h"

#include <QString>

namespace PhotoLibrary
{
PhotoItemList scanDirectory(const QString& rootPath);
QString monthTitle(const QString& monthKey);
QString folderMonthKey(const QString& folderPath);
}

#endif // PHOTOLIBRARY_H
