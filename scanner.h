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

struct JunkScan
{
    QStringList junkFiles;    // Thumbs.db, desktop.ini
    QStringList emptyFolders; // empty once the junk files are gone; deepest first
};

bool isJunkFile(const QString &path);

// What "Видалити сміття" removes under rootFolder (never the root itself): junk files, and
// folders that contain nothing but junk files at any depth. Other hidden/system files count
// as content; symlinks/junctions are never entered and count as content. Worker-thread safe.
JunkScan findJunk(const QString &rootFolder);

} // namespace Scanner

#endif // SCANNER_H
