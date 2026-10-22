#ifndef DATABASE_H
#define DATABASE_H

#include <QList>
#include <QSqlDatabase>
#include <QString>
#include <QStringList>

struct Gallery
{
    qint64 id = 0;
    QString name;
    QString rootFolder;
    bool allowUpscale = false; // viewer may enlarge images beyond their original size
};

struct ImageRecord
{
    qint64 id = 0;
    QString relativePath;
    int rating = 50;
    qint64 size = 0; // bytes
};

namespace Db {

// Connection used by the GUI thread. Worker threads must open their own.
inline constexpr const char *MainConnection = "xgallery_main";

// Service "tag" for search only: matches images that have no tags at all.
// Never stored in the DB. '<' and '>' can't appear in Windows folder names, and the
// tag editor rejects them, so it can't collide with a real tag.
inline const QString NoTagsPseudoTag = QStringLiteral("<без тегів>");

// xgallery.sqlite next to the exe if it exists (portable mode), otherwise in AppData.
QString databasePath();
// The DB lies next to the exe. Gallery roots must then be on the program's drive and are
// stored relative to the exe folder (Gallery::rootFolder is still absolute in memory).
bool isPortable();
bool isOnProgramDrive(const QString &folder);
QString programDrive(); // e.g. "E:\"
// Portable mode: converts absolute roots on the program's drive (e.g. from a DB copied
// from AppData) to relative ones. No-op otherwise.
bool makeRootsPortable();

// Opens (or reuses) a named connection and makes sure the schema exists (migrating old DBs).
// Returns a closed database on failure; see lastError().
QSqlDatabase open(const QString &connectionName);
void close(const QString &connectionName);
QString lastError();

QString setting(const QString &key, const QString &defaultValue = {});
bool setSetting(const QString &key, const QString &value);

QList<Gallery> galleries();
// Returns the new gallery id, or 0 on failure.
qint64 createGallery(const Gallery &gallery);
bool updateGallery(const Gallery &gallery);
// Removes the gallery and everything that belongs to it (images, tags). Files are not touched.
// Takes an explicit connection so it can run in a worker thread.
bool deleteGallery(const QSqlDatabase &db, qint64 galleryId);

QStringList allTags(qint64 galleryId);
QStringList tagsForImage(qint64 imageId);

enum class SortOrder {
    Classic,     // best rating first, then file name
    NewestFirst, // most recently added to the DB first, then as Classic
    LargestFirst // biggest files first, then as Classic
};

// Images of the gallery having ALL of includeTags and NONE of excludeTags.
// Empty includeTags means "any image". limit < 0 means "no limit".
// NoTagsPseudoTag may be used in either list.
QList<ImageRecord> searchImages(qint64 galleryId,
                                const QStringList &includeTags,
                                const QStringList &excludeTags,
                                int offset, int limit, int *totalCount,
                                SortOrder order = SortOrder::Classic);

// Replaces the image's rating and tag set in one transaction. New tag names are created;
// tags of the gallery that end up unused are deleted.
bool saveImageMeta(qint64 galleryId, qint64 imageId, int rating, const QStringList &tags);

// The file at relativePath was rewritten by the program (e.g. rotated): refresh its record.
bool updateImageFile(qint64 galleryId, const QString &relativePath, qint64 size, qint64 mtime,
                     const QString &sha256);

struct DuplicateGroup
{
    QString sha256;
    qint64 size = 0;
    QList<ImageRecord> files; // at least two
};

// Files of the gallery that share a sha256, one group per hash.
QList<DuplicateGroup> findDuplicates(qint64 galleryId);

// Removes the records (not the files) and then tags nobody uses any more.
bool deleteImages(qint64 galleryId, const QList<qint64> &imageIds);

} // namespace Db

#endif // DATABASE_H
