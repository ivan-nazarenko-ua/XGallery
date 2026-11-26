#ifndef SCANNER_H
#define SCANNER_H

#include <QPromise>
#include <QSqlDatabase>
#include <QString>
#include <QStringList>

#include <atomic>
#include <functional>
#include <optional>

class QFileInfo;

struct ScanStats
{
    int added = 0;
    int updated = 0;
    int removed = 0;
    int moved = 0;      // moved/renamed: record kept with its manual tags
    int unchanged = 0;
    int failed = 0;     // files that could not be read/hashed
    QString error;      // fatal error; empty on success
};

namespace Scanner {

// Synchronises the gallery's records in the database with the files under rootFolder.
// followLinks: also walk into junctions and symlinked folders (forEachFile).
// Meant to run in a worker thread (QtConcurrent::run); opens its own DB connection.
void rescan(QPromise<ScanStats> &promise, qint64 galleryId, const QString &rootFolder, bool followLinks);

// Hex sha256 of the file's content, empty if it can't be read.
QString fileSha256(const QString &path);

// Files the gallery never keeps: Thumbs.db, desktop.ini and anything else rescan skips
// (incl. everything in the program's folder; pass an absolute path for that check).
bool isIgnoredFile(const QString &path);

// The name can be given to a file on Windows: no forbidden characters, no device name
// (CON, NUL.jpg, ...), no dot or space at the end (Windows would silently drop them).
bool isValidFileName(const QString &name);

// --- Import -------------------------------------------------------------------------------

struct ImportItem
{
    QString source;    // absolute path of the file to copy
    QString targetDir; // folder inside the gallery, relative to its root, '/'-separated
    QString targetName = {};     // file name in the gallery; empty = the source's name
    bool keepDuplicate = false;  // the user asked for a copy: same content is not skipped
    QStringList tags = {};       // given to the file besides (or instead of) the folder tags; normalized here
    bool folderTags = true;      // false: the folders of targetDir give no tags
};

struct ImportStats
{
    int copied = 0;
    int duplicates = 0;   // same content already in the gallery (or earlier in this batch)
    int ignored = 0;      // Thumbs.db, desktop.ini, ...
    QStringList failed;   // files that could not be read or copied
    QString error;        // fatal error; empty on success
    bool stopped = false; // the user stopped it: the files copied so far are in the gallery
};

// Copies the files into the gallery and registers them in the DB, with the target folders
// as tags. Duplicates (by sha256 + size) are skipped; a name already taken by a different
// file gets " (1)", " (2)", ... like Windows does. Worker-thread safe (own DB connection).
// stop (may be null): set from the GUI thread to stop after the current file; unlike cancelling the
// future, the stats still arrive.
void importFiles(QPromise<ImportStats> &promise, qint64 galleryId, const QString &rootFolder,
                 const QList<ImportItem> &items, const std::atomic_bool *stop = nullptr);

enum class RecycleOutcome
{
    Recycled,      // in the Recycle Bin
    NotRecyclable, // Windows would delete it permanently (too big for the bin, a drive without one): nothing done
    Failed,        // not moved (locked, no access, …): nothing done
};

// Moves one file to the Recycle Bin, never deleting it permanently instead (QFile::moveToTrash can't
// promise that: the shell silently "nukes" what doesn't fit). *error: the reason when Failed.
// Any thread (initializes COM for the call).
RecycleOutcome recycleFile(const QString &path, QString *error);

// What a trash job does with a file that can't go to the Recycle Bin.
enum class TrashProblem { NotRecyclable, Failed };
enum class TrashChoice
{
    DeletePermanently,    // NotRecyclable only: this file
    DeleteAllPermanently, // NotRecyclable only: this and every later such file, without asking again
    Continue,             // Failed only: leave the file, go on with the others
    Stop,                 // leave the file and stop
};
// Called on the worker thread; must block until the user has answered.
using TrashQuestion = std::function<TrashChoice(const QString &path, TrashProblem problem, const QString &error)>;

struct TrashResult
{
    QStringList trashed;  // absolute paths now in the Recycle Bin (or already gone from disk)
    QStringList deleted;  // deleted permanently, as the user agreed
    QStringList failed;   // left where they are
    bool stopped = false; // the user stopped it
};

// Moves the files to the Recycle Bin one by one, with progress (a worker job; the DB is not touched).
// A file already missing counts as trashed. A file that can't go there is never deleted silently:
// ask() decides. stop (may be null) stops after the current file.
void moveFilesToTrash(QPromise<TrashResult> &promise, const QStringList &paths, const std::atomic_bool *stop,
                      const TrashQuestion &ask);

// The existing folder's path relative to the gallery root, '/'-separated, empty for the root
// itself; nullopt if the folder is outside the gallery. Links are resolved first.
std::optional<QString> folderInGallery(const QString &rootFolder, const QString &folder);

// Copies a file out of the gallery. A file already at destination is replaced, but only after
// the copy has succeeded. Worker-thread safe.
bool exportCopy(const QString &source, const QString &destination);

// A real directory, not a symlink or an NTFS junction (those may lead outside the gallery).
bool isRealFolder(const QFileInfo &info);

// Names of the real folders right in `folder` (no links), sorted case-insensitively.
QStringList realSubfolders(const QString &folder);

// A name the user may give to a first-level folder of the gallery: not empty, not "." or "..",
// none of \ / : * ? " < > |, no '.' at the end. (Device names are fine: import adds '_'.)
bool isValidFolderName(const QString &name);

// Calls onFile for every readable file under folder, hidden and system ones too, folder by folder.
// Links to folders (junctions, symlinks) are entered only with followLinks, and only after every
// folder reachable without them, so a folder reachable both ways is listed under its own path.
// Each folder on disk is walked once whatever path leads to it (identified by volume + file index /
// st_dev + st_ino), so links and mounts pointing back up can't make the walk endless.
// onFile returns false to stop the walk; then forEachFile returns false too. Worker-thread safe.
bool forEachFile(const QString &folder, bool followLinks,
                 const std::function<bool(const QString &path)> &onFile);
// The same walk, with what the folder listing already knows about each file (size, ...).
bool forEachFileInfo(const QString &folder, bool followLinks,
                     const std::function<bool(const QFileInfo &info)> &onFile);

// --- Records for jobs that change gallery files themselves (FolderSync) -------------------
// All of them work in the caller's transaction on `db`; afterwards call Collections::retag for the
// added/moved records, then Db::deleteUnusedTags and Collections::sync.

// The tags the folders between the root and the file give (as rescan gives them).
QStringList folderTags(const QString &relativePath);
// Records a file now at relativePath (folder tags only). The new id, 0 on an error.
qint64 addRecord(const QSqlDatabase &db, qint64 galleryId, const QString &relativePath, qint64 size, qint64 mtime,
                 const QString &sha256, qint64 addedAt, QString *error);
// The record's file now lies at newPath: path and name follow, the old path's folder tags are swapped for the
// new path's ones (manual tags stay), like rescan does for a moved file.
bool moveRecord(const QSqlDatabase &db, qint64 galleryId, qint64 imageId, const QString &oldPath,
                const QString &newPath, qint64 mtime, QString *error);

struct JunkScan
{
    QStringList junkFiles;    // Thumbs.db, desktop.ini
    QStringList emptyFolders; // empty once the junk files are gone; deepest first
};

bool isJunkFile(const QString &path);

// What "Видалити сміття" removes under rootFolder (never the root itself): junk files, and
// folders that contain nothing but junk files at any depth. Other hidden/system files count
// as content; symlinks/junctions are never entered (whatever the setting: this deletes
// permanently) and count as content; a folder mounted twice is walked once. The program's folder
// is left alone and counts as content. Worker-thread safe.
JunkScan findJunk(const QString &rootFolder);

struct JunkRemoval
{
    int removedFiles = 0;
    int removedFolders = 0;
    QStringList failedFolders; // native separators; folders kept by a locked junk file are not listed
};

// Deletes what findJunk found, permanently. Junk files that can't be deleted are skipped
// silently (Explorer often holds them). Worker-thread safe.
JunkRemoval removeJunk(const JunkScan &junk);

} // namespace Scanner

#endif // SCANNER_H
