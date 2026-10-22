#include "database.h"

#include "migrations.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QVariant>

namespace {

thread_local QString g_lastError;

bool exec(QSqlQuery &q, const QString &sql)
{
    if (!q.exec(sql)) {
        g_lastError = q.lastError().text() + "\n" + sql;
        return false;
    }
    return true;
}

bool exec(QSqlQuery &q)
{
    if (!q.exec()) {
        g_lastError = q.lastError().text();
        return false;
    }
    return true;
}

bool ensureSchema(QSqlDatabase &db)
{
    QString error;
    if (Migrations::run(db, &error))
        return true;
    g_lastError = error;
    return false;
}

QString placeholders(qsizetype n)
{
    QStringList list;
    for (qsizetype i = 0; i < n; ++i)
        list << "?";
    return list.join(',');
}

QStringList normalized(const QStringList &tags)
{
    QStringList out;
    for (const QString &t : tags) {
        const QString s = t.trimmed();
        if (!s.isEmpty() && !out.contains(s))
            out << s;
    }
    return out;
}

QSqlDatabase mainDb()
{
    return QSqlDatabase::database(Db::MainConnection);
}

} // namespace

namespace Db {

QString databasePath()
{
    // Decided once per run so every connection (incl. worker threads) uses the same file.
    static const QString path = [] {
        const QString fileName = QStringLiteral("xgallery.sqlite");

        // Portable mode: a DB lying next to the exe wins (e.g. the program runs from a USB stick).
        const QString portable = QDir(QCoreApplication::applicationDirPath()).filePath(fileName);
        if (QFileInfo(portable).isFile())
            return portable;

        const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir().mkpath(dir);
        return QDir(dir).filePath(fileName);
    }();
    return path;
}

bool isPortable()
{
    return QFileInfo(databasePath()).absolutePath() == QFileInfo(QCoreApplication::applicationDirPath()).absoluteFilePath();
}

bool isOnProgramDrive(const QString &folder)
{
    const QString programRoot = QStorageInfo(QCoreApplication::applicationDirPath()).rootPath();
    const QString folderRoot = QStorageInfo(folder).rootPath();
    return !folderRoot.isEmpty() && folderRoot.compare(programRoot, Qt::CaseInsensitive) == 0;
}

QString programDrive()
{
    return QDir::toNativeSeparators(QStorageInfo(QCoreApplication::applicationDirPath()).rootPath());
}

QSqlDatabase open(const QString &connectionName)
{
    if (QSqlDatabase::contains(connectionName)) {
        QSqlDatabase db = QSqlDatabase::database(connectionName);
        if (db.isOpen())
            return db;
    }

    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connectionName);
    db.setDatabaseName(databasePath());
    db.setConnectOptions("QSQLITE_BUSY_TIMEOUT=10000");
    if (!db.open()) {
        g_lastError = db.lastError().text();
        return db;
    }
    if (!ensureSchema(db))
        db.close();
    return db;
}

void close(const QString &connectionName)
{
    {
        QSqlDatabase db = QSqlDatabase::database(connectionName, false);
        if (db.isValid())
            db.close();
    }
    QSqlDatabase::removeDatabase(connectionName);
}

QString lastError()
{
    return g_lastError;
}

QString setting(const QString &key, const QString &defaultValue)
{
    QSqlQuery q(mainDb());
    q.prepare("SELECT value FROM settings WHERE key = ?");
    q.addBindValue(key);
    if (q.exec() && q.next())
        return q.value(0).toString();
    return defaultValue;
}

bool setSetting(const QString &key, const QString &value)
{
    QSqlQuery q(mainDb());
    q.prepare("INSERT INTO settings(key, value) VALUES(?, ?) "
              "ON CONFLICT(key) DO UPDATE SET value = excluded.value");
    q.addBindValue(key);
    q.addBindValue(value);
    return exec(q);
}

// In portable mode the root is stored relative to the exe folder, so the gallery survives the
// USB stick getting another drive letter. In memory Gallery::rootFolder is always absolute.
static QString storedRoot(const QString &absoluteRoot)
{
    if (!isPortable() || !isOnProgramDrive(absoluteRoot))
        return absoluteRoot;
    return QDir(QCoreApplication::applicationDirPath()).relativeFilePath(absoluteRoot);
}

static QString resolvedRoot(const QString &stored)
{
    if (stored.isEmpty() || !QDir::isRelativePath(stored))
        return stored;
    return QDir::cleanPath(QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(stored));
}

bool makeRootsPortable()
{
    if (!isPortable())
        return true;
    QList<QPair<qint64, QString>> toFix;
    QSqlQuery q(mainDb());
    if (!exec(q, "SELECT id, root FROM galleries"))
        return false;
    while (q.next()) {
        const QString root = q.value(1).toString();
        if (!QDir::isRelativePath(root) && isOnProgramDrive(root))
            toFix.append({q.value(0).toLongLong(), storedRoot(root)});
    }
    q.finish();
    q.prepare("UPDATE galleries SET root = ? WHERE id = ?");
    for (const auto &fix : std::as_const(toFix)) {
        q.addBindValue(fix.second);
        q.addBindValue(fix.first);
        if (!exec(q))
            return false;
    }
    return true;
}

QList<Gallery> galleries()
{
    QList<Gallery> result;
    QSqlQuery q(mainDb());
    if (exec(q, "SELECT id, name, root, allow_upscale FROM galleries ORDER BY name COLLATE NOCASE"))
        while (q.next())
            result.append({q.value(0).toLongLong(), q.value(1).toString(), resolvedRoot(q.value(2).toString()),
                           q.value(3).toBool()});
    return result;
}

qint64 createGallery(const Gallery &gallery)
{
    QSqlQuery q(mainDb());
    q.prepare("INSERT INTO galleries(name, root, allow_upscale) VALUES(?, ?, ?)");
    q.addBindValue(gallery.name);
    q.addBindValue(storedRoot(gallery.rootFolder));
    q.addBindValue(gallery.allowUpscale ? 1 : 0);
    if (!exec(q))
        return 0;
    return q.lastInsertId().toLongLong();
}

bool updateGallery(const Gallery &gallery)
{
    QSqlQuery q(mainDb());
    q.prepare("UPDATE galleries SET name = ?, root = ?, allow_upscale = ? WHERE id = ?");
    q.addBindValue(gallery.name);
    q.addBindValue(storedRoot(gallery.rootFolder));
    q.addBindValue(gallery.allowUpscale ? 1 : 0);
    q.addBindValue(gallery.id);
    return exec(q);
}

bool deleteGallery(const QSqlDatabase &db, qint64 galleryId)
{
    // images, tags and image_tags go away via ON DELETE CASCADE.
    QSqlQuery q(db);
    q.prepare("DELETE FROM galleries WHERE id = ?");
    q.addBindValue(galleryId);
    return exec(q);
}

QStringList allTags(qint64 galleryId)
{
    QStringList tags;
    QSqlQuery q(mainDb());
    q.prepare("SELECT name FROM tags WHERE gallery_id = ? ORDER BY name COLLATE NOCASE");
    q.addBindValue(galleryId);
    if (exec(q))
        while (q.next())
            tags << q.value(0).toString();
    return tags;
}

QStringList tagsForImage(qint64 imageId)
{
    QStringList tags;
    QSqlQuery q(mainDb());
    q.prepare("SELECT t.name FROM tags t JOIN image_tags it ON it.tag_id = t.id "
              "WHERE it.image_id = ? ORDER BY t.name COLLATE NOCASE");
    q.addBindValue(imageId);
    if (exec(q))
        while (q.next())
            tags << q.value(0).toString();
    return tags;
}

QList<ImageRecord> searchImages(qint64 galleryId,
                                const QStringList &includeTags,
                                const QStringList &excludeTags,
                                int offset, int limit, int *totalCount)
{
    QStringList inc = normalized(includeTags);
    QStringList exc = normalized(excludeTags);
    const bool onlyUntagged = inc.removeAll(NoTagsPseudoTag) > 0;
    const bool skipUntagged = exc.removeAll(NoTagsPseudoTag) > 0;

    // Tag ids are unique per gallery, so filtering tags by gallery_id is enough inside the subqueries.
    QStringList where{"i.gallery_id = ?"};
    QVariantList binds{galleryId};
    if (onlyUntagged)
        where << "NOT EXISTS (SELECT 1 FROM image_tags it WHERE it.image_id = i.id)";
    if (skipUntagged)
        where << "EXISTS (SELECT 1 FROM image_tags it WHERE it.image_id = i.id)";
    if (!inc.isEmpty()) {
        where << QString("i.id IN (SELECT it.image_id FROM image_tags it "
                         "JOIN tags t ON t.id = it.tag_id WHERE t.gallery_id = ? AND t.name IN (%1) "
                         "GROUP BY it.image_id HAVING COUNT(DISTINCT t.id) = %2)")
                     .arg(placeholders(inc.size()))
                     .arg(inc.size());
        binds << galleryId;
        for (const QString &t : inc)
            binds << t;
    }
    if (!exc.isEmpty()) {
        where << QString("i.id NOT IN (SELECT it.image_id FROM image_tags it "
                         "JOIN tags t ON t.id = it.tag_id WHERE t.gallery_id = ? AND t.name IN (%1))")
                     .arg(placeholders(exc.size()));
        binds << galleryId;
        for (const QString &t : exc)
            binds << t;
    }
    const QString whereSql = " WHERE " + where.join(" AND ");

    QSqlDatabase db = mainDb();

    if (totalCount) {
        *totalCount = 0;
        QSqlQuery cq(db);
        cq.prepare("SELECT COUNT(*) FROM images i" + whereSql);
        for (const QVariant &v : binds)
            cq.addBindValue(v);
        if (exec(cq) && cq.next())
            *totalCount = cq.value(0).toInt();
    }

    QList<ImageRecord> result;
    QSqlQuery q(db);
    q.prepare("SELECT i.id, i.path, i.rating FROM images i" + whereSql
              + " ORDER BY i.rating DESC, i.name COLLATE NOCASE, i.id LIMIT ? OFFSET ?");
    for (const QVariant &v : binds)
        q.addBindValue(v);
    q.addBindValue(limit);
    q.addBindValue(offset);
    if (!exec(q))
        return result;
    while (q.next())
        result.append({q.value(0).toLongLong(), q.value(1).toString(), q.value(2).toInt()});
    return result;
}

bool saveImageMeta(qint64 galleryId, qint64 imageId, int rating, const QStringList &tags)
{
    QSqlDatabase db = mainDb();
    if (!db.transaction()) {
        g_lastError = db.lastError().text();
        return false;
    }

    auto ok = [&] {
        QSqlQuery q(db);
        q.prepare("UPDATE images SET rating = ? WHERE id = ? AND gallery_id = ?");
        q.addBindValue(rating);
        q.addBindValue(imageId);
        q.addBindValue(galleryId);
        if (!exec(q))
            return false;

        q.prepare("DELETE FROM image_tags WHERE image_id = ?");
        q.addBindValue(imageId);
        if (!exec(q))
            return false;

        QSqlQuery insertTag(db), selectTag(db), link(db);
        insertTag.prepare("INSERT OR IGNORE INTO tags(gallery_id, name) VALUES(?, ?)");
        selectTag.prepare("SELECT id FROM tags WHERE gallery_id = ? AND name = ?");
        link.prepare("INSERT OR IGNORE INTO image_tags(image_id, tag_id) VALUES(?, ?)");
        for (const QString &tag : normalized(tags)) {
            if (tag == NoTagsPseudoTag)
                continue; // service tag, never stored
            insertTag.addBindValue(galleryId);
            insertTag.addBindValue(tag);
            if (!exec(insertTag))
                return false;
            selectTag.addBindValue(galleryId);
            selectTag.addBindValue(tag);
            if (!exec(selectTag) || !selectTag.next())
                return false;
            const qint64 tagId = selectTag.value(0).toLongLong();
            selectTag.finish();
            link.addBindValue(imageId);
            link.addBindValue(tagId);
            if (!exec(link))
                return false;
        }

        // Tags nobody uses any more disappear from the gallery.
        q.prepare("DELETE FROM tags WHERE gallery_id = ? "
                  "AND id NOT IN (SELECT DISTINCT tag_id FROM image_tags)");
        q.addBindValue(galleryId);
        return exec(q);
    }();

    if (ok && db.commit())
        return true;
    if (ok)
        g_lastError = db.lastError().text();
    db.rollback();
    return false;
}

} // namespace Db
