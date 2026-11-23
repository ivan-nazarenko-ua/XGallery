#include "thumbcache.h"

#include "database.h"
#include "mediafiles.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>
#include <QSqlQuery>
#include <QVariant>

namespace {

// A sha256 in hex: anything else never becomes a file name.
bool isHash(const QString &sha256)
{
    if (sha256.size() != 64)
        return false;
    for (QChar c : sha256)
        if (!c.isDigit() && !(c >= u'a' && c <= u'f'))
            return false;
    return true;
}

// thumbs/3f/3fa9….png: 256 subfolders keep each folder small (FAT32 allows ~9300 long names per folder).
QString fileOf(const QString &sha256)
{
    return QDir(ThumbCache::folder()).filePath(sha256.left(2) + '/' + sha256 + ".png");
}

} // namespace

namespace ThumbCache {

QString folder()
{
    static const QString path = QDir(QFileInfo(Db::databasePath()).absolutePath()).filePath("thumbs");
    return path;
}

void init()
{
    QDir dir;
    if (!dir.mkpath(folder()))
        return;
    // Thumbnails of the first version lay in the folder itself; they are made again in the subfolders.
    dir.setPath(folder());
    for (const QString &name : dir.entryList({"*.png"}, QDir::Files))
        dir.remove(name);
}

QImage load(const QString &sha256)
{
    if (!isHash(sha256))
        return {};
    const QString file = fileOf(sha256);
    return QFileInfo::exists(file) ? QImage(file, "PNG") : QImage();
}

void store(const QString &sha256, const QImage &thumbnail)
{
    if (!isHash(sha256) || thumbnail.isNull())
        return;
    // Written aside and renamed: a half-written file is never read, and two workers making the
    // thumbnail of the same content (duplicates on one page) don't clash.
    const QString path = fileOf(sha256);
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return;
    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly) && thumbnail.save(&file, "PNG"))
        file.commit();
}

QImage thumbnail(const QString &path, const QString &sha256, int size)
{
    // Other files are always a card, even if the same content is cached as a picture
    // (e.g. a photo renamed to another extension).
    if (MediaFiles::kindOf(path) == MediaFiles::Kind::Other)
        return MediaFiles::makeThumbnail(path, size);
    const QImage cached = load(sha256);
    if (!cached.isNull())
        return cached;
    bool isPreview = false;
    const QImage made = MediaFiles::makeThumbnail(path, size, &isPreview);
    if (isPreview) // not the card of a video Windows can't preview yet: it is tried again next time
        store(sha256, made);
    return made;
}

void removeUnused(const QSqlDatabase &db, const QStringList &sha256s)
{
    QSqlQuery q(db);
    q.prepare("SELECT 1 FROM images WHERE sha256 = ? LIMIT 1");
    for (const QString &sha256 : QSet<QString>(sha256s.cbegin(), sha256s.cend())) {
        if (!isHash(sha256) || !QFileInfo::exists(fileOf(sha256)))
            continue;
        q.addBindValue(sha256);
        if (!q.exec())
            continue; // not sure: keep it
        const bool used = q.next();
        q.finish();
        if (!used)
            QFile::remove(fileOf(sha256));
    }
}

} // namespace ThumbCache
