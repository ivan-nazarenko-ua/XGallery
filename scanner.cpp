#include "scanner.h"

#include "database.h"
#include "virtualpath.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
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
// The program's folder (exe, DLLs, portable DB) or anything inside it. Nothing there is a
// user's file, even when a gallery root contains it (portable mode on a USB stick).
// path must be absolute.
bool isInProgramFolder(const QString &path)
{
    static const QString programDir = QDir::cleanPath(QCoreApplication::applicationDirPath());
    const QString p = QDir::cleanPath(path);
    return p.compare(programDir, Qt::CaseInsensitive) == 0
           || p.startsWith(programDir + '/', Qt::CaseInsensitive);
}

// Records of such files already in the DB are dropped by the next rescan as "missing".
// The program's folder and its own DB (with the SQLite side files) are excluded too.
// A bare file name is checked by its suffix only.
bool isExcluded(const QString &path)
{
    static const QStringList suffixes = {"db", "ini"};
    if (suffixes.contains(QFileInfo(path).suffix(), Qt::CaseInsensitive))
        return true;
    if (QDir::isRelativePath(path))
        return false;
    if (isInProgramFolder(path))
        return true;
    static const QString dbPath = QFileInfo(Db::databasePath()).absoluteFilePath();
    const QString absolute = QFileInfo(path).absoluteFilePath();
    for (const char *side : {"", "-journal", "-wal", "-shm"})
        if (absolute.compare(dbPath + QLatin1String(side), Qt::CaseInsensitive) == 0)
            return true;
    return false;
}

// Windows device names can't be folder names, with an extension either ("CON", "nul.old").
bool isReservedName(const QString &name)
{
    static const QStringList reserved = {
        "CON", "PRN", "AUX", "NUL",
        "COM0", "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
        "COM¹", "COM²", "COM³",
        "LPT0", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9",
        "LPT¹", "LPT²", "LPT³"};
    return reserved.contains(name.section('.', 0, 0).trimmed().toUpper());
}

// Import creates a folder with a reserved name with '_' after the name: "CON" -> "CON_",
// "con.old" -> "con_.old". The tag stays without it (folderTagName).
QString safeFolderName(const QString &name)
{
    if (!isReservedName(name))
        return name;
    const qsizetype dot = name.indexOf('.');
    return dot < 0 ? name + '_' : name.left(dot) + '_' + name.mid(dot);
}

// The tag-giving name of a folder: undoes safeFolderName.
QString folderTagName(const QString &folder)
{
    const qsizetype dot = folder.indexOf('.');
    const QString stem = dot < 0 ? folder : folder.left(dot);
    if (stem.endsWith('_')) {
        const QString original = stem.chopped(1) + (dot < 0 ? QString() : folder.mid(dot));
        if (isReservedName(original))
            return original;
    }
    return folder;
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
        const QString tag = Db::normalizeTag(folderTagName(folder).replace(',', ' '));
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
    QList<qint64> retagged; // new and moved files: their tags (so their virtual path) changed

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
            retagged << imageId;
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
        const qint64 newId = insert.lastInsertId().toLongLong();
        if (!tagWriter.addTags(newId, tagsFromRelativePath(file.relative), &error))
            return rollback(error);
        retagged << newId;
        ++stats.added;
    }
    {
        QString error;
        if (!VirtualPath::update(db, retagged, &error))
            return rollback(error);
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

    {
        QString error;
        if (!Db::deleteUnusedTags(db, galleryId, &error))
            return rollback(error);
    }

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

QString fileSha256(const QString &path)
{
    return sha256Of(path);
}

bool isIgnoredFile(const QString &path)
{
    return isExcluded(path) || isJunkFile(path);
}

bool isValidFileName(const QString &name)
{
    static const QRegularExpression forbidden(QStringLiteral(R"([\\/:*?"<>|\x00-\x1f])"));
    return !name.isEmpty() && !name.contains(forbidden) && !name.endsWith('.') && !name.endsWith(' ')
           && !isReservedName(name);
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
        if (!item.keepDuplicate && knownContent.contains(key)) {
            ++stats.duplicates;
            continue;
        }

        // targetDir must stay inside the gallery: relative, no "..", no drive. Reserved
        // Windows names get a '_' (safeFolderName).
        bool inside = !QDir::isAbsolutePath(item.targetDir);
        QStringList segments;
        for (const QString &segment : QDir::cleanPath(item.targetDir).split('/', Qt::SkipEmptyParts)) {
            if (segment == ".")
                continue;
            if (segment == ".." || segment.contains(':') || segment.contains('\\')) {
                inside = false;
                break;
            }
            segments << safeFolderName(segment);
        }
        const QString targetDir = segments.join('/');
        if (!inside || !root.mkpath(targetDir.isEmpty() ? "." : targetDir)) {
            stats.failed << QDir::toNativeSeparators(item.source);
            continue;
        }
        const QDir dir(root.filePath(targetDir));

        // Same name already there: maybe it's this very file, just not scanned yet.
        const QString fileName = item.targetName.isEmpty() ? source.fileName() : item.targetName;
        const QString existing = dir.filePath(fileName);
        if (!item.keepDuplicate && QFileInfo(existing).size() == source.size() && QFileInfo::exists(existing)
            && sha256Of(existing) == hash) {
            knownContent.insert(key);
            ++stats.duplicates;
            continue;
        }

        const QString name = freeFileName(dir, fileName);
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
        const bool inserted = insert.exec();
        const qint64 newId = inserted ? insert.lastInsertId().toLongLong() : 0;
        if (!inserted || !tagWriter.addTags(newId, tagsFromRelativePath(relative), &error)
            || !VirtualPath::update(db, {newId}, &error)) {
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

std::optional<QString> folderInGallery(const QString &rootFolder, const QString &folder)
{
    const auto resolved = [](const QString &path) {
        const QFileInfo fi(path);
        const QString canonical = fi.canonicalFilePath();
        return canonical.isEmpty() ? fi.absoluteFilePath() : canonical;
    };
    const QString relative = QDir(resolved(rootFolder)).relativeFilePath(resolved(folder));
    if (relative == "." || relative.isEmpty())
        return QString();
    if (relative == ".." || relative.startsWith("../") || QDir::isAbsolutePath(relative))
        return std::nullopt; // above the root, or on another drive
    return relative;
}

bool exportCopy(const QString &source, const QString &destination)
{
    const QFileInfo target(destination);
    const QDir dir = target.absoluteDir();
    const QString temporary = dir.filePath(freeFileName(dir, target.fileName() + ".part"));
    if (!QFile::copy(source, temporary))
        return false;
    if ((QFileInfo::exists(destination) && !QFile::remove(destination))
        || !QFile::rename(temporary, destination)) {
        QFile::remove(temporary);
        return false;
    }
    return true;
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
        if (isInProgramFolder(fi.absoluteFilePath())) {
            notEmpty.insert(fi.absolutePath()); // the program's folder is never touched and keeps its parent
            continue;
        }
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

JunkRemoval removeJunk(const JunkScan &junk)
{
    JunkRemoval result;
    // Junk files first; failures are silently skipped (they are often locked by Explorer).
    QStringList keptJunk;
    for (const QString &file : junk.junkFiles) {
        QFile::setPermissions(file, QFile::permissions(file) | QFile::WriteOwner | QFile::WriteUser);
        if (QFile::remove(file))
            ++result.removedFiles;
        else
            keptJunk << file;
    }

    // Then folders, deepest first, so a parent is already empty when its turn comes.
    for (const QString &folder : junk.emptyFolders) {
        if (QDir().rmdir(folder)) {
            ++result.removedFolders;
            continue;
        }
        // A junk file we couldn't delete keeps its folders: that's expected, not an error.
        const bool explained = std::any_of(keptJunk.cbegin(), keptJunk.cend(), [&folder](const QString &f) {
            return f.startsWith(folder + '/', Qt::CaseInsensitive);
        });
        if (!explained)
            result.failedFolders << QDir::toNativeSeparators(folder);
    }
    return result;
}

} // namespace Scanner
