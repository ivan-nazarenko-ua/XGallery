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

// The one spelling of a tag: '_' read as a space, whitespace collapsed, first letter upper case,
// the rest lower ("нове СВЯТО" -> "Нове свято", "_april" -> "April"). Every tag is normalized
// before it is stored or searched for. Changing the rule needs a migration step.
QString normalizeTag(const QString &tag);
// Manual tags must not contain ',' (separates tags in the search fields), '<' '>' (reserved
// for the search fields' "<без тегів>"), '/' (joins tags into the virtual path) or '\'.
bool isValidTagName(const QString &tag);
// Tags of a free-form list (clipboard): commas and line breaks separate them, the other forbidden
// characters become spaces. Normalized, without empties and repeats.
QStringList tagsFromText(const QString &text);
// Deletes the gallery's tags no image uses any more. Called after every change of tag links.
bool deleteUnusedTags(const QSqlDatabase &db, qint64 galleryId, QString *error);

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
    Classic,     // folder path inside the gallery, then best rating, then file name
    NewestFirst, // most recently added to the DB first, then as Classic
    LargestFirst // biggest files first, then as Classic
};

// What a tag search asks for. Tag names are normalized by the search itself.
struct TagQuery
{
    QStringList include;       // images having ALL of these; empty = any image
    QStringList exclude;       // ... and NONE of these
    bool onlyUntagged = false; // only images without any tag
    bool skipUntagged = false; // no images without tags
    bool exact = false;        // tag set is exactly `include`; exclude and skipUntagged are ignored
};

// Images of the gallery matching the query. limit < 0 means "no limit".
QList<ImageRecord> searchImages(qint64 galleryId, const TagQuery &query,
                                int offset, int limit, int *totalCount,
                                SortOrder order = SortOrder::Classic);

// Tags occurring in the images a search finds.
QStringList tagsOfSearch(qint64 galleryId, const TagQuery &query);

// Replaces the image's rating and tag set in one transaction. New tag names are created;
// tags of the gallery that end up unused are deleted.
bool saveImageMeta(qint64 galleryId, qint64 imageId, int rating, const QStringList &tags);

// The gallery's records with these ids (ids that no longer exist are skipped), sorted by path.
QList<ImageRecord> imagesByIds(qint64 galleryId, const QList<qint64> &imageIds);

// Renames a tag of the gallery. newName must be normalized and not taken (UNIQUE fails otherwise).
bool renameTag(qint64 galleryId, const QString &oldName, const QString &newName);

// Bulk edit: adds/removes the given tags on every image (an image that already has an added
// tag keeps it once). Unused tags are deleted afterwards. One transaction.
bool applyTagChanges(qint64 galleryId, const QList<qint64> &imageIds,
                     const QStringList &added, const QStringList &removed);

// The file at relativePath was rewritten by the program (e.g. rotated): refresh its record.
bool updateImageFile(qint64 galleryId, const QString &relativePath, qint64 size, qint64 mtime,
                     const QString &sha256);

// The file of this record got a new name on disk: relativePath is its new path, name the file name.
bool renameImage(qint64 galleryId, qint64 imageId, const QString &relativePath, const QString &name);

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
