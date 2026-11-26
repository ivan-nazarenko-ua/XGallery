#ifndef FOLDERSYNC_H
#define FOLDERSYNC_H

#include "scanner.h"

#include <QList>
#include <QPromise>
#include <QString>
#include <QStringList>

#include <atomic>

// File menu "Синхронізувати з папкою": a folder is compared with the gallery as if it were the root of a
// second gallery (the same paths relative to the root, case ignored), then one side is made like the other.
// The gallery side is its DB records (the caller rescans first), the folder side is walked (links never
// followed: files there may get deleted). Files rescan ignores (*.db, *.ini, Thumbs.db, ...) are left alone.
namespace FolderSync {

enum class Mode {
    Quick,    // same path + same size = the same file, without reading it
    Reliable, // same file only if sha256 matches too
};
// Both modes read (sha256) only folder files whose size some not yet matched gallery file has: other sizes
// can't be the same content. Gallery files are never read: their sha256 comes from the DB.

// Shared with the GUI thread. stop: the job ends after the current step (a copy under way is undone);
// pause: the job waits (while the user is asked whether to stop).
struct Control
{
    std::atomic_bool stop = false;
    std::atomic_bool pause = false;
};

struct File
{
    QString path;       // relative to its root, '/'-separated, as spelled on disk
    qint64 size = 0;
    QString sha256;     // folder files: empty when not read
    qint64 imageId = 0; // gallery files: the record
};

struct Pair
{
    File gallery;
    File folder;
};

struct Comparison
{
    int synced = 0;             // same path, same file
    QList<Pair> moved;          // same content at another path
    QList<Pair> changed;        // same path, other content
    QList<File> onlyInGallery;
    QList<File> onlyInFolder;
    QStringList unreadable;     // folder files that could not be read (relative): neither they nor the gallery
                                // file at their path are touched
    bool stopped = false;
    QString error;

    bool differs() const;
};

// Worker job (own DB connection).
void compare(QPromise<Comparison> &promise, qint64 galleryId, const QString &folder, Mode mode, Control *control);

struct ApplyResult
{
    int deleted = 0;            // in the Recycle Bin (replaced old versions not counted)
    int deletedPermanently = 0; // the user agreed (no Recycle Bin there); old versions included
    int moved = 0;
    int copied = 0;             // into the folder
    int added = 0;              // into the gallery (recorded with folder tags)
    int replaced = 0;           // the old version deleted like the others
    int renamed = 0;            // merge: copied or added as "name (1).ext", the name was taken
    int removedFolders = 0;     // left empty by the job
    QStringList failed;         // "path — reason", native separators
    QList<qint64> removedImageIds; // gallery records dropped
    bool stopped = false;
    QString error;              // DB error (files may have changed: rescan)
};

// Makes the folder like the gallery: files only the folder has go to the Recycle Bin, moved ones are moved to
// the gallery's paths, missing ones are copied there, changed ones replaced. Folders the job emptied are removed
// when Windows lets it. Nothing is ever overwritten without the old file being deleted first like the others.
// ask decides about files that can't go to the Recycle Bin (blocks until the user answers).
void applyToFolder(QPromise<ApplyResult> &promise, const QString &rootFolder, const QString &folder,
                   const Comparison &comparison, Control *control, const Scanner::TrashQuestion &ask);

// The same the other way round, and the records follow: moved records keep their id and manual tags (folder
// tags follow the path), new files are recorded with their folder tags, changed ones keep their record.
// One transaction, committed also when stopped.
void applyToGallery(QPromise<ApplyResult> &promise, qint64 galleryId, const QString &rootFolder,
                    const QString &folder, const Comparison &comparison, Control *control,
                    const Scanner::TrashQuestion &ask);

// What the user chose after the comparison.
enum class Direction {
    FolderLikeGallery, // applyToFolder
    GalleryLikeFolder, // applyToGallery
    Merge,             // merge
};

// Each side gets the files only the other has (onlyInGallery / onlyInFolder, and the other side's version of
// a changed file), at the same relative path; a name already taken there gets " (1)", " (2)", ... like Windows.
// Nothing is deleted, moved or overwritten. Files new to the gallery are recorded with their folder tags
// (one transaction, committed also when stopped). First the folder, then the gallery.
void merge(QPromise<ApplyResult> &promise, qint64 galleryId, const QString &rootFolder, const QString &folder,
           const Comparison &comparison, Control *control);

} // namespace FolderSync

#endif // FOLDERSYNC_H
