#ifndef THUMBCACHE_H
#define THUMBCACHE_H

#include <QImage>
#include <QSqlDatabase>
#include <QString>
#include <QStringList>

// Thumbnails of the main window's grid, cached as <first two hex digits>/<sha256>.png in the
// "thumbs" folder next to the database in use. Files with the same content share one thumbnail. Every failure (no folder, can't
// write, broken file) is ignored: the thumbnail is then simply made again. All functions are safe
// to call from worker threads.
namespace ThumbCache {

QString folder();

// Creates the folder (at program start) and removes thumbnails of the old flat layout from it.
// Failure is ignored.
void init();

// The cached thumbnail of this content, or a null image.
QImage load(const QString &sha256);

void store(const QString &sha256, const QImage &thumbnail);

// The cached thumbnail if there is one; otherwise MediaFiles::makeThumbnail, cached for images and
// videos (cards of other files cost nothing to draw).
QImage thumbnail(const QString &path, const QString &sha256, int size);

// Deletes the cached thumbnails of these hashes that no file record in `db` has any more.
// Call after the records are gone (after the commit).
void removeUnused(const QSqlDatabase &db, const QStringList &sha256s);

} // namespace ThumbCache

#endif // THUMBCACHE_H
