#include "database.h"

#include "collections.h"
#include "migrations.h"
#include "thumbcache.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QVariant>

namespace {

constexpr char16_t ForbiddenTagChars[] = {u',', u'<', u'>', u'/', u'\\'};

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
        const QString s = Db::normalizeTag(t);
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

QString normalizeTag(const QString &tag)
{
    const QString t = QString(tag).replace(u'_', u' ').simplified();
    if (t.isEmpty())
        return t;
    return t.left(1).toUpper() + t.mid(1).toLower();
}

bool isValidTagName(const QString &tag)
{
    for (const char16_t c : ForbiddenTagChars)
        if (tag.contains(QChar(c)))
            return false;
    return true;
}

QStringList tagsFromText(const QString &text)
{
    QStringList parts = text.split(QRegularExpression("[,\\r\\n]"), Qt::SkipEmptyParts);
    for (QString &part : parts)
        for (const char16_t c : ForbiddenTagChars)
            part.replace(QChar(c), QChar(u' '));
    return normalized(parts);
}

bool deleteUnusedTags(const QSqlDatabase &db, qint64 galleryId, QString *error)
{
    QSqlQuery q(db);
    q.prepare("DELETE FROM tags WHERE gallery_id = ? "
              "AND id NOT IN (SELECT DISTINCT tag_id FROM image_tags)");
    q.addBindValue(galleryId);
    if (q.exec())
        return true;
    if (error)
        *error = q.lastError().text();
    return false;
}

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

bool repairCollections()
{
    QSqlDatabase db = mainDb();
    QHash<qint64, QList<qint64>> unplaced; // gallery -> files without a virtual path
    QSet<qint64> galleries;
    {
        QSqlQuery q(db);
        if (!q.exec("SELECT gallery_id, id FROM images WHERE vpath_hash IS NULL")) {
            g_lastError = q.lastError().text();
            return false;
        }
        while (q.next()) {
            unplaced[q.value(0).toLongLong()] << q.value(1).toLongLong();
            galleries.insert(q.value(0).toLongLong());
        }
        // Collections whose files were all deleted or moved away.
        if (!q.exec("SELECT DISTINCT c.gallery_id FROM collections c WHERE NOT EXISTS ("
                    " SELECT 1 FROM images i WHERE i.gallery_id = c.gallery_id AND i.vpath_hash = c.vpath_hash)")) {
            g_lastError = q.lastError().text();
            return false;
        }
        while (q.next())
            galleries.insert(q.value(0).toLongLong());
    }
    if (galleries.isEmpty())
        return true;

    if (!db.transaction()) {
        g_lastError = db.lastError().text();
        return false;
    }
    bool ok = true;
    for (const qint64 galleryId : std::as_const(galleries)) {
        QList<Collections::Move> moves;
        ok = Collections::retag(db, unplaced.value(galleryId), &moves, &g_lastError)
             && Collections::sync(db, galleryId, moves, &g_lastError);
        if (!ok)
            break;
    }
    if (ok && db.commit())
        return true;
    if (ok)
        g_lastError = db.lastError().text();
    db.rollback();
    return false;
}

QString gallerySetting(qint64 galleryId, const QString &key, const QString &defaultValue)
{
    QSqlQuery q(mainDb());
    q.prepare("SELECT value FROM gallery_settings WHERE gallery_id = ? AND key = ?");
    q.addBindValue(galleryId);
    q.addBindValue(key);
    if (q.exec() && q.next())
        return q.value(0).toString();
    return defaultValue;
}

bool setGallerySetting(qint64 galleryId, const QString &key, const QString &value)
{
    QSqlQuery q(mainDb());
    q.prepare("INSERT INTO gallery_settings(gallery_id, key, value) VALUES(?, ?, ?) "
              "ON CONFLICT(gallery_id, key) DO UPDATE SET value = excluded.value");
    q.addBindValue(galleryId);
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
    QStringList hashes;
    QSqlQuery q(db);
    q.prepare("SELECT DISTINCT sha256 FROM images WHERE gallery_id = ?");
    q.addBindValue(galleryId);
    if (!exec(q))
        return false;
    while (q.next())
        hashes << q.value(0).toString();
    q.finish();

    // images, tags, image_tags and collections go away via ON DELETE CASCADE.
    q.prepare("DELETE FROM galleries WHERE id = ?");
    q.addBindValue(galleryId);
    if (!exec(q))
        return false;
    ThumbCache::removeUnused(db, hashes);
    return true;
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

QHash<QString, int> tagCollectionCounts(qint64 galleryId)
{
    // All files of a collection have the same tags, so a tag's collections are the distinct
    // vpath_hash values of its files.
    QHash<QString, int> counts;
    QSqlQuery q(mainDb());
    q.prepare("SELECT t.name, COUNT(DISTINCT i.vpath_hash) FROM tags t"
              " JOIN image_tags it ON it.tag_id = t.id"
              " JOIN images i ON i.id = it.image_id"
              " WHERE t.gallery_id = ? GROUP BY t.id");
    q.addBindValue(galleryId);
    if (exec(q))
        while (q.next())
            counts.insert(q.value(0).toString(), q.value(1).toInt());
    return counts;
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

QList<TagGroup> tagGroups(qint64 galleryId)
{
    QList<TagGroup> groups;
    QSqlQuery q(mainDb());
    q.prepare("SELECT id, name FROM tag_groups WHERE gallery_id = ? ORDER BY position, id");
    q.addBindValue(galleryId);
    if (exec(q))
        while (q.next())
            groups.append({q.value(0).toLongLong(), q.value(1).toString()});
    return groups;
}

bool saveTagGroups(qint64 galleryId, const QList<TagGroup> &groups)
{
    QSqlDatabase db = mainDb();
    if (!db.transaction()) {
        g_lastError = db.lastError().text();
        return false;
    }
    const bool ok = [&] {
        QSet<qint64> kept;
        for (const TagGroup &group : groups)
            if (group.id != 0)
                kept.insert(group.id);
        QSqlQuery q(db);
        q.prepare("SELECT id FROM tag_groups WHERE gallery_id = ?");
        q.addBindValue(galleryId);
        if (!exec(q))
            return false;
        QList<qint64> removed;
        while (q.next())
            if (!kept.contains(q.value(0).toLongLong()))
                removed << q.value(0).toLongLong();
        q.finish();
        for (const qint64 id : std::as_const(removed)) {
            // ON DELETE SET NULL does the same; explicit, so it doesn't depend on the pragma.
            q.prepare("UPDATE tags SET group_id = NULL WHERE group_id = ?");
            q.addBindValue(id);
            if (!exec(q))
                return false;
            q.prepare("DELETE FROM tag_groups WHERE id = ?");
            q.addBindValue(id);
            if (!exec(q))
                return false;
        }
        for (int position = 0; position < groups.size(); ++position) {
            const TagGroup &group = groups.at(position);
            if (group.id != 0) {
                q.prepare("UPDATE tag_groups SET name = ?, position = ? WHERE id = ? AND gallery_id = ?");
                q.addBindValue(group.name);
                q.addBindValue(position);
                q.addBindValue(group.id);
                q.addBindValue(galleryId);
            } else {
                q.prepare("INSERT INTO tag_groups(gallery_id, name, position) VALUES(?, ?, ?)");
                q.addBindValue(galleryId);
                q.addBindValue(group.name);
                q.addBindValue(position);
            }
            if (!exec(q))
                return false;
        }
        return true;
    }();
    if (ok && db.commit())
        return true;
    if (ok)
        g_lastError = db.lastError().text();
    db.rollback();
    return false;
}

bool setTagGroup(qint64 galleryId, const QString &tag, qint64 groupId)
{
    QSqlQuery q(mainDb());
    q.prepare("UPDATE tags SET group_id = ? WHERE gallery_id = ? AND name = ?");
    q.addBindValue(groupId != 0 ? QVariant(groupId) : QVariant(QMetaType::fromType<qint64>()));
    q.addBindValue(galleryId);
    q.addBindValue(tag);
    return exec(q);
}

bool prepareTagGroups(qint64 galleryId, const QHash<QString, qint64> &groups)
{
    if (groups.isEmpty())
        return true;
    QSqlDatabase db = mainDb();
    if (!db.transaction()) {
        g_lastError = db.lastError().text();
        return false;
    }
    const bool ok = [&] {
        QSqlQuery insert(db), update(db);
        insert.prepare("INSERT OR IGNORE INTO tags(gallery_id, name) VALUES(?, ?)");
        update.prepare("UPDATE tags SET group_id = ? WHERE gallery_id = ? AND name = ?");
        for (auto it = groups.cbegin(); it != groups.cend(); ++it) {
            const QString tag = normalizeTag(it.key());
            if (tag.isEmpty())
                continue;
            insert.addBindValue(galleryId);
            insert.addBindValue(tag);
            if (!exec(insert))
                return false;
            update.addBindValue(it.value() != 0 ? QVariant(it.value()) : QVariant(QMetaType::fromType<qint64>()));
            update.addBindValue(galleryId);
            update.addBindValue(tag);
            if (!exec(update))
                return false;
        }
        return true;
    }();
    if (ok && db.commit())
        return true;
    if (ok)
        g_lastError = db.lastError().text();
    db.rollback();
    return false;
}

QString defaultCollectionName(qint64 galleryId, const QString &virtualPath)
{
    QHash<QString, int> ranks;
    Collections::groupRanks(mainDb(), galleryId, &ranks, &g_lastError); // on an error: as if no groups
    return Collections::defaultName(virtualPath, ranks);
}

QList<TagSection> tagSections(qint64 galleryId, const QStringList &tags)
{
    QHash<QString, qint64> groupOf;
    QSqlQuery q(mainDb());
    q.prepare("SELECT name, group_id FROM tags WHERE gallery_id = ? AND group_id IS NOT NULL");
    q.addBindValue(galleryId);
    if (exec(q))
        while (q.next())
            groupOf.insert(q.value(0).toString(), q.value(1).toLongLong());

    QList<TagSection> sections{TagSection{}};
    QHash<qint64, qsizetype> indexOf{{0, 0}};
    for (const TagGroup &group : tagGroups(galleryId)) {
        indexOf.insert(group.id, sections.size());
        sections.append({group.id, group.name, {}});
    }
    for (const QString &tag : tags)
        sections[indexOf.value(groupOf.value(tag), 0)].tags << tag;
    sections.removeIf([](const TagSection &section) { return section.tags.isEmpty(); });
    return sections;
}

// WHERE clause (on "images i") and its bind values for a tag search; shared by the search
// itself and by the list of tags occurring in its results.
static QString searchFilter(qint64 galleryId, const TagQuery &query, QVariantList *binds)
{
    const QStringList inc = normalized(query.include);
    QStringList exc = normalized(query.exclude);
    const bool onlyUntagged = query.onlyUntagged;
    bool skipUntagged = query.skipUntagged;
    const bool exactTags = query.exact;
    if (exactTags) {
        exc.clear(); // "exactly these tags" already rules everything else out
        skipUntagged = false;
    }

    // Tag ids are unique per gallery, so filtering tags by gallery_id is enough inside the subqueries.
    QStringList where{"i.gallery_id = ?"};
    *binds = {galleryId};
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
        *binds << galleryId;
        for (const QString &t : inc)
            *binds << t;
    }
    // Exact mode: having all the wanted tags and exactly that many tags means no others.
    // (Empty include list = images without tags.)
    if (exactTags)
        where << QString("(SELECT COUNT(*) FROM image_tags it WHERE it.image_id = i.id) = %1").arg(inc.size());
    if (query.collectionId != 0) {
        where << "i.vpath_hash = (SELECT c.vpath_hash FROM collections c WHERE c.id = ? AND c.gallery_id = ?)";
        *binds << query.collectionId << galleryId;
    }
    if (!exc.isEmpty()) {
        where << QString("i.id NOT IN (SELECT it.image_id FROM image_tags it "
                         "JOIN tags t ON t.id = it.tag_id WHERE t.gallery_id = ? AND t.name IN (%1))")
                     .arg(placeholders(exc.size()));
        *binds << galleryId;
        for (const QString &t : exc)
            *binds << t;
    }
    return " WHERE " + where.join(" AND ");
}

// The files' collection rows, for sorting on their rating and name ("ic").
static const char *CollectionJoin =
    " LEFT JOIN collections ic ON ic.gallery_id = i.gallery_id AND ic.vpath_hash = i.vpath_hash";

// ORDER BY terms (on "images i" + CollectionJoin) of a sort order. Classic: the collection's rating
// (higher first), the name it is shown with, its virtual path; file name and id always last.
static QString orderTerms(SortOrder order)
{
    QString firstKey;
    switch (order) {
    case SortOrder::Classic:
        break;
    case SortOrder::NewestFirst:
        firstKey = "i.added_at DESC, ";
        break;
    case SortOrder::LargestFirst:
        firstKey = "i.size DESC, ";
        break;
    }
    return firstKey + "ic.rating DESC, ic.name_key, i.vpath_key, i.name COLLATE NOCASE, i.id";
}

QStringList tagsOfSearch(qint64 galleryId, const TagQuery &query)
{
    QVariantList binds;
    const QString whereSql = searchFilter(galleryId, query, &binds);
    QStringList tags;
    QSqlQuery q(mainDb());
    q.prepare("SELECT DISTINCT t.name FROM tags t JOIN image_tags it ON it.tag_id = t.id "
              "WHERE it.image_id IN (SELECT i.id FROM images i" + whereSql + ") "
              "ORDER BY t.name COLLATE NOCASE");
    for (const QVariant &v : binds)
        q.addBindValue(v);
    if (exec(q))
        while (q.next())
            tags << q.value(0).toString();
    return tags;
}

QList<ImageRecord> searchImages(qint64 galleryId, const TagQuery &query,
                                int offset, int limit, int *totalCount, SortOrder order)
{
    QVariantList binds;
    const QString whereSql = searchFilter(galleryId, query, &binds);

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
    q.prepare("SELECT i.id, i.path, i.size, i.sha256 FROM images i" + QString(CollectionJoin) + whereSql + " ORDER BY " + orderTerms(order)
              + " LIMIT ? OFFSET ?");
    for (const QVariant &v : binds)
        q.addBindValue(v);
    q.addBindValue(limit);
    q.addBindValue(offset);
    if (!exec(q))
        return result;
    while (q.next())
        result.append({q.value(0).toLongLong(), q.value(1).toString(), q.value(2).toLongLong(), q.value(3).toString()});
    return result;
}

// Id, name, rating, virtual path and the cover's path (or the first file's) of collection "c".
static const char *CollectionColumns =
    "c.id, c.name, c.rating, "
    "(SELECT i.vpath FROM images i WHERE i.gallery_id = c.gallery_id AND i.vpath_hash = c.vpath_hash LIMIT 1), "
    "COALESCE((SELECT i.path FROM images i WHERE i.id = c.cover_image_id), "
    " (SELECT i.path FROM images i WHERE i.gallery_id = c.gallery_id AND i.vpath_hash = c.vpath_hash "
    "  ORDER BY i.name COLLATE NOCASE, i.id LIMIT 1)), "
    "COALESCE((SELECT i.sha256 FROM images i WHERE i.id = c.cover_image_id), "
    " (SELECT i.sha256 FROM images i WHERE i.gallery_id = c.gallery_id AND i.vpath_hash = c.vpath_hash "
    "  ORDER BY i.name COLLATE NOCASE, i.id LIMIT 1)), "
    "c.name_manual";

// Columns after CollectionColumns: the file count.
static CollectionRecord collectionFrom(const QSqlQuery &q)
{
    return {q.value(0).toLongLong(), q.value(1).toString(), q.value(3).toString(), q.value(2).toInt(),
            q.value(4).toString(), q.value(7).toInt(), q.value(5).toString(), q.value(6).toBool()};
}

QList<CollectionRecord> searchCollections(qint64 galleryId, const TagQuery &query, int offset, int limit,
                                          int *totalCollections, int *totalFiles, SortOrder order)
{
    QVariantList binds;
    const QString whereSql = searchFilter(galleryId, query, &binds);
    QSqlDatabase db = mainDb();

    if (totalCollections || totalFiles) {
        QSqlQuery cq(db);
        cq.prepare("SELECT COUNT(DISTINCT i.vpath_hash), COUNT(*) FROM images i" + whereSql);
        for (const QVariant &v : binds)
            cq.addBindValue(v);
        const bool ok = exec(cq) && cq.next();
        if (totalCollections)
            *totalCollections = ok ? cq.value(0).toInt() : 0;
        if (totalFiles)
            *totalFiles = ok ? cq.value(1).toInt() : 0;
    }

    // Each found file gets its place in the search order; a collection comes where its first file does.
    QList<CollectionRecord> result;
    QSqlQuery q(db);
    q.prepare(QString("SELECT %1, f.files FROM ("
                      " SELECT r.vpath_hash, MIN(r.rn) AS first, COUNT(*) AS files FROM ("
                      "  SELECT i.vpath_hash, ROW_NUMBER() OVER (ORDER BY %2) AS rn FROM images i%3%4) r"
                      " GROUP BY r.vpath_hash) f "
                      "JOIN collections c ON c.gallery_id = ? AND c.vpath_hash = f.vpath_hash "
                      "ORDER BY f.first LIMIT ? OFFSET ?")
                  .arg(CollectionColumns, orderTerms(order), CollectionJoin, whereSql));
    for (const QVariant &v : binds)
        q.addBindValue(v);
    q.addBindValue(galleryId);
    q.addBindValue(limit);
    q.addBindValue(offset);
    if (!exec(q))
        return result;
    while (q.next())
        result.append(collectionFrom(q));
    return result;
}

CollectionRecord collection(qint64 galleryId, qint64 collectionId)
{
    QSqlQuery q(mainDb());
    q.prepare(QString("SELECT %1, (SELECT COUNT(*) FROM images i WHERE i.gallery_id = c.gallery_id "
                      "AND i.vpath_hash = c.vpath_hash) FROM collections c WHERE c.id = ? AND c.gallery_id = ?")
                  .arg(CollectionColumns));
    q.addBindValue(collectionId);
    q.addBindValue(galleryId);
    if (!exec(q) || !q.next())
        return {};
    return collectionFrom(q);
}

bool isCollectionCover(qint64 galleryId, qint64 imageId)
{
    QSqlQuery q(mainDb());
    q.prepare("SELECT 1 FROM collections c JOIN images i ON i.gallery_id = c.gallery_id AND i.vpath_hash = c.vpath_hash "
              "WHERE i.id = ? AND i.gallery_id = ? AND c.cover_image_id = i.id");
    q.addBindValue(imageId);
    q.addBindValue(galleryId);
    return exec(q) && q.next();
}

bool setCollectionCover(qint64 galleryId, qint64 imageId)
{
    QSqlQuery q(mainDb());
    q.prepare("UPDATE collections SET cover_image_id = ? WHERE gallery_id = ? AND vpath_hash = "
              "(SELECT vpath_hash FROM images WHERE id = ? AND gallery_id = ?)");
    q.addBindValue(imageId);
    q.addBindValue(galleryId);
    q.addBindValue(imageId);
    q.addBindValue(galleryId);
    return exec(q);
}

bool updateCollection(qint64 galleryId, qint64 collectionId, const QString &name, bool nameManual, int rating)
{
    const QString virtualPath = collection(galleryId, collectionId).virtualPath;
    QSqlQuery q(mainDb());
    q.prepare("UPDATE collections SET name = ?, name_key = ?, name_manual = ?, rating = ? WHERE id = ? AND gallery_id = ?");
    q.addBindValue(name.isNull() ? QStringLiteral("") : name);
    q.addBindValue(Collections::nameKey(name, virtualPath));
    q.addBindValue(nameManual ? 1 : 0);
    q.addBindValue(qBound(Collections::MinRating, rating, Collections::MaxRating));
    q.addBindValue(collectionId);
    q.addBindValue(galleryId);
    return exec(q);
}

int renameCollections(qint64 galleryId, bool onlyAutomatic)
{
    QSqlDatabase db = mainDb();
    if (!db.transaction()) {
        g_lastError = db.lastError().text();
        return -1;
    }
    int renamed = 0;
    const bool ok = [&] {
        QHash<QString, int> ranks;
        if (!Collections::groupRanks(db, galleryId, &ranks, &g_lastError))
            return false;
        struct Row
        {
            qint64 id;
            QString name;
            QString virtualPath;
            bool manual;
        };
        QList<Row> rows;
        QSqlQuery q(db);
        q.prepare("SELECT c.id, c.name, (SELECT i.vpath FROM images i WHERE i.gallery_id = c.gallery_id "
                  "AND i.vpath_hash = c.vpath_hash LIMIT 1), c.name_manual FROM collections c WHERE c.gallery_id = ?");
        q.addBindValue(galleryId);
        if (!exec(q))
            return false;
        while (q.next())
            rows.append({q.value(0).toLongLong(), q.value(1).toString(), q.value(2).toString(), q.value(3).toBool()});
        q.finish();

        q.prepare("UPDATE collections SET name = ?, name_key = ?, name_manual = 0 WHERE id = ?");
        for (const Row &row : std::as_const(rows)) {
            if (onlyAutomatic && row.manual)
                continue;
            const QString name = Collections::defaultName(row.virtualPath, ranks);
            if (name == row.name && !row.manual)
                continue; // already so
            q.addBindValue(name);
            q.addBindValue(Collections::nameKey(name, row.virtualPath));
            q.addBindValue(row.id);
            if (!exec(q))
                return false;
            if (name != row.name)
                ++renamed;
        }
        return true;
    }();
    if (ok && db.commit())
        return renamed;
    if (ok)
        g_lastError = db.lastError().text();
    db.rollback();
    return -1;
}

bool saveImageMeta(qint64 galleryId, qint64 imageId, const QStringList &tags)
{
    QSqlDatabase db = mainDb();
    if (!db.transaction()) {
        g_lastError = db.lastError().text();
        return false;
    }

    auto ok = [&] {
        QSqlQuery q(db);
        q.prepare("SELECT 1 FROM images WHERE id = ? AND gallery_id = ?");
        q.addBindValue(imageId);
        q.addBindValue(galleryId);
        if (!exec(q))
            return false;
        if (!q.next()) {
            g_lastError = QStringLiteral("No such file record");
            return false;
        }
        q.finish();

        q.prepare("DELETE FROM image_tags WHERE image_id = ?");
        q.addBindValue(imageId);
        if (!exec(q))
            return false;

        QSqlQuery insertTag(db), selectTag(db), link(db);
        insertTag.prepare("INSERT OR IGNORE INTO tags(gallery_id, name) VALUES(?, ?)");
        selectTag.prepare("SELECT id FROM tags WHERE gallery_id = ? AND name = ?");
        link.prepare("INSERT OR IGNORE INTO image_tags(image_id, tag_id) VALUES(?, ?)");
        for (const QString &tag : normalized(tags)) {
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

        if (!Collections::update(db, galleryId, {imageId}, &g_lastError))
            return false;

        // Tags nobody uses any more disappear from the gallery.
        return deleteUnusedTags(db, galleryId, &g_lastError);
    }();

    if (ok && db.commit())
        return true;
    if (ok)
        g_lastError = db.lastError().text();
    db.rollback();
    return false;
}

QList<ImageRecord> imagesByIds(qint64 galleryId, const QList<qint64> &imageIds)
{
    QList<ImageRecord> result;
    if (imageIds.isEmpty())
        return result;
    QStringList ids;
    for (qint64 id : imageIds)
        ids << QString::number(id); // numbers only, safe to inline
    QSqlQuery q(mainDb());
    q.prepare("SELECT id, path, size, sha256 FROM images WHERE gallery_id = ? AND id IN (" + ids.join(',')
              + ") ORDER BY path COLLATE NOCASE");
    q.addBindValue(galleryId);
    if (exec(q))
        while (q.next())
            result.append({q.value(0).toLongLong(), q.value(1).toString(), q.value(2).toLongLong(),
                           q.value(3).toString()});
    return result;
}

bool renameTag(qint64 galleryId, const QString &oldName, const QString &newName)
{
    QSqlDatabase db = mainDb();
    if (!db.transaction()) {
        g_lastError = db.lastError().text();
        return false;
    }
    const bool ok = [&] {
        QSqlQuery q(db);
        q.prepare("UPDATE tags SET name = ? WHERE gallery_id = ? AND name = ?");
        q.addBindValue(newName);
        q.addBindValue(galleryId);
        q.addBindValue(oldName);
        if (!exec(q))
            return false;
        // Every file with this tag gets a new virtual path.
        QList<qint64> ids;
        q.prepare("SELECT it.image_id FROM image_tags it JOIN tags t ON t.id = it.tag_id "
                  "WHERE t.gallery_id = ? AND t.name = ?");
        q.addBindValue(galleryId);
        q.addBindValue(newName);
        if (!exec(q))
            return false;
        while (q.next())
            ids << q.value(0).toLongLong();
        q.finish();
        return Collections::update(db, galleryId, ids, &g_lastError);
    }();
    if (ok && db.commit())
        return true;
    if (ok)
        g_lastError = db.lastError().text();
    db.rollback();
    return false;
}

bool applyTagChanges(qint64 galleryId, const QList<qint64> &imageIds,
                     const QStringList &added, const QStringList &removed)
{
    QSqlDatabase db = mainDb();
    if (!db.transaction()) {
        g_lastError = db.lastError().text();
        return false;
    }

    const bool ok = [&] {
        QSqlQuery insertTag(db), link(db), unlink(db), cleanup(db);
        insertTag.prepare("INSERT OR IGNORE INTO tags(gallery_id, name) VALUES(?, ?)");
        link.prepare("INSERT OR IGNORE INTO image_tags(image_id, tag_id) "
                     "SELECT ?, id FROM tags WHERE gallery_id = ? AND name = ?");
        unlink.prepare("DELETE FROM image_tags WHERE image_id = ? AND tag_id IN "
                       "(SELECT id FROM tags WHERE gallery_id = ? AND name = ?)");

        for (const QString &tag : normalized(added)) {
            insertTag.addBindValue(galleryId);
            insertTag.addBindValue(tag);
            if (!exec(insertTag))
                return false;
            for (qint64 id : imageIds) {
                link.addBindValue(id); // OR IGNORE: already there -> stays once
                link.addBindValue(galleryId);
                link.addBindValue(tag);
                if (!exec(link))
                    return false;
            }
        }
        for (const QString &tag : normalized(removed)) {
            for (qint64 id : imageIds) {
                unlink.addBindValue(id);
                unlink.addBindValue(galleryId);
                unlink.addBindValue(tag);
                if (!exec(unlink))
                    return false;
            }
        }
        if (!Collections::update(db, galleryId, imageIds, &g_lastError))
            return false;
        return deleteUnusedTags(db, galleryId, &g_lastError);
    }();

    if (ok && db.commit())
        return true;
    if (ok)
        g_lastError = db.lastError().text();
    db.rollback();
    return false;
}

bool deleteTags(qint64 galleryId, const QStringList &names)
{
    if (names.isEmpty())
        return true;
    QSqlDatabase db = mainDb();
    if (!db.transaction()) {
        g_lastError = db.lastError().text();
        return false;
    }

    const bool ok = [&] {
        QSet<qint64> ids; // an image with several of the tags needs one new virtual path
        QSqlQuery select(db), unlink(db);
        select.prepare("SELECT it.image_id FROM image_tags it JOIN tags t ON t.id = it.tag_id "
                       "WHERE t.gallery_id = ? AND t.name = ?");
        unlink.prepare("DELETE FROM image_tags WHERE tag_id IN "
                       "(SELECT id FROM tags WHERE gallery_id = ? AND name = ?)");
        for (const QString &name : names) {
            select.addBindValue(galleryId);
            select.addBindValue(name);
            if (!exec(select))
                return false;
            while (select.next())
                ids.insert(select.value(0).toLongLong());
            select.finish();
            unlink.addBindValue(galleryId);
            unlink.addBindValue(name);
            if (!exec(unlink))
                return false;
        }
        if (!Collections::update(db, galleryId, ids.values(), &g_lastError))
            return false;
        return deleteUnusedTags(db, galleryId, &g_lastError);
    }();

    if (ok && db.commit())
        return true;
    if (ok)
        g_lastError = db.lastError().text();
    db.rollback();
    return false;
}

bool updateImageFile(qint64 galleryId, const QString &relativePath, qint64 size, qint64 mtime,
                     const QString &sha256)
{
    QSqlQuery q(mainDb());
    q.prepare("SELECT sha256 FROM images WHERE gallery_id = ? AND path = ?");
    q.addBindValue(galleryId);
    q.addBindValue(relativePath);
    const QString oldSha256 = exec(q) && q.next() ? q.value(0).toString() : QString();
    q.finish();

    q.prepare("UPDATE images SET size = ?, mtime = ?, sha256 = ? WHERE gallery_id = ? AND path = ?");
    q.addBindValue(size);
    q.addBindValue(mtime);
    q.addBindValue(sha256);
    q.addBindValue(galleryId);
    q.addBindValue(relativePath);
    if (!exec(q))
        return false;
    if (oldSha256 != sha256)
        ThumbCache::removeUnused(mainDb(), {oldSha256}); // the picture changed (rotation)
    return true;
}

bool renameImage(qint64 galleryId, qint64 imageId, const QString &relativePath, const QString &name)
{
    QSqlQuery q(mainDb());
    q.prepare("UPDATE images SET path = ?, name = ? WHERE gallery_id = ? AND id = ?");
    q.addBindValue(relativePath);
    q.addBindValue(name);
    q.addBindValue(galleryId);
    q.addBindValue(imageId);
    return exec(q);
}

QList<DuplicateGroup> findDuplicates(qint64 galleryId)
{
    QList<DuplicateGroup> groups;
    QSqlQuery q(mainDb());
    q.prepare("SELECT i.sha256, i.size, i.id, i.path, i.vpath, c.name, "
              " EXISTS(SELECT 1 FROM ignored_duplicates d WHERE d.gallery_id = i.gallery_id "
              "        AND d.path = i.path AND d.sha256 = i.sha256) FROM images i "
              "LEFT JOIN collections c ON c.gallery_id = i.gallery_id AND c.vpath_hash = i.vpath_hash "
              "WHERE i.gallery_id = ? AND i.sha256 IN ("
              " SELECT sha256 FROM images WHERE gallery_id = ? GROUP BY sha256 HAVING COUNT(*) > 1) "
              "ORDER BY i.sha256");
    q.addBindValue(galleryId);
    q.addBindValue(galleryId);
    if (!exec(q))
        return groups;
    while (q.next()) {
        const QString hash = q.value(0).toString();
        if (groups.isEmpty() || groups.last().sha256 != hash)
            groups.append({hash, q.value(1).toLongLong(), {}});
        groups.last().files.append({{q.value(2).toLongLong(), q.value(3).toString(), groups.last().size, hash},
                                    q.value(4).toString(),
                                    q.value(5).toString(),
                                    q.value(6).toBool()});
    }
    return groups;
}

bool setDuplicatesIgnored(qint64 galleryId, const QList<ImageRecord> &files, bool ignored)
{
    QSqlDatabase db = mainDb();
    if (!db.transaction()) {
        g_lastError = db.lastError().text();
        return false;
    }
    bool ok = true;
    {
        QSqlQuery q(db);
        q.prepare(ignored ? "INSERT OR IGNORE INTO ignored_duplicates(gallery_id, path, sha256) VALUES(?, ?, ?)"
                          : "DELETE FROM ignored_duplicates WHERE gallery_id = ? AND path = ? AND sha256 = ?");
        for (const ImageRecord &file : files) {
            q.addBindValue(galleryId);
            q.addBindValue(file.relativePath);
            q.addBindValue(file.sha256);
            if (!(ok = exec(q)))
                break;
        }
    }
    if (ok && db.commit())
        return true;
    if (ok)
        g_lastError = db.lastError().text();
    db.rollback();
    return false;
}

bool pruneIgnoredDuplicates(qint64 galleryId)
{
    QSqlQuery q(mainDb());
    q.prepare("DELETE FROM ignored_duplicates WHERE gallery_id = ? AND NOT EXISTS("
              " SELECT 1 FROM images i WHERE i.gallery_id = ignored_duplicates.gallery_id "
              " AND i.path = ignored_duplicates.path AND i.sha256 = ignored_duplicates.sha256)");
    q.addBindValue(galleryId);
    return exec(q);
}

bool deleteImages(qint64 galleryId, const QList<qint64> &imageIds)
{
    QSqlDatabase db = mainDb();
    if (!db.transaction()) {
        g_lastError = db.lastError().text();
        return false;
    }
    bool ok = true;
    QStringList hashes;
    {
        QSqlQuery q(db);
        q.prepare("SELECT sha256 FROM images WHERE id = ? AND gallery_id = ?");
        for (qint64 id : imageIds) {
            q.addBindValue(id);
            q.addBindValue(galleryId);
            if (!(ok = exec(q)))
                break;
            if (q.next())
                hashes << q.value(0).toString();
            q.finish();
        }
        q.prepare("DELETE FROM images WHERE id = ? AND gallery_id = ?"); // image_tags: ON DELETE CASCADE
        for (qint64 id : ok ? imageIds : QList<qint64>{}) {
            q.addBindValue(id);
            q.addBindValue(galleryId);
            if (!(ok = exec(q)))
                break;
        }
        if (ok)
            ok = deleteUnusedTags(db, galleryId, &g_lastError)
                 && Collections::sync(db, galleryId, {}, &g_lastError); // emptied collections go away
    }
    if (ok && db.commit()) {
        ThumbCache::removeUnused(db, hashes);
        return true;
    }
    if (ok)
        g_lastError = db.lastError().text();
    db.rollback();
    return false;
}

// ---- Download profiles --------------------------------------------------------------------

static QString attributesJson(const DownloadProfile &profile)
{
    QJsonArray array;
    for (const auto &attribute : profile.attributes)
        array.append(QJsonObject{{"name", attribute.first}, {"value", attribute.second}});
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

QList<DownloadProfile> downloadProfiles()
{
    QList<DownloadProfile> profiles;
    QSqlQuery q(mainDb());
    if (!exec(q, "SELECT name, user_agent, mode, tag, source, classes, attributes, attempts FROM download_profiles"))
        return profiles;
    while (q.next()) {
        DownloadProfile profile;
        profile.name = q.value(0).toString();
        profile.userAgent = q.value(1).toString();
        profile.mode = q.value(2).toInt() == 0 ? DownloadProfile::Mode::Page : DownloadProfile::Mode::Files;
        switch (q.value(3).toInt()) {
        case 1:
            profile.tag = DownloadProfile::Tag::Img;
            break;
        case 2:
            profile.tag = DownloadProfile::Tag::A;
            break;
        default:
            profile.tag = DownloadProfile::Tag::Any;
        }
        profile.source = q.value(4).toString();
        profile.classes = q.value(5).toString();
        const QJsonArray attributes = QJsonDocument::fromJson(q.value(6).toString().toUtf8()).array();
        for (const QJsonValue &value : attributes) {
            const QJsonObject attribute = value.toObject();
            profile.attributes.append({attribute.value("name").toString(), attribute.value("value").toString()});
        }
        profile.attempts = q.value(7).toInt();
        profile.normalize();
        profiles << profile;
    }
    std::sort(profiles.begin(), profiles.end(), [](const DownloadProfile &a, const DownloadProfile &b) {
        return QString::compare(a.name, b.name, Qt::CaseInsensitive) < 0;
    });
    return profiles;
}

static bool storeDownloadProfile(const QSqlDatabase &db, const DownloadProfile &profile)
{
    // A null QString would be bound as NULL; the columns are NOT NULL.
    const auto text = [](const QString &value) { return value.isNull() ? QStringLiteral("") : value; };
    QSqlQuery q(db);
    q.prepare("INSERT INTO download_profiles(name, user_agent, mode, tag, source, classes, attributes, attempts) "
              "VALUES(?, ?, ?, ?, ?, ?, ?, ?) "
              "ON CONFLICT(name) DO UPDATE SET user_agent = excluded.user_agent, mode = excluded.mode, "
              "tag = excluded.tag, source = excluded.source, classes = excluded.classes, "
              "attributes = excluded.attributes, attempts = excluded.attempts");
    q.addBindValue(profile.name);
    q.addBindValue(text(profile.userAgent));
    q.addBindValue(profile.mode == DownloadProfile::Mode::Page ? 0 : 1);
    q.addBindValue(profile.tag == DownloadProfile::Tag::Img ? 1 : profile.tag == DownloadProfile::Tag::A ? 2 : 0);
    q.addBindValue(text(profile.source));
    q.addBindValue(text(profile.classes));
    q.addBindValue(attributesJson(profile));
    q.addBindValue(profile.attempts);
    return exec(q);
}

bool saveDownloadProfile(const DownloadProfile &profile)
{
    return storeDownloadProfile(mainDb(), profile);
}

bool deleteDownloadProfile(const QString &name)
{
    QSqlQuery q(mainDb());
    q.prepare("DELETE FROM download_profiles WHERE name = ?");
    q.addBindValue(name);
    return exec(q);
}

bool saveDownloadProfiles(const QList<DownloadProfile> &profiles, int *replaced)
{
    QSqlDatabase db = mainDb();
    QSet<QString> existing;
    {
        QSqlQuery q(db);
        if (!exec(q, "SELECT name FROM download_profiles"))
            return false;
        while (q.next())
            existing.insert(q.value(0).toString());
    }
    if (!db.transaction()) {
        g_lastError = db.lastError().text();
        return false;
    }
    *replaced = 0;
    for (const DownloadProfile &profile : profiles) {
        if (!storeDownloadProfile(db, profile)) {
            db.rollback();
            return false;
        }
        if (existing.contains(profile.name))
            ++*replaced;
        existing.insert(profile.name); // a name twice in the file: the second one replaces
    }
    if (!db.commit()) {
        g_lastError = db.lastError().text();
        db.rollback();
        return false;
    }
    return true;
}

} // namespace Db
