#include "scanner.h"

#include "database.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>
#include <QTimeZone>
#include <QVariant>

namespace {

struct KnownFile
{
    qint64 id;
    qint64 size;
    qint64 mtime;
    QString sha256;
};

// A file found on disk that has no record yet.
struct NewFile
{
    QString relative;
    QString name;
    qint64 size;
    qint64 mtime;
    QString sha256;
};

// sha256 + size: what identifies the same content at another path.
QString contentKey(const QString &sha256, qint64 size)
{
    return sha256 + ':' + QString::number(size);
}

QString sha256Of(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&f))
        return {};
    return QString::fromLatin1(hash.result().toHex());
}

// Service files that never belong in a gallery (Thumbs.db, desktop.ini, ...).
// Records of such files already in the DB are dropped by the next rescan as "missing".
bool isExcluded(const QString &path)
{
    static const QStringList suffixes = {"db", "ini"};
    return suffixes.contains(QFileInfo(path).suffix(), Qt::CaseInsensitive);
}

// Every folder between the root and the file becomes a tag.
// Commas separate tags in the search fields, so "name,1" becomes the tag "name 1".
QStringList tagsFromRelativePath(const QString &relativePath)
{
    QStringList parts = relativePath.split('/', Qt::SkipEmptyParts);
    if (!parts.isEmpty())
        parts.removeLast(); // the file name itself
    QStringList tags;
    for (const QString &folder : std::as_const(parts)) {
        const QString tag = QString(folder).replace(',', ' ').simplified();
        if (!tag.isEmpty() && !tags.contains(tag))
            tags << tag;
    }
    return tags;
}

class TagWriter
{
public:
    TagWriter(const QSqlDatabase &db, qint64 galleryId)
        : m_galleryId(galleryId), m_insertTag(db), m_selectTag(db), m_link(db)
    {
        m_insertTag.prepare("INSERT OR IGNORE INTO tags(gallery_id, name) VALUES(?, ?)");
        m_selectTag.prepare("SELECT id FROM tags WHERE gallery_id = ? AND name = ?");
        m_link.prepare("INSERT OR IGNORE INTO image_tags(image_id, tag_id) VALUES(?, ?)");
    }

    // Only adds links: tags the user removed/added by hand on an existing image stay as they are.
    bool addTags(qint64 imageId, const QStringList &tags, QString *error)
    {
        for (const QString &tag : tags) {
            const qint64 tagId = tagIdFor(tag, error);
            if (tagId < 0)
                return false;
            m_link.addBindValue(imageId);
            m_link.addBindValue(tagId);
            if (!m_link.exec())
                return fail(m_link, error);
        }
        return true;
    }

private:
    qint64 tagIdFor(const QString &tag, QString *error)
    {
        const auto it = m_cache.constFind(tag);
        if (it != m_cache.constEnd())
            return *it;
        m_insertTag.addBindValue(m_galleryId);
        m_insertTag.addBindValue(tag);
        if (!m_insertTag.exec()) {
            fail(m_insertTag, error);
            return -1;
        }
        m_selectTag.addBindValue(m_galleryId);
        m_selectTag.addBindValue(tag);
        if (!m_selectTag.exec() || !m_selectTag.next()) {
            fail(m_selectTag, error);
            return -1;
        }
        const qint64 id = m_selectTag.value(0).toLongLong();
        m_selectTag.finish();
        m_cache.insert(tag, id);
        return id;
    }

    static bool fail(const QSqlQuery &q, QString *error)
    {
        *error = q.lastError().text();
        return false;
    }

    qint64 m_galleryId;
    QSqlQuery m_insertTag;
    QSqlQuery m_selectTag;
    QSqlQuery m_link;
    QHash<QString, qint64> m_cache;
};

ScanStats doRescan(QPromise<ScanStats> &promise, QSqlDatabase &db, qint64 galleryId,
                   const QString &rootFolder)
{
    ScanStats stats;
    const QDir root(rootFolder);
    if (!root.exists()) {
        stats.error = QCoreApplication::translate("Scanner", "Папка не існує: %1").arg(rootFolder);
        return stats;
    }

    // 1. What we already know.
    QHash<QString, KnownFile> known;
    {
        QSqlQuery q(db);
        q.prepare("SELECT id, path, size, mtime, sha256 FROM images WHERE gallery_id = ?");
        q.addBindValue(galleryId);
        if (!q.exec()) {
            stats.error = q.lastError().text();
            return stats;
        }
        while (q.next())
            known.insert(q.value(1).toString(),
                         {q.value(0).toLongLong(), q.value(2).toLongLong(), q.value(3).toLongLong(),
                          q.value(4).toString()});
    }

    // 2. What is on disk.
    promise.setProgressRange(0, 0);
    promise.setProgressValueAndText(0, QCoreApplication::translate("Scanner", "Шукаю файли…"));
    QStringList files;
    // Every file, whatever its type. Hidden/system ones too: Windows sometimes marks
    // ordinary files that way (and hidden folders are then also walked into).
    QDirIterator it(rootFolder, QDir::Files | QDir::Readable | QDir::Hidden | QDir::System,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        if (isExcluded(path))
            continue;
        files << path;
        if (files.size() % 500 == 0)
            promise.setProgressValueAndText(
                0, QCoreApplication::translate("Scanner", "Шукаю файли… знайдено %1").arg(files.size()));
        if (promise.isCanceled())
            return stats;
    }

    // 3. Sync.
    if (!db.transaction()) {
        stats.error = db.lastError().text();
        return stats;
    }

    QSqlQuery insert(db);
    // One timestamp for the whole run, so "newest first" keeps a batch together.
    const qint64 addedAt = QDateTime::currentSecsSinceEpoch();
    insert.prepare("INSERT INTO images(gallery_id, path, name, size, mtime, sha256, added_at) "
                   "VALUES(?, ?, ?, ?, ?, ?, ?)");
    QSqlQuery update(db);
    update.prepare("UPDATE images SET size = ?, mtime = ?, sha256 = ? WHERE id = ?");
    TagWriter tagWriter(db, galleryId);

    auto rollback = [&](const QString &error) {
        stats.error = error;
        db.rollback();
        return stats;
    };

    promise.setProgressRange(0, int(files.size()));
    QList<NewFile> newFiles;
    QSet<QString> seen;
    seen.reserve(files.size());
    for (int i = 0; i < files.size(); ++i) {
        if (promise.isCanceled())
            return rollback(QCoreApplication::translate("Scanner", "Скасовано"));

        const QString &absolute = files.at(i);
        const QString relative = root.relativeFilePath(absolute);
        if (i % 20 == 0)
            promise.setProgressValueAndText(
                i, QCoreApplication::translate("Scanner", "Обробляю %1 з %2\n%3")
                       .arg(i + 1).arg(files.size()).arg(relative));

        const QFileInfo fi(absolute);
        const qint64 size = fi.size();
        const qint64 mtime = fi.lastModified(QTimeZone::UTC).toMSecsSinceEpoch();
        seen.insert(relative);

        const auto k = known.constFind(relative);
        if (k != known.constEnd() && k->size == size && k->mtime == mtime) {
            ++stats.unchanged;
            continue;
        }

        const QString hash = sha256Of(absolute);
        if (hash.isEmpty()) {
            ++stats.failed;
            continue;
        }

        if (k != known.constEnd()) {
            // Same path, new content: keeps its folder tags (same path) and manual edits.
            update.addBindValue(size);
            update.addBindValue(mtime);
            update.addBindValue(hash);
            update.addBindValue(k->id);
            if (!update.exec())
                return rollback(update.lastError().text());
            ++stats.updated;
        } else {
            newFiles.append({relative, fi.fileName(), size, mtime, hash}); // decided below
        }
    }
    promise.setProgressValueAndText(int(files.size()),
                                    QCoreApplication::translate("Scanner", "Прибираю видалені файли…"));

    // 4. Moved/renamed files: a record whose file disappeared plus a new file with the same
    //    content (sha256 + size). Only unambiguous 1:1 pairs count; copies stay new files.
    //    The record keeps its id, rating and manual tags; folder tags follow the new path.
    QHash<QString, QStringList> missingByContent; // content key -> old relative paths
    for (auto k = known.constBegin(); k != known.constEnd(); ++k)
        if (!seen.contains(k.key()))
            missingByContent[contentKey(k->sha256, k->size)] << k.key();
    QHash<QString, int> newByContent;
    for (const NewFile &file : std::as_const(newFiles))
        ++newByContent[contentKey(file.sha256, file.size)];

    QSqlQuery move(db);
    move.prepare("UPDATE images SET path = ?, name = ?, size = ?, mtime = ? WHERE id = ?");
    QSqlQuery dropFolderTag(db);
    dropFolderTag.prepare("DELETE FROM image_tags WHERE image_id = ? AND tag_id IN "
                          "(SELECT id FROM tags WHERE gallery_id = ? AND name = ?)");
    QSet<QString> movedFrom;

    for (const NewFile &file : std::as_const(newFiles)) {
        const QString key = contentKey(file.sha256, file.size);
        const QStringList oldPaths = missingByContent.value(key);
        QString error;

        if (oldPaths.size() == 1 && newByContent.value(key) == 1) {
            const QString &oldPath = oldPaths.first();
            const qint64 imageId = known.value(oldPath).id;
            move.addBindValue(file.relative);
            move.addBindValue(file.name);
            move.addBindValue(file.size);
            move.addBindValue(file.mtime);
            move.addBindValue(imageId);
            if (!move.exec())
                return rollback(move.lastError().text());

            // Old folder names that are not on the new path stop being tags of this file.
            const QStringList newFolders = tagsFromRelativePath(file.relative);
            for (const QString &folder : tagsFromRelativePath(oldPath)) {
                if (newFolders.contains(folder))
                    continue;
                dropFolderTag.addBindValue(imageId);
                dropFolderTag.addBindValue(galleryId);
                dropFolderTag.addBindValue(folder);
                if (!dropFolderTag.exec())
                    return rollback(dropFolderTag.lastError().text());
            }
            if (!tagWriter.addTags(imageId, newFolders, &error))
                return rollback(error);
            movedFrom.insert(oldPath);
            ++stats.moved;
            continue;
        }

        insert.addBindValue(galleryId);
        insert.addBindValue(file.relative);
        insert.addBindValue(file.name);
        insert.addBindValue(file.size);
        insert.addBindValue(file.mtime);
        insert.addBindValue(file.sha256);
        insert.addBindValue(addedAt);
        if (!insert.exec())
            return rollback(insert.lastError().text());
        // Folder tags only for new files: changed ones keep theirs (same path) and manual edits.
        if (!tagWriter.addTags(insert.lastInsertId().toLongLong(), tagsFromRelativePath(file.relative), &error))
            return rollback(error);
        ++stats.added;
    }

    // 5. Forget files that disappeared (image_tags go away via ON DELETE CASCADE).
    QSqlQuery remove(db);
    remove.prepare("DELETE FROM images WHERE id = ?");
    for (auto k = known.constBegin(); k != known.constEnd(); ++k) {
        if (seen.contains(k.key()) || movedFrom.contains(k.key()))
            continue;
        remove.addBindValue(k->id);
        if (!remove.exec())
            return rollback(remove.lastError().text());
        ++stats.removed;
    }

    QSqlQuery cleanup(db);
    cleanup.prepare("DELETE FROM tags WHERE gallery_id = ? "
                    "AND id NOT IN (SELECT DISTINCT tag_id FROM image_tags)");
    cleanup.addBindValue(galleryId);
    if (!cleanup.exec())
        return rollback(cleanup.lastError().text());

    if (!db.commit())
        return rollback(db.lastError().text());
    return stats;
}

} // namespace

namespace Scanner {

void rescan(QPromise<ScanStats> &promise, qint64 galleryId, const QString &rootFolder)
{
    const QString connection = QStringLiteral("xgallery_scan_%1")
                                   .arg(quintptr(QThread::currentThreadId()));
    ScanStats stats;
    {
        QSqlDatabase db = Db::open(connection);
        if (!db.isOpen())
            stats.error = Db::lastError();
        else
            stats = doRescan(promise, db, galleryId, rootFolder);
    }
    Db::close(connection);
    promise.addResult(stats);
}

bool isIgnoredFile(const QString &path)
{
    return isExcluded(path) || isJunkFile(path);
}

namespace {

// "name.ext" -> "name (1).ext", "name (2).ext", ... until the name is free.
QString freeFileName(const QDir &dir, const QString &fileName)
{
    if (!dir.exists(fileName))
        return fileName;
    const QFileInfo fi(fileName);
    const QString base = fi.completeBaseName();
    const QString suffix = fi.suffix().isEmpty() ? QString() : '.' + fi.suffix();
    for (int n = 1;; ++n) {
        const QString candidate = QString("%1 (%2)%3").arg(base).arg(n).arg(suffix);
        if (!dir.exists(candidate))
            return candidate;
    }
}

ImportStats doImport(QPromise<ImportStats> &promise, QSqlDatabase &db, qint64 galleryId,
                     const QString &rootFolder, const QList<ImportItem> &items)
{
    ImportStats stats;
    const QDir root(rootFolder);
    if (!root.exists()) {
        stats.error = QCoreApplication::translate("Scanner", "Папка галереї не існує: %1").arg(rootFolder);
        return stats;
    }

    // Content already in the gallery.
    QSet<QString> knownContent;
    {
        QSqlQuery q(db);
        q.prepare("SELECT sha256, size FROM images WHERE gallery_id = ?");
        q.addBindValue(galleryId);
        if (!q.exec()) {
            stats.error = q.lastError().text();
            return stats;
        }
        while (q.next())
            knownContent.insert(contentKey(q.value(0).toString(), q.value(1).toLongLong()));
    }

    if (!db.transaction()) {
        stats.error = db.lastError().text();
        return stats;
    }
    QSqlQuery insert(db);
    // One timestamp for the whole run, so "newest first" keeps a batch together.
    const qint64 addedAt = QDateTime::currentSecsSinceEpoch();
    insert.prepare("INSERT INTO images(gallery_id, path, name, size, mtime, sha256, added_at) "
                   "VALUES(?, ?, ?, ?, ?, ?, ?)");
    TagWriter tagWriter(db, galleryId);

    promise.setProgressRange(0, int(items.size()));
    for (int i = 0; i < items.size(); ++i) {
        if (promise.isCanceled())
            break;
        const ImportItem &item = items.at(i);
        const QFileInfo source(item.source);
        promise.setProgressValueAndText(
            i, QCoreApplication::translate("Scanner", "Копіюю %1 з %2\n%3").arg(i + 1).arg(items.size())
                   .arg(source.fileName()));

        if (Scanner::isIgnoredFile(item.source)) {
            ++stats.ignored;
            continue;
        }
        const QString hash = sha256Of(item.source);
        if (hash.isEmpty()) {
            stats.failed << QDir::toNativeSeparators(item.source);
            continue;
        }
        const QString key = contentKey(hash, source.size());
        if (knownContent.contains(key)) {
            ++stats.duplicates;
            continue;
        }

        const QString targetDir = QDir::cleanPath(item.targetDir);
        if (!root.mkpath(targetDir)) {
            stats.failed << QDir::toNativeSeparators(item.source);
            continue;
        }
        const QDir dir(root.filePath(targetDir));

        // Same name already there: maybe it's this very file, just not scanned yet.
        const QString existing = dir.filePath(source.fileName());
        if (QFileInfo(existing).size() == source.size() && QFileInfo::exists(existing)
            && sha256Of(existing) == hash) {
            knownContent.insert(key);
            ++stats.duplicates;
            continue;
        }

        const QString name = freeFileName(dir, source.fileName());
        const QString target = dir.filePath(name);
        if (!QFile::copy(item.source, target)) {
            stats.failed << QDir::toNativeSeparators(item.source);
            continue;
        }
        knownContent.insert(key); // the same content later in this batch is a duplicate

        const QFileInfo copied(target);
        const QString relative = root.relativeFilePath(target);
        insert.addBindValue(galleryId);
        insert.addBindValue(relative);
        insert.addBindValue(name);
        insert.addBindValue(copied.size());
        insert.addBindValue(copied.lastModified(QTimeZone::UTC).toMSecsSinceEpoch());
        insert.addBindValue(hash);
        insert.addBindValue(addedAt);
        QString error;
        if (!insert.exec()
            || !tagWriter.addTags(insert.lastInsertId().toLongLong(), tagsFromRelativePath(relative), &error)) {
            stats.error = error.isEmpty() ? insert.lastError().text() : error;
            db.rollback();
            stats.error = QCoreApplication::translate("Scanner", "Файли скопійовано, але записати їх у базу не вдалося "
                                                                "(зроби рескан):\n%1").arg(stats.error);
            return stats;
        }
        ++stats.copied;
    }

    if (!db.commit()) {
        stats.error = db.lastError().text();
        db.rollback();
    }
    return stats;
}

} // namespace

void importFiles(QPromise<ImportStats> &promise, qint64 galleryId, const QString &rootFolder,
                 const QList<ImportItem> &items)
{
    const QString connection = QStringLiteral("xgallery_import_%1")
                                   .arg(quintptr(QThread::currentThreadId()));
    ImportStats stats;
    {
        QSqlDatabase db = Db::open(connection);
        if (!db.isOpen())
            stats.error = Db::lastError();
        else
            stats = doImport(promise, db, galleryId, rootFolder, items);
    }
    Db::close(connection);
    promise.addResult(stats);
}

bool isJunkFile(const QString &path)
{
    static const QStringList names = {"thumbs.db", "desktop.ini"};
    return names.contains(QFileInfo(path).fileName(), Qt::CaseInsensitive);
}

JunkScan findJunk(const QString &rootFolder)
{
    const QString root = QDir::cleanPath(rootFolder);
    JunkScan result;
    QStringList dirs;
    QSet<QString> notEmpty; // folders known to hold something that stays

    QDirIterator it(root, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QFileInfo fi = it.fileInfo();
        if (fi.isDir() && !fi.isSymLink() && !fi.isJunction())
            dirs << fi.absoluteFilePath();
        else if (fi.isFile() && !fi.isSymLink() && isJunkFile(fi.fileName()))
            result.junkFiles << fi.absoluteFilePath(); // goes away, doesn't keep the folder
        else
            notEmpty.insert(fi.absolutePath()); // a real file (or link) keeps its folder alive
    }

    // Deepest first: a folder is empty if nothing marked it and none of its subfolders stays.
    std::sort(dirs.begin(), dirs.end(), [](const QString &a, const QString &b) {
        return a.count('/') > b.count('/');
    });
    for (const QString &dir : std::as_const(dirs)) {
        if (notEmpty.contains(dir))
            notEmpty.insert(QFileInfo(dir).absolutePath());
        else
            result.emptyFolders << dir;
    }
    return result;
}

} // namespace Scanner
