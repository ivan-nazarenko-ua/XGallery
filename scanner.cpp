#include "scanner.h"

#include "collections.h"
#include "database.h"
#include "thumbcache.h"

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

#ifdef Q_OS_WIN
#include <windows.h>
#include <shobjidl.h>
#else
#include <sys/stat.h>
#endif

namespace {

// The folder on disk, the same whatever path (junction, symlink, mount) leads to it.
QString folderIdentity(const QString &path)
{
#ifdef Q_OS_WIN
    // No access rights needed for the file index; BACKUP_SEMANTICS opens directories.
    const HANDLE handle = CreateFileW(reinterpret_cast<const wchar_t *>(QDir::toNativeSeparators(path).utf16()), 0,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                      OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle != INVALID_HANDLE_VALUE) {
        BY_HANDLE_FILE_INFORMATION info;
        const bool ok = GetFileInformationByHandle(handle, &info);
        CloseHandle(handle);
        if (ok)
            return QStringLiteral("%1:%2:%3")
                .arg(quint32(info.dwVolumeSerialNumber))
                .arg(quint32(info.nFileIndexHigh))
                .arg(quint32(info.nFileIndexLow));
    }
#else
    struct stat st;
    if (::stat(QFile::encodeName(path).constData(), &st) == 0)
        return QStringLiteral("%1:%2").arg(quint64(st.st_dev)).arg(quint64(st.st_ino));
#endif
    // Not readable that way: the resolved path still catches links.
    const QString canonical = QFileInfo(path).canonicalFilePath();
    return canonical.isEmpty() ? QDir::cleanPath(path) : canonical;
}

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
// The program's folder, its own DB (with the SQLite side files) and the thumbnail cache next to
// the DB are excluded too (a gallery root may contain AppData). A bare file name is checked by
// its suffix only.
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
    static const QString thumbsDir = QDir::cleanPath(QFileInfo(ThumbCache::folder()).absoluteFilePath());
    const QString absolute = QFileInfo(path).absoluteFilePath();
    for (const char *side : {"", "-journal", "-wal", "-shm"})
        if (absolute.compare(dbPath + QLatin1String(side), Qt::CaseInsensitive) == 0)
            return true;
    return absolute.startsWith(thumbsDir + '/', Qt::CaseInsensitive);
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
                   const QString &rootFolder, bool followLinks)
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
    // Folder links only with the setting; never in circles (Scanner::forEachFile).
    const bool complete = Scanner::forEachFile(rootFolder, followLinks, [&](const QString &path) {
        if (isExcluded(path))
            return true;
        files << path;
        if (files.size() % 500 == 0)
            promise.setProgressValueAndText(
                0, QCoreApplication::translate("Scanner", "Шукаю файли… знайдено %1").arg(files.size()));
        return !promise.isCanceled();
    });
    if (!complete)
        return stats;

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
    QStringList goneContent; // content of removed records and of rewritten files: their cached thumbnails
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
            goneContent << k->sha256;
            ++stats.updated;
        } else {
            newFiles.append({relative, fi.fileName(), size, mtime, hash}); // decided below
        }
    }
    promise.setProgressValueAndText(int(files.size()),
                                    QCoreApplication::translate("Scanner", "Прибираю видалені файли…"));

    // 4. Moved/renamed files: a record whose file disappeared plus a new file with the same
    //    content (sha256 + size). Only unambiguous 1:1 pairs count; copies stay new files.
    //    The record keeps its id and manual tags; folder tags follow the new path.
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
    QList<qint64> retagged; // new and moved files: their tags (so their collection) changed

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
    QList<Collections::Move> moves;
    {
        QString error;
        if (!Collections::retag(db, retagged, &moves, &error))
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
        goneContent << k->sha256;
        ++stats.removed;
    }

    {
        QString error;
        // Collections after the removals: a moved folder hands its collection on.
        if (!Db::deleteUnusedTags(db, galleryId, &error) || !Collections::sync(db, galleryId, moves, &error))
            return rollback(error);
    }

    if (!db.commit())
        return rollback(db.lastError().text());
    ThumbCache::removeUnused(db, goneContent);
    return stats;
}

} // namespace

namespace Scanner {

void rescan(QPromise<ScanStats> &promise, qint64 galleryId, const QString &rootFolder, bool followLinks)
{
    const QString connection = QStringLiteral("xgallery_scan_%1")
                                   .arg(quintptr(QThread::currentThreadId()));
    ScanStats stats;
    {
        QSqlDatabase db = Db::open(connection);
        if (!db.isOpen())
            stats.error = Db::lastError();
        else
            stats = doRescan(promise, db, galleryId, rootFolder, followLinks);
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

bool isRealFolder(const QFileInfo &info)
{
    return info.isDir() && !info.isSymLink() && !info.isJunction();
}

QStringList realSubfolders(const QString &folder)
{
    QStringList folders;
    const QFileInfoList entries = QDir(folder).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden,
                                                             QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo &entry : entries)
        if (isRealFolder(entry))
            folders << entry.fileName();
    return folders;
}

bool isValidFolderName(const QString &name)
{
    static const QRegularExpression forbidden(QStringLiteral(R"([\\/:*?"<>|])"));
    return !name.isEmpty() && name != "." && name != ".." && !name.contains(forbidden) && !name.endsWith('.');
}

bool forEachFile(const QString &folder, bool followLinks,
                 const std::function<bool(const QString &path)> &onFile)
{
    // Folder by folder: QDirIterator's own recursion skips symlinks but walks into junctions,
    // and knows nothing of loops.
    QSet<QString> walked; // folderIdentity of every folder walked
    QStringList pending{folder};
    QStringList links; // folder links: walked once nothing else is left
    for (;;) {
        QString dir;
        if (!pending.isEmpty())
            dir = pending.takeLast();
        else if (!links.isEmpty())
            dir = links.takeFirst();
        else
            break;
        const QString identity = folderIdentity(dir);
        if (walked.contains(identity))
            continue; // already walked through another path, or a loop back up
        walked.insert(identity);

        QDirIterator it(dir, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable
                                 | QDir::Hidden | QDir::System);
        while (it.hasNext()) {
            it.next();
            const QFileInfo info = it.fileInfo();
            if (info.isDir()) {
                if (isRealFolder(info))
                    pending << info.absoluteFilePath();
                else if (followLinks)
                    links << info.absoluteFilePath();
            } else if (!onFile(info.filePath())) {
                return false;
            }
        }
    }
    return true;
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
        // One arg() call: chained ones would treat a "%1"/"%2B" in the file name as a placeholder.
        const QString candidate = QString("%1 (%2)%3").arg(base, QString::number(n), suffix);
        if (!dir.exists(candidate))
            return candidate;
    }
}

ImportStats doImport(QPromise<ImportStats> &promise, QSqlDatabase &db, qint64 galleryId,
                     const QString &rootFolder, const QList<ImportItem> &items, const std::atomic_bool *stop)
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
    QList<Collections::Move> moves;

    promise.setProgressRange(0, int(items.size()));
    for (int i = 0; i < items.size(); ++i) {
        if (promise.isCanceled())
            break;
        if (stop && *stop) {
            stats.stopped = true; // what was copied so far is committed below
            break;
        }
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
        QStringList tags = item.folderTags ? tagsFromRelativePath(relative) : QStringList();
        for (const QString &tag : item.tags) {
            const QString normalized = Db::normalizeTag(tag);
            if (!normalized.isEmpty() && !tags.contains(normalized))
                tags << normalized;
        }
        if (!inserted || !tagWriter.addTags(newId, tags, &error)
            || !Collections::retag(db, {newId}, &moves, &error)) {
            stats.error = error.isEmpty() ? insert.lastError().text() : error;
            db.rollback();
            stats.error = QCoreApplication::translate("Scanner", "Файли скопійовано, але записати їх у базу не вдалося "
                                                                "(зроби рескан):\n%1").arg(stats.error);
            return stats;
        }
        ++stats.copied;
    }

    QString error;
    if (!Collections::sync(db, galleryId, moves, &error)) {
        db.rollback();
        stats.error = QCoreApplication::translate("Scanner", "Файли скопійовано, але записати їх у базу не вдалося "
                                                            "(зроби рескан):\n%1").arg(error);
        return stats;
    }
    if (!db.commit()) {
        stats.error = db.lastError().text();
        db.rollback();
    }
    return stats;
}

} // namespace

void importFiles(QPromise<ImportStats> &promise, qint64 galleryId, const QString &rootFolder,
                 const QList<ImportItem> &items, const std::atomic_bool *stop)
{
    const QString connection = QStringLiteral("xgallery_import_%1")
                                   .arg(quintptr(QThread::currentThreadId()));
    ImportStats stats;
    {
        QSqlDatabase db = Db::open(connection);
        if (!db.isOpen())
            stats.error = Db::lastError();
        else
            stats = doImport(promise, db, galleryId, rootFolder, items, stop);
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

    // Folder by folder: QDirIterator's own recursion skips symlinks but walks into junctions,
    // i.e. into folders outside the gallery. A folder mounted inside itself (Linux bind mount) is
    // not a link: it is walked once.
    QSet<QString> walked;
    QStringList pending{root};
    while (!pending.isEmpty()) {
        const QString dir = pending.takeLast();
        const QString identity = folderIdentity(dir);
        if (walked.contains(identity)) {
            notEmpty.insert(dir); // never "empty": the mount point stays (and keeps its parent), like a link
            continue;
        }
        walked.insert(identity);
        QDirIterator it(dir, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
        while (it.hasNext()) {
            it.next();
            const QFileInfo fi = it.fileInfo();
            if (isInProgramFolder(fi.absoluteFilePath())) {
                notEmpty.insert(fi.absolutePath()); // the program's folder is never touched and keeps its parent
                continue;
            }
            if (isRealFolder(fi)) {
                dirs << fi.absoluteFilePath();
                pending << fi.absoluteFilePath();
            } else if (fi.isFile() && !fi.isSymLink() && isJunkFile(fi.fileName())) {
                result.junkFiles << fi.absoluteFilePath(); // goes away, doesn't keep the folder
            } else {
                notEmpty.insert(fi.absolutePath()); // a real file (or link) keeps its folder alive
            }
        }
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

#ifdef Q_OS_WIN
namespace {

// Watches one delete of an IFileOperation: aborts it when the shell would delete the file permanently
// instead of recycling it, and keeps the result.
class RecycleSink final : public IFileOperationProgressSink
{
public:
    bool notRecyclable = false;
    HRESULT result = S_OK;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **object) override
    {
        if (riid == IID_IUnknown || riid == IID_IFileOperationProgressSink) {
            *object = static_cast<IFileOperationProgressSink *>(this);
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }
    // Lives on the stack for one operation only.
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }

    HRESULT STDMETHODCALLTYPE PreDeleteItem(DWORD flags, IShellItem *) override
    {
        // Without this flag the item would be destroyed, not recycled.
        if (!(flags & TSF_DELETE_RECYCLE_IF_POSSIBLE)) {
            notRecyclable = true;
            return E_ABORT;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE PostDeleteItem(DWORD, IShellItem *, HRESULT hrDelete, IShellItem *) override
    {
        result = hrDelete;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE StartOperations() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE FinishOperations(HRESULT) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE PreRenameItem(DWORD, IShellItem *, LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE PostRenameItem(DWORD, IShellItem *, LPCWSTR, HRESULT, IShellItem *) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE PreMoveItem(DWORD, IShellItem *, IShellItem *, LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE PostMoveItem(DWORD, IShellItem *, IShellItem *, LPCWSTR, HRESULT, IShellItem *) override
    {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE PreCopyItem(DWORD, IShellItem *, IShellItem *, LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE PostCopyItem(DWORD, IShellItem *, IShellItem *, LPCWSTR, HRESULT, IShellItem *) override
    {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE PreNewItem(DWORD, IShellItem *, LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE PostNewItem(DWORD, IShellItem *, LPCWSTR, LPCWSTR, DWORD, HRESULT, IShellItem *) override
    {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE UpdateProgress(UINT, UINT) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE ResetTimer() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE PauseTimer() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE ResumeTimer() override { return S_OK; }
};

QString comErrorText(HRESULT hr)
{
    const QString text = qt_error_string(int(hr)).trimmed();
    return text.isEmpty() ? QStringLiteral("0x%1").arg(quint32(hr), 8, 16, QLatin1Char('0')) : text;
}

} // namespace
#endif

#ifdef Q_OS_WIN
namespace {

// recycleFile with COM ready on this thread.
RecycleOutcome recycleWithCom(const QString &path, QString *error)
{
    IFileOperation *operation = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&operation));
    if (FAILED(hr)) {
        *error = comErrorText(hr);
        return RecycleOutcome::Failed;
    }
    // No shell dialogs at all; FOF_NOCONFIRMATION alone would let it destroy what doesn't fit the bin,
    // which the sink prevents.
    operation->SetOperationFlags(FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI
                                 | FOFX_EARLYFAILURE | FOFX_RECYCLEONDELETE);
    IShellItem *item = nullptr;
    const QString native = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
    hr = SHCreateItemFromParsingName(reinterpret_cast<const wchar_t *>(native.utf16()), nullptr, IID_PPV_ARGS(&item));
    if (FAILED(hr)) {
        operation->Release();
        *error = comErrorText(hr);
        return RecycleOutcome::Failed;
    }
    RecycleSink sink;
    DWORD cookie = 0;
    const bool advised = SUCCEEDED(operation->Advise(&sink, &cookie));
    hr = operation->DeleteItem(item, nullptr);
    if (SUCCEEDED(hr))
        hr = operation->PerformOperations();
    BOOL aborted = FALSE;
    operation->GetAnyOperationsAborted(&aborted);
    if (advised)
        operation->Unadvise(cookie);
    item->Release();
    operation->Release();

    if (!advised) {
        // Without the sink there is no way to know it was recycled: don't claim it.
        *error = QCoreApplication::translate("Scanner", "Не вдалося стежити за операцією Кошика.");
        return QFileInfo::exists(path) ? RecycleOutcome::Failed : RecycleOutcome::Recycled;
    }
    if (sink.notRecyclable && QFileInfo::exists(path))
        return RecycleOutcome::NotRecyclable;
    if (FAILED(hr) || FAILED(sink.result) || aborted || QFileInfo::exists(path)) {
        *error = comErrorText(FAILED(sink.result) ? sink.result : FAILED(hr) ? hr : E_FAIL);
        return RecycleOutcome::Failed;
    }
    return RecycleOutcome::Recycled;
}

} // namespace
#endif

RecycleOutcome recycleFile(const QString &path, QString *error)
{
#ifdef Q_OS_WIN
    // Any thread may call it: COM is initialized here (already initialized: just counted, balanced below).
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const RecycleOutcome outcome = recycleWithCom(path, error);
    if (SUCCEEDED(init))
        CoUninitialize();
    return outcome;
#else
    if (QFile::moveToTrash(path))
        return RecycleOutcome::Recycled;
    *error = QCoreApplication::translate("Scanner", "Не вдалося перемістити в Кошик.");
    return RecycleOutcome::Failed;
#endif
}
void moveFilesToTrash(QPromise<TrashResult> &promise, const QStringList &paths, const std::atomic_bool *stop,
                      const TrashQuestion &ask)
{
#ifdef Q_OS_WIN
    // The Recycle Bin is a shell operation (COM) and this is a pool thread.
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
#endif
    TrashResult result;
    bool deleteAllPermanently = false;
    promise.setProgressRange(0, int(paths.size()));
    for (int i = 0; i < paths.size(); ++i) {
        if (promise.isCanceled() || (stop && *stop)) {
            result.stopped = true;
            break;
        }
        const QString &path = paths.at(i);
        promise.setProgressValueAndText(
            i, QCoreApplication::translate("Scanner", "Переміщую в Кошик %1 з %2\n%3")
                   .arg(i + 1).arg(paths.size()).arg(QFileInfo(path).fileName()));
        // A file already gone from disk only needs to leave the DB.
        if (!QFileInfo::exists(path)) {
            result.trashed << path;
            continue;
        }

        QString error;
        RecycleOutcome outcome = recycleFile(path, &error);
        if (outcome == RecycleOutcome::NotRecyclable) {
            const TrashChoice choice = deleteAllPermanently ? TrashChoice::DeleteAllPermanently
                                                            : ask(path, TrashProblem::NotRecyclable, QString());
            if (choice == TrashChoice::DeleteAllPermanently)
                deleteAllPermanently = true;
            if (choice == TrashChoice::DeletePermanently || choice == TrashChoice::DeleteAllPermanently) {
                QFile file(path);
                if (file.remove()) {
                    result.deleted << path;
                    continue;
                }
                outcome = RecycleOutcome::Failed;
                error = file.errorString();
            } else {
                result.failed << path;
                result.stopped = true;
                break;
            }
        }
        if (outcome == RecycleOutcome::Failed) {
            result.failed << path;
            if (ask(path, TrashProblem::Failed, error) != TrashChoice::Continue) {
                result.stopped = true;
                break;
            }
            continue;
        }
        result.trashed << path;
    }
    promise.setProgressValue(int(paths.size()));
#ifdef Q_OS_WIN
    if (SUCCEEDED(init))
        CoUninitialize();
#endif
    promise.addResult(result);
}
} // namespace Scanner
