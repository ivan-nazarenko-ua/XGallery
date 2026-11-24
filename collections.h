#ifndef COLLECTIONS_H
#define COLLECTIONS_H

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QSqlDatabase>
#include <QString>

// A collection: the files of a gallery with the same tag set, i.e. the same virtual path
// ("Маша/Море/2024", see virtualpath.h). images.vpath_hash is the xxh128 of the virtual path,
// so files and their collection are joined on a fixed-length key. The collections table keeps
// what belongs to the collection itself: name (shown and sorted on, duplicates allowed),
// rating (1...100) and cover (an image file of the collection). name_key is the sort key of the
// name the user sees (nameKey()), kept in step whenever the name or, for an empty name, the path changes.
//
// Whenever the tag set of files changes, call retag() for them and then sync() for the gallery
// (update() does both); after deleting records, sync() alone. In sync():
// - a collection that lost all its files hands its properties (name, rating, cover) to the new
//   collection most of them went to, if that one has no properties yet;
// - files whose tag set has no collection yet get one with default properties;
// - collections without files are deleted; a cover that left the collection is chosen anew.
namespace Collections {

inline constexpr int DefaultRating = 50;
inline constexpr int MinRating = 1;
inline constexpr int MaxRating = 100;

// xxh128 of the virtual path's UTF-8, canonical (big-endian) form: 16 bytes. Changing it needs a
// migration step that recomputes images.vpath_hash and collections.vpath_hash.
QByteArray hashOf(const QString &virtualPath);

// The name a new collection gets: "<word> <number>" (owner's rule).
// - number: the smallest by value of the tags made of digits only, written as the tag is ("001");
//   "001" when there is none;
// - word: among the other tags, the alphabetically first one of the last group (by the groups'
//   order); none there: of the group before it, and so on; tags without a group come last;
//   "Collection" when there is none.
// groupRank: tag -> position of its group (groupRanks()); a tag missing from it has no group.
// "Маша/Море/2024", Маша in a group: "Маша 2024"; no tags: "Collection 001".
QString defaultName(const QString &virtualPath, const QHash<QString, int> &groupRank);
// Tag -> position of its group, for the gallery's tags that are in a group.
bool groupRanks(const QSqlDatabase &db, qint64 galleryId, QHash<QString, int> *ranks, QString *error);

// Sort key of the shown name: the name, or the virtual path if the name is empty (that is what the
// main window shows), in VirtualPath::sortKey form (alphabet-aware, case-insensitive). Never null.
QString nameKey(const QString &name, const QString &virtualPath);

struct Move
{
    QByteArray from; // the file's collection before (empty: the file had none yet)
    QByteArray to;
};

// Recomputes vpath, vpath_key and vpath_hash of the images from their current tags (on `db`,
// inside the caller's transaction if there is one). Appends a Move for every image whose
// collection changed.
bool retag(const QSqlDatabase &db, const QList<qint64> &imageIds, QList<Move> *moves, QString *error);

// Brings the gallery's collections in line with its files (see above).
bool sync(const QSqlDatabase &db, qint64 galleryId, const QList<Move> &moves, QString *error);

// retag() + sync().
bool update(const QSqlDatabase &db, qint64 galleryId, const QList<qint64> &imageIds, QString *error);

} // namespace Collections

#endif // COLLECTIONS_H
