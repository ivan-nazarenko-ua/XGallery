#ifndef SCANNER_H
#define SCANNER_H

#include <QPromise>
#include <QString>

struct ScanStats
{
    int added = 0;
    int updated = 0;
    int removed = 0;
    int moved = 0;      // moved/renamed: record kept with its rating and manual tags
    int unchanged = 0;
    int failed = 0;     // files that could not be read/hashed
    QString error;      // fatal error; empty on success
};

namespace Scanner {

// Synchronises the gallery's records in the database with the files under rootFolder.
// Meant to run in a worker thread (QtConcurrent::run); opens its own DB connection.
void rescan(QPromise<ScanStats> &promise, qint64 galleryId, const QString &rootFolder);

// Hex sha256 of the file's content, empty if it can't be read.
QString fileSha256(const QString &path);

// Files the gallery never keeps: Thumbs.db, desktop.ini and anything else rescan skips
// (incl. everything in the program's folder; pass an absolute path for that check).
bool isIgnoredFile(const QString &path);

// --- Import -------------------------------------------------------------------------------

struct ImportItem
{
    QString source;    // absolute path of the file to copy
    QString targetDir; // folder inside the gallery, relative to its root, '/'-separated
};

struct ImportStats
{
    int copied = 0;
    int duplicates = 0;   // same content already in the gallery (or earlier in this batch)
    int ignored = 0;      // Thumbs.db, desktop.ini, ...
    QStringList failed;   // files that could not be read or copied
    QString error;        // fatal error; empty on success
};

// Copies the files into the gallery and registers them in the DB, with the target folders
// as tags. Duplicates (by sha256 + size) are skipped; a name already taken by a different
// file gets " (1)", " (2)", ... like Windows does. Worker-thread safe (own DB connection).
void importFiles(QPromise<ImportStats> &promise, qint64 galleryId, const QString &rootFolder,
                 const QList<ImportItem> &items);

struct JunkScan
{
    QStringList junkFiles;    // Thumbs.db, desktop.ini
    QStringList emptyFolders; // empty once the junk files are gone; deepest first
};

bool isJunkFile(const QString &path);

// What "Видалити сміття" removes under rootFolder (never the root itself): junk files, and
// folders that contain nothing but junk files at any depth. Other hidden/system files count
// as content; symlinks/junctions are never entered and count as content. The program's folder
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
