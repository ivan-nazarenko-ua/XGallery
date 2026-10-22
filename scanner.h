#ifndef SCANNER_H
#define SCANNER_H

#include <QPromise>
#include <QString>

struct ScanStats
{
    int added = 0;
    int updated = 0;
    int removed = 0;
    int unchanged = 0;
    int failed = 0;     // files that could not be read/hashed
    QString error;      // fatal error; empty on success
};

namespace Scanner {

// Synchronises the gallery's records in the database with the files under rootFolder.
// Meant to run in a worker thread (QtConcurrent::run); opens its own DB connection.
void rescan(QPromise<ScanStats> &promise, qint64 galleryId, const QString &rootFolder);

} // namespace Scanner

#endif // SCANNER_H
