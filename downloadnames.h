#ifndef DOWNLOADNAMES_H
#define DOWNLOADNAMES_H

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QUrl>

class QDir;

// Names the download manager gives to folders and files.
namespace DownloadNames {

// Suffix of the folder each download goes to ("006-collection").
inline constexpr char FolderSuffix[] = "-collection";

// The number of the next download folder: the largest number some folder name starts with, plus 1;
// below 1000 with leading zeros to three digits ("1-море", "004 - село - 2", "05 - літо" -> "006").
QString nextFolderNumber(const QStringList &folderNames);
// nextFolderNumber of the folders in `folder` (none, or it doesn't exist yet: "001") + FolderSuffix.
QString nextFolderName(const QString &folder);

// The file name a link gives: its last path segment (decoded, without the query), made valid on
// Windows. Empty if the link has no name with an extension ("…/82840051", "…/82840051/?ver=1").
QString nameFromUrl(const QUrl &url);

// "file_007.<ext>": number = position of the link (1-based), at least 3 digits (more if count needs them).
QString numberedName(int number, int count, const QString &extension);

// Extension for a Content-Type ("image/jpeg" -> "jpeg"); "bin" when it says nothing known.
QString extensionFor(const QByteArray &contentType);

// "page_N.html" (Content-Type text/html) or "page_N.bin", N from 1: the first name free in dir.
QString pageFileName(const QDir &dir, const QByteArray &contentType);

} // namespace DownloadNames

#endif // DOWNLOADNAMES_H
