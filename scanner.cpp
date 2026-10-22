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
};

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
QStringList tagsFromRelativePath(const QString &relativePath)
{
    QStringList parts = relativePath.split('/', Qt::SkipEmptyParts);
    if (!parts.isEmpty())
        parts.removeLast(); // the file name itself
    return parts;
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
        q.prepare("SELECT id, path, size, mtime FROM images WHERE gallery_id = ?");
        q.addBindValue(galleryId);
        if (!q.exec()) {
            stats.error = q.lastError().text();
            return stats;
        }
        while (q.next())
            known.insert(q.value(1).toString(),
                         {q.value(0).toLongLong(), q.value(2).toLongLong(), q.value(3).toLongLong()});
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
    insert.prepare("INSERT INTO images(gallery_id, path, name, size, mtime, sha256) VALUES(?, ?, ?, ?, ?, ?)");
    QSqlQuery update(db);
    update.prepare("UPDATE images SET size = ?, mtime = ?, sha256 = ? WHERE id = ?");
    TagWriter tagWriter(db, galleryId);

    auto rollback = [&](const QString &error) {
        stats.error = error;
        db.rollback();
        return stats;
    };

    promise.setProgressRange(0, int(files.size()));
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

        qint64 imageId;
        if (k != known.constEnd()) {
            imageId = k->id;
            update.addBindValue(size);
            update.addBindValue(mtime);
            update.addBindValue(hash);
            update.addBindValue(imageId);
            if (!update.exec())
                return rollback(update.lastError().text());
            ++stats.updated;
        } else {
            insert.addBindValue(galleryId);
            insert.addBindValue(relative);
            insert.addBindValue(fi.fileName());
            insert.addBindValue(size);
            insert.addBindValue(mtime);
            insert.addBindValue(hash);
            if (!insert.exec())
                return rollback(insert.lastError().text());
            imageId = insert.lastInsertId().toLongLong();
            ++stats.added;
        }

        // Folder tags only for new files: a changed file keeps its path, hence its folder tags.
        if (k != known.constEnd())
            continue;
        QString error;
        if (!tagWriter.addTags(imageId, tagsFromRelativePath(relative), &error))
            return rollback(error);
    }
    promise.setProgressValueAndText(int(files.size()),
                                    QCoreApplication::translate("Scanner", "Прибираю видалені файли…"));

    // 4. Forget files that disappeared (image_tags go away via ON DELETE CASCADE).
    QSqlQuery remove(db);
    remove.prepare("DELETE FROM images WHERE id = ?");
    for (auto k = known.constBegin(); k != known.constEnd(); ++k) {
        if (seen.contains(k.key()))
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

} // namespace Scanner
