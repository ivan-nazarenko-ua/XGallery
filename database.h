#ifndef DATABASE_H
#define DATABASE_H

#include <QHash>
#include <QList>
#include <QSqlDatabase>
#include <QString>
#include <QStringList>

#include "downloadprofile.h"

struct Gallery
{
    qint64 id = 0;
    QString name;
    QString rootFolder;
    bool allowUpscale = false; // viewer may enlarge images beyond their original size
};

// A group of tags, as the user named and ordered it in the gallery dialog.
struct TagGroup
{
    qint64 id = 0; // 0: not stored yet
    QString name;
};

// Tags of one group (groupId 0: tags without a group), for lists that show tags group by group.
struct TagSection
{
    qint64 groupId = 0;
    QString groupName;
    QStringList tags;
};

struct ImageRecord
{
    qint64 id = 0;
    QString relativePath;
    qint64 size = 0; // bytes
    QString sha256 = {}; // content; names the cached thumbnail
};

// A collection as the main window shows it (see collections.h).
struct CollectionRecord
{
    qint64 id = 0;
    QString name;        // as stored; may be empty (files without tags, or cleared by the user)
    QString virtualPath; // its files' tags joined by '/'
    int rating = 50;
    QString coverPath;   // relative path of the cover; the first file if the collection has no image
    int fileCount = 0;
    QString coverSha256; // content of coverPath
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

// Values remembered per gallery (table gallery_settings; deleted with the gallery).
QString gallerySetting(qint64 galleryId, const QString &key, const QString &defaultValue = {});
bool setGallerySetting(qint64 galleryId, const QString &key, const QString &value);

QList<Gallery> galleries();
// Returns the new gallery id, or 0 on failure.
qint64 createGallery(const Gallery &gallery);
bool updateGallery(const Gallery &gallery);
// Removes the gallery and everything that belongs to it (images, tags, cached thumbnails no other
// gallery needs). Files are not touched.
// Takes an explicit connection so it can run in a worker thread.
bool deleteGallery(const QSqlDatabase &db, qint64 galleryId);

QStringList allTags(qint64 galleryId);
// Tag name -> how many collections (distinct virtual paths) of the gallery have it. Unused tags are absent.
QHash<QString, int> tagCollectionCounts(qint64 galleryId);
QStringList tagsForImage(qint64 imageId);

// The gallery's tag groups in the user's order.
QList<TagGroup> tagGroups(qint64 galleryId);
// Makes the gallery's groups exactly `groups`, in this order: id 0 = new; stored groups that are
// missing are deleted and their tags left without a group. One transaction.
bool saveTagGroups(qint64 galleryId, const QList<TagGroup> &groups);
// Puts the tag into the group (0: no group).
bool setTagGroup(qint64 galleryId, const QString &tag, qint64 groupId);
// Splits tags (their order kept inside each part) by group: tags without a group (also names the
// DB doesn't know) first, then the groups in the user's order. Empty parts are left out.
QList<TagSection> tagSections(qint64 galleryId, const QStringList &tags);

enum class SortOrder {
    Classic,     // collection: its rating, shown name, virtual path; then file name, id
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
    qint64 collectionId = 0;   // non-zero: only the files of this collection
};

// Images of the gallery matching the query. limit < 0 means "no limit".
QList<ImageRecord> searchImages(qint64 galleryId, const TagQuery &query,
                                int offset, int limit, int *totalCount,
                                SortOrder order = SortOrder::Classic);

// The collections of the images a search finds, in the order their first file comes in the
// search result; paged by collection. *totalFiles = all files found.
QList<CollectionRecord> searchCollections(qint64 galleryId, const TagQuery &query, int offset, int limit,
                                          int *totalCollections, int *totalFiles,
                                          SortOrder order = SortOrder::Classic);

// The gallery's collection with this id; id 0 if there is none (any more).
CollectionRecord collection(qint64 galleryId, qint64 collectionId);

// The image is the cover of its collection.
bool isCollectionCover(qint64 galleryId, qint64 imageId);
// Makes the image the cover of the collection it is in now.
bool setCollectionCover(qint64 galleryId, qint64 imageId);

// Saves the user-editable properties of a collection.
bool updateCollection(qint64 galleryId, qint64 collectionId, const QString &name, int rating);

// Tags occurring in the images a search finds.
QStringList tagsOfSearch(qint64 galleryId, const TagQuery &query);

// Replaces the image's tag set in one transaction. New tag names are created; tags of the
// gallery that end up unused are deleted; collections follow (Collections::update).
bool saveImageMeta(qint64 galleryId, qint64 imageId, const QStringList &tags);

// The gallery's records with these ids (ids that no longer exist are skipped), sorted by path.
QList<ImageRecord> imagesByIds(qint64 galleryId, const QList<qint64> &imageIds);

// Renames a tag of the gallery. newName must be normalized and not taken (UNIQUE fails otherwise).
bool renameTag(qint64 galleryId, const QString &oldName, const QString &newName);

// Bulk edit: adds/removes the given tags on every image (an image that already has an added
// tag keeps it once). Unused tags are deleted afterwards. One transaction.
bool applyTagChanges(qint64 galleryId, const QList<qint64> &imageIds,
                     const QStringList &added, const QStringList &removed);

// Removes the tags from the whole gallery: every image loses them. One transaction.
bool deleteTags(qint64 galleryId, const QStringList &names);

// The file at relativePath was rewritten by the program (e.g. rotated): refresh its record.
bool updateImageFile(qint64 galleryId, const QString &relativePath, qint64 size, qint64 mtime,
                     const QString &sha256);

// The file of this record got a new name on disk: relativePath is its new path, name the file name.
bool renameImage(qint64 galleryId, qint64 imageId, const QString &relativePath, const QString &name);

// A duplicate and where it sits among the collections.
struct DuplicateFile : ImageRecord
{
    QString virtualPath;    // empty = no tags
    QString collectionName; // the stored name, may be empty
};

struct DuplicateGroup
{
    QString sha256;
    qint64 size = 0;
    QList<DuplicateFile> files; // at least two
};

// Files of the gallery that share a sha256, one group per hash.
QList<DuplicateGroup> findDuplicates(qint64 galleryId);

// Removes the records (not the files), then tags nobody uses any more and cached thumbnails
// no other record needs.
bool deleteImages(qint64 galleryId, const QList<qint64> &imageIds);

// Download manager profiles (program-wide), sorted by name (case ignored).
QList<DownloadProfile> downloadProfiles();
// Stores the profile under its name: replaces the one with that name, or adds it.
bool saveDownloadProfile(const DownloadProfile &profile);
bool deleteDownloadProfile(const QString &name);
// Saves all of them in one transaction (import); *replaced = how many names existed before.
bool saveDownloadProfiles(const QList<DownloadProfile> &profiles, int *replaced);

} // namespace Db

#endif // DATABASE_H
