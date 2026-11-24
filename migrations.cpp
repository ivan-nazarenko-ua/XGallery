#include "migrations.h"

#include "collections.h"
#include "mediafiles.h"
#include "virtualpath.h"

#include <QFileInfo>
#include <QHash>
#include <QList>
#include <QMap>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QVariant>

namespace {

// Thrown-away helper state for one step: a query plus the error sink.
class Sql
{
public:
    Sql(QSqlDatabase &db, QString *error) : m_db(db), m_q(db), m_error(error) {}

    QSqlDatabase &db() { return m_db; }
    QSqlQuery &query() { return m_q; }

    bool exec(const QString &sql)
    {
        if (m_q.exec(sql))
            return true;
        *m_error = m_q.lastError().text() + "\n" + sql;
        return false;
    }

    bool exec()
    {
        if (m_q.exec())
            return true;
        *m_error = m_q.lastError().text();
        return false;
    }

    bool fail(const QString &message)
    {
        *m_error = message;
        return false;
    }

    bool hasColumn(const QString &table, const QString &column)
    {
        QSqlQuery q(m_db);
        if (!q.exec(QString("PRAGMA table_info(%1)").arg(table)))
            return false;
        while (q.next())
            if (q.value(1).toString() == column)
                return true;
        return false;
    }

private:
    QSqlDatabase &m_db;
    QSqlQuery m_q;
    QString *m_error;
};

// ---- v0 -> v1: galleries --------------------------------------------------------------
// Fresh DB: creates the v1 schema.
// Pre-galleries DB (single root folder in settings, images/tags without gallery_id):
// moves its data into one gallery, keeping ids, so nothing has to be re-hashed.
bool toV1Galleries(Sql &s)
{
    const bool legacy = s.db().tables().contains("images") && !s.hasColumn("images", "gallery_id");
    if (legacy
        && !(s.exec("ALTER TABLE image_tags RENAME TO image_tags_v0")
             && s.exec("ALTER TABLE images RENAME TO images_v0")
             && s.exec("ALTER TABLE tags RENAME TO tags_v0")
             && s.exec("DROP INDEX IF EXISTS idx_images_sha256")
             && s.exec("DROP INDEX IF EXISTS idx_image_tags_tag")))
        return false;

    const bool created =
        s.exec("CREATE TABLE IF NOT EXISTS settings ("
               " key TEXT PRIMARY KEY,"
               " value TEXT)")
        && s.exec("CREATE TABLE IF NOT EXISTS galleries ("
                  " id INTEGER PRIMARY KEY AUTOINCREMENT,"
                  " name TEXT NOT NULL UNIQUE,"
                  " root TEXT NOT NULL)")
        && s.exec("CREATE TABLE IF NOT EXISTS images ("
                  " id INTEGER PRIMARY KEY AUTOINCREMENT,"
                  " gallery_id INTEGER NOT NULL REFERENCES galleries(id) ON DELETE CASCADE,"
                  " path TEXT NOT NULL,"         // relative to the gallery root, '/' separated
                  " size INTEGER NOT NULL,"
                  " mtime INTEGER NOT NULL,"     // ms since epoch, UTC
                  " sha256 TEXT NOT NULL,"
                  " added_at INTEGER NOT NULL DEFAULT (strftime('%s','now')),"
                  " UNIQUE (gallery_id, path))")
        && s.exec("CREATE INDEX IF NOT EXISTS idx_images_sha256 ON images(sha256)")
        && s.exec("CREATE TABLE IF NOT EXISTS tags ("
                  " id INTEGER PRIMARY KEY AUTOINCREMENT,"
                  " gallery_id INTEGER NOT NULL REFERENCES galleries(id) ON DELETE CASCADE,"
                  " name TEXT NOT NULL,"
                  " UNIQUE (gallery_id, name))")
        && s.exec("CREATE TABLE IF NOT EXISTS image_tags ("
                  " image_id INTEGER NOT NULL REFERENCES images(id) ON DELETE CASCADE,"
                  " tag_id INTEGER NOT NULL REFERENCES tags(id) ON DELETE CASCADE,"
                  " PRIMARY KEY (image_id, tag_id))")
        && s.exec("CREATE INDEX IF NOT EXISTS idx_image_tags_tag ON image_tags(tag_id)");
    if (!created)
        return false;
    if (!legacy)
        return true;

    QSqlQuery &q = s.query();
    QString root;
    if (q.exec("SELECT value FROM settings WHERE key = 'root_folder'") && q.next())
        root = q.value(0).toString();
    q.finish();
    if (!s.exec("SELECT COUNT(*) FROM images_v0") || !q.next())
        return false;
    const bool hasImages = q.value(0).toInt() > 0;
    q.finish();

    if (!root.isEmpty() || hasImages) {
        QString name = QFileInfo(root).fileName();
        if (name.isEmpty())
            name = QStringLiteral("Моя галерея");
        q.prepare("INSERT INTO galleries(name, root) VALUES(?, ?)");
        q.addBindValue(name);
        q.addBindValue(root);
        if (!s.exec())
            return false;
        const QString gid = QString::number(q.lastInsertId().toLongLong());

        if (!s.exec("INSERT INTO images(id, gallery_id, path, size, mtime, sha256, added_at) "
                    "SELECT id, " + gid + ", path, size, mtime, sha256, added_at FROM images_v0")
            || !s.exec("INSERT INTO tags(id, gallery_id, name) SELECT id, " + gid + ", name FROM tags_v0")
            || !s.exec("INSERT INTO image_tags(image_id, tag_id) SELECT image_id, tag_id FROM image_tags_v0")
            || !s.exec("INSERT OR REPLACE INTO settings(key, value) VALUES('current_gallery', " + gid + ")"))
            return false;
    }

    return s.exec("DROP TABLE image_tags_v0")
        && s.exec("DROP TABLE images_v0")
        && s.exec("DROP TABLE tags_v0")
        && s.exec("DELETE FROM settings WHERE key = 'root_folder'");
}

// ---- v1 -> v2: rating + file name for sorting -------------------------------------------
bool toV2RatingAndName(Sql &s)
{
    if (!s.exec("ALTER TABLE images ADD COLUMN rating INTEGER NOT NULL DEFAULT 50")
        || !s.exec("ALTER TABLE images ADD COLUMN name TEXT NOT NULL DEFAULT ''"))
        return false;

    // SQLite has no basename(): fill the file name from the relative path here.
    QList<QPair<qint64, QString>> rows;
    QSqlQuery &q = s.query();
    if (!s.exec("SELECT id, path FROM images"))
        return false;
    while (q.next())
        rows.append({q.value(0).toLongLong(), q.value(1).toString()});
    q.finish();

    q.prepare("UPDATE images SET name = ? WHERE id = ?");
    for (const auto &row : std::as_const(rows)) {
        q.addBindValue(row.second.section('/', -1));
        q.addBindValue(row.first);
        if (!s.exec())
            return false;
    }

    return s.exec("CREATE INDEX IF NOT EXISTS idx_images_order "
                  "ON images(gallery_id, rating DESC, name COLLATE NOCASE)");
}

// ---- v2 -> v3: per-gallery viewer option -----------------------------------------------
bool toV3AllowUpscale(Sql &s)
{
    return s.exec("ALTER TABLE galleries ADD COLUMN allow_upscale INTEGER NOT NULL DEFAULT 0");
}

// ---- v3 -> v4: no commas in tags --------------------------------------------------------
// Folder names became tags as-is, but a comma can't be searched for. "name,1" -> "name 1";
// if that tag already exists in the gallery, the two are merged.
bool toV4NoCommasInTags(Sql &s)
{
    struct Tag { qint64 id; qint64 galleryId; QString name; };
    QList<Tag> withCommas;
    QSqlQuery &q = s.query();
    if (!s.exec("SELECT id, gallery_id, name FROM tags WHERE name LIKE '%,%'"))
        return false;
    while (q.next())
        withCommas.append({q.value(0).toLongLong(), q.value(1).toLongLong(), q.value(2).toString()});
    q.finish();

    QSqlQuery find(s.db());
    find.prepare("SELECT id FROM tags WHERE gallery_id = ? AND name = ?");
    for (const Tag &tag : std::as_const(withCommas)) {
        const QString newName = QString(tag.name).replace(',', ' ').simplified();
        find.addBindValue(tag.galleryId);
        find.addBindValue(newName);
        if (!find.exec())
            return false;
        const qint64 existing = find.next() ? find.value(0).toLongLong() : 0;
        find.finish();

        if (existing == 0 && !newName.isEmpty()) {
            q.prepare("UPDATE tags SET name = ? WHERE id = ?");
            q.addBindValue(newName);
            q.addBindValue(tag.id);
            if (!s.exec())
                return false;
            continue;
        }
        if (existing != 0) {
            q.prepare("INSERT OR IGNORE INTO image_tags(image_id, tag_id) "
                      "SELECT image_id, ? FROM image_tags WHERE tag_id = ?");
            q.addBindValue(existing);
            q.addBindValue(tag.id);
            if (!s.exec())
                return false;
        }
        q.prepare("DELETE FROM image_tags WHERE tag_id = ?");
        q.addBindValue(tag.id);
        if (!s.exec())
            return false;
        q.prepare("DELETE FROM tags WHERE id = ?");
        q.addBindValue(tag.id);
        if (!s.exec())
            return false;
    }
    return true;
}

// ---- v4 -> v5: one spelling per tag ------------------------------------------------------
// "коти", "КОТИ" and "Коти" become "Коти": whitespace collapsed, first letter upper case,
// the rest lower. Tags that end up with the same name in a gallery are merged.
// (The rule is spelled out here instead of calling Db::normalizeTag so this step never changes.)
// Empty names can't happen (rescan/import never create them), so they are left alone.
bool toV5NormalizedTags(Sql &s)
{
    struct Tag { qint64 id; qint64 galleryId; QString name; };
    QList<Tag> tags;
    QSqlQuery &q = s.query();
    if (!s.exec("SELECT id, gallery_id, name FROM tags ORDER BY id"))
        return false;
    while (q.next())
        tags.append({q.value(0).toLongLong(), q.value(1).toLongLong(), q.value(2).toString()});
    q.finish();

    // Group all spellings by gallery + target name.
    QMap<QPair<qint64, QString>, QList<Tag>> groups;
    for (const Tag &tag : std::as_const(tags)) {
        const QString t = tag.name.simplified();
        const QString name = t.isEmpty() ? t : t.left(1).toUpper() + t.mid(1).toLower();
        groups[{tag.galleryId, name}].append(tag);
    }

    for (auto g = groups.constBegin(); g != groups.constEnd(); ++g) {
        const QString &name = g.key().second;
        const QList<Tag> &spellings = g.value();
        // Keep the tag that already has the right spelling, otherwise the oldest one.
        qsizetype keep = 0;
        for (qsizetype i = 0; i < spellings.size(); ++i)
            if (spellings[i].name == name)
                keep = i;
        const qint64 keeperId = spellings[keep].id;

        // Other spellings: move their links to the keeper, then drop them.
        for (qsizetype i = 0; i < spellings.size(); ++i) {
            if (i == keep)
                continue;
            q.prepare("INSERT OR IGNORE INTO image_tags(image_id, tag_id) "
                      "SELECT image_id, ? FROM image_tags WHERE tag_id = ?");
            q.addBindValue(keeperId);
            q.addBindValue(spellings[i].id);
            if (!s.exec())
                return false;
            q.prepare("DELETE FROM image_tags WHERE tag_id = ?");
            q.addBindValue(spellings[i].id);
            if (!s.exec())
                return false;
            q.prepare("DELETE FROM tags WHERE id = ?");
            q.addBindValue(spellings[i].id);
            if (!s.exec())
                return false;
        }
        // Only now the name is free for the keeper.
        if (spellings[keep].name != name) {
            q.prepare("UPDATE tags SET name = ? WHERE id = ?");
            q.addBindValue(name);
            q.addBindValue(keeperId);
            if (!s.exec())
                return false;
        }
    }
    return true;
}

// ---- v5 -> v6: virtual path --------------------------------------------------------------
// vpath = the file's tags in alphabetical order joined with '/', vpath_key = its sort form
// (see virtualpath.h). Classic sorting uses vpath_key. TEXT has no length limit in SQLite.
bool toV6VirtualPath(Sql &s)
{
    if (!s.exec("ALTER TABLE images ADD COLUMN vpath TEXT NOT NULL DEFAULT ''")
        || !s.exec("ALTER TABLE images ADD COLUMN vpath_key TEXT NOT NULL DEFAULT ''")
        || !s.exec("CREATE INDEX IF NOT EXISTS idx_images_vpath ON images(gallery_id, vpath_key)"))
        return false;

    QList<qint64> ids;
    QSqlQuery &q = s.query();
    if (!s.exec("SELECT id FROM images"))
        return false;
    while (q.next())
        ids << q.value(0).toLongLong();
    q.finish();

    QString error;
    return VirtualPath::update(s.db(), ids, &error) || s.fail(error);
}

// ---- v6 -> v7: digits after letters in virtual paths ---------------------------------------
// The alphabet of VirtualPath::sortKey changed ("Маша/Море/001"): recompute every path.
bool toV7DigitsLast(Sql &s)
{
    QList<qint64> ids;
    QSqlQuery &q = s.query();
    if (!s.exec("SELECT id FROM images"))
        return false;
    while (q.next())
        ids << q.value(0).toLongLong();
    q.finish();

    QString error;
    return VirtualPath::update(s.db(), ids, &error) || s.fail(error);
}

// ---- v7 -> v8: '_' in tags reads as a space ----------------------------------------------
// "_april", "april" and "April" become "April". Tags that end up with the same name in a gallery
// are merged; a tag of underscores only (empty name) is dropped. Virtual paths are recomputed.
// (The rule is spelled out here instead of calling Db::normalizeTag so this step never changes.)
bool toV8UnderscoreIsSpace(Sql &s)
{
    struct Tag { qint64 id; qint64 galleryId; QString name; };
    QList<Tag> tags;
    QSqlQuery &q = s.query();
    if (!s.exec("SELECT id, gallery_id, name FROM tags ORDER BY id"))
        return false;
    while (q.next())
        tags.append({q.value(0).toLongLong(), q.value(1).toLongLong(), q.value(2).toString()});
    q.finish();

    // Group all spellings by gallery + target name.
    QMap<QPair<qint64, QString>, QList<Tag>> groups;
    for (const Tag &tag : std::as_const(tags)) {
        const QString t = QString(tag.name).replace(u'_', u' ').simplified();
        const QString name = t.isEmpty() ? t : t.left(1).toUpper() + t.mid(1).toLower();
        groups[{tag.galleryId, name}].append(tag);
    }

    auto drop = [&](qint64 tagId) {
        q.prepare("DELETE FROM image_tags WHERE tag_id = ?");
        q.addBindValue(tagId);
        if (!s.exec())
            return false;
        q.prepare("DELETE FROM tags WHERE id = ?");
        q.addBindValue(tagId);
        return s.exec();
    };

    for (auto g = groups.constBegin(); g != groups.constEnd(); ++g) {
        const QString &name = g.key().second;
        const QList<Tag> &spellings = g.value();
        if (name.isEmpty()) {
            for (const Tag &tag : spellings)
                if (!drop(tag.id))
                    return false;
            continue;
        }
        // Keep the tag that already has the right spelling, otherwise the oldest one.
        qsizetype keep = 0;
        for (qsizetype i = 0; i < spellings.size(); ++i)
            if (spellings[i].name == name)
                keep = i;
        const qint64 keeperId = spellings[keep].id;

        // Other spellings: move their links to the keeper, then drop them.
        for (qsizetype i = 0; i < spellings.size(); ++i) {
            if (i == keep)
                continue;
            q.prepare("INSERT OR IGNORE INTO image_tags(image_id, tag_id) "
                      "SELECT image_id, ? FROM image_tags WHERE tag_id = ?");
            q.addBindValue(keeperId);
            q.addBindValue(spellings[i].id);
            if (!s.exec() || !drop(spellings[i].id))
                return false;
        }
        // Only now the name is free for the keeper.
        if (spellings[keep].name != name) {
            q.prepare("UPDATE tags SET name = ? WHERE id = ?");
            q.addBindValue(name);
            q.addBindValue(keeperId);
            if (!s.exec())
                return false;
        }
    }

    QList<qint64> ids;
    if (!s.exec("SELECT id FROM images"))
        return false;
    while (q.next())
        ids << q.value(0).toLongLong();
    q.finish();

    QString error;
    return VirtualPath::update(s.db(), ids, &error) || s.fail(error);
}

// ---- v8 -> v9: collections -------------------------------------------------------------------
// A collection = the files with the same virtual path. images.vpath_hash = xxh128 of the path
// (Collections::hashOf); the collections table keeps name, rating and cover. The rating moves
// from the files to their collection: the average of the files' ratings rounded up, kept in
// 1...100. Name = the last two tags of the path joined by a space; cover = the first image of the
// collection by file name. images.rating is dropped.
// (Name and cover rules are spelled out here so this step never changes.)
bool toV9Collections(Sql &s)
{
    if (!s.exec("CREATE TABLE collections ("
                " id INTEGER PRIMARY KEY AUTOINCREMENT,"
                " gallery_id INTEGER NOT NULL REFERENCES galleries(id) ON DELETE CASCADE,"
                " vpath_hash BLOB NOT NULL,"   // images.vpath_hash of its files
                " name TEXT NOT NULL DEFAULT '',"
                " rating INTEGER NOT NULL DEFAULT 50,"
                " cover_image_id INTEGER REFERENCES images(id) ON DELETE SET NULL,"
                " UNIQUE (gallery_id, vpath_hash))")
        || !s.exec("ALTER TABLE images ADD COLUMN vpath_hash BLOB"))
        return false;

    QSqlQuery &q = s.query();
    QList<QPair<qint64, QString>> paths;
    if (!s.exec("SELECT id, vpath FROM images"))
        return false;
    while (q.next())
        paths.append({q.value(0).toLongLong(), q.value(1).toString()});
    q.finish();
    q.prepare("UPDATE images SET vpath_hash = ? WHERE id = ?");
    for (const auto &[id, path] : std::as_const(paths)) {
        q.addBindValue(Collections::hashOf(path));
        q.addBindValue(id);
        if (!s.exec())
            return false;
    }
    if (!s.exec("CREATE INDEX idx_images_collection ON images(gallery_id, vpath_hash)"))
        return false;

    struct Group { qint64 galleryId; QByteArray hash; QString path; qint64 ratingSum; qint64 count; };
    QList<Group> groups;
    if (!s.exec("SELECT gallery_id, vpath_hash, MIN(vpath), SUM(rating), COUNT(*) FROM images "
                "GROUP BY gallery_id, vpath_hash"))
        return false;
    while (q.next())
        groups.append({q.value(0).toLongLong(), q.value(1).toByteArray(), q.value(2).toString(),
                       q.value(3).toLongLong(), q.value(4).toLongLong()});
    q.finish();

    QSqlQuery files(s.db());
    files.prepare("SELECT id, path FROM images WHERE gallery_id = ? AND vpath_hash = ? ORDER BY name COLLATE NOCASE, id");
    q.prepare("INSERT INTO collections(gallery_id, vpath_hash, name, rating, cover_image_id) VALUES(?, ?, ?, ?, ?)");
    for (const Group &g : std::as_const(groups)) {
        const qint64 average = (qMax(qint64(0), g.ratingSum) + g.count - 1) / g.count; // rounded up
        const int rating = int(qBound(qint64(1), average, qint64(100)));
        const QStringList tags = g.path.split(u'/', Qt::SkipEmptyParts);
        const QString name = tags.mid(qMax(qsizetype(0), tags.size() - 2)).join(u' ');

        files.addBindValue(g.galleryId);
        files.addBindValue(g.hash);
        if (!files.exec())
            return s.fail(files.lastError().text());
        QVariant cover(QMetaType::fromType<qint64>());
        while (files.next())
            if (MediaFiles::kindOf(files.value(1).toString()) == MediaFiles::Kind::Image) {
                cover = files.value(0).toLongLong();
                break;
            }
        files.finish();

        q.addBindValue(g.galleryId);
        q.addBindValue(g.hash);
        q.addBindValue(name.isNull() ? QStringLiteral("") : name);
        q.addBindValue(rating);
        q.addBindValue(cover);
        if (!s.exec())
            return false;
    }

    return s.exec("DROP INDEX IF EXISTS idx_images_order")
        && s.exec("ALTER TABLE images DROP COLUMN rating");
}

// ---- v9 -> v10: collections sorted by the name the user sees -------------------------------
// collections.name_key = VirtualPath::sortKey of the name, or of the virtual path for an empty name
// (shown that way), so names sort by the alphabet and regardless of case (NOCASE knows Latin only).
bool toV10CollectionNameKey(Sql &s)
{
    if (!s.exec("ALTER TABLE collections ADD COLUMN name_key TEXT NOT NULL DEFAULT ''"))
        return false;
    QSqlQuery &q = s.query();
    QList<QPair<qint64, QString>> keys;
    if (!s.exec("SELECT c.id, c.name, (SELECT i.vpath FROM images i WHERE i.gallery_id = c.gallery_id "
                "AND i.vpath_hash = c.vpath_hash LIMIT 1) FROM collections c"))
        return false;
    while (q.next()) {
        const QString name = q.value(1).toString();
        const QString key = VirtualPath::sortKey(name.isEmpty() ? q.value(2).toString() : name);
        keys.append({q.value(0).toLongLong(), key.isNull() ? QStringLiteral("") : key});
    }
    q.finish();
    q.prepare("UPDATE collections SET name_key = ? WHERE id = ?");
    for (const auto &[id, key] : std::as_const(keys)) {
        q.addBindValue(key);
        q.addBindValue(id);
        if (!s.exec())
            return false;
    }
    return true;
}

// ---- v10 -> v11: tag groups -----------------------------------------------------------------
// Each tag is in at most one group of its gallery (tags.group_id, NULL = no group). The user
// orders the groups (position); a deleted group leaves its tags without a group.
bool toV11TagGroups(Sql &s)
{
    return s.exec("CREATE TABLE tag_groups ("
                  " id INTEGER PRIMARY KEY AUTOINCREMENT,"
                  " gallery_id INTEGER NOT NULL REFERENCES galleries(id) ON DELETE CASCADE,"
                  " name TEXT NOT NULL,"
                  " position INTEGER NOT NULL)")
        && s.exec("ALTER TABLE tags ADD COLUMN group_id INTEGER REFERENCES tag_groups(id) ON DELETE SET NULL")
        && s.exec("CREATE INDEX idx_tags_group ON tags(group_id)");
}

// ---- v11 -> v12: download profiles ------------------------------------------------------------
// Profiles of the download manager, program-wide (no gallery). mode: 0 = page, 1 = files;
// tag: 0 = any, 1 = img, 2 = a; attributes: JSON array of {"name", "value"}.
bool toV12DownloadProfiles(Sql &s)
{
    return s.exec("CREATE TABLE download_profiles ("
                  " id INTEGER PRIMARY KEY AUTOINCREMENT,"
                  " name TEXT NOT NULL UNIQUE,"
                  " user_agent TEXT NOT NULL DEFAULT '',"
                  " mode INTEGER NOT NULL DEFAULT 1,"
                  " tag INTEGER NOT NULL DEFAULT 0,"
                  " source TEXT NOT NULL DEFAULT '',"
                  " classes TEXT NOT NULL DEFAULT '',"
                  " attributes TEXT NOT NULL DEFAULT '[]')");
}

// ---- v12 -> v13: per-gallery settings ---------------------------------------------------------
// Values remembered for one gallery (e.g. the root folder last chosen for downloads and imports);
// they go with the gallery.
bool toV13GallerySettings(Sql &s)
{
    return s.exec("CREATE TABLE gallery_settings ("
                  " gallery_id INTEGER NOT NULL REFERENCES galleries(id) ON DELETE CASCADE,"
                  " key TEXT NOT NULL,"
                  " value TEXT NOT NULL,"
                  " PRIMARY KEY (gallery_id, key))");
}

} // namespace

struct Migrations::Step
{
    bool (*apply)(Sql &);
    bool needsForeignKeysOff; // table rebuilds must not trigger cascades
};

static const QList<Migrations::Step> &steps();

int Migrations::latestVersion()
{
    return int(steps().size());
}

bool Migrations::run(QSqlDatabase &db, QString *error)
{
    QSqlQuery q(db);
    if (!q.exec("PRAGMA user_version") || !q.next()) {
        *error = q.lastError().text();
        return false;
    }
    const int version = q.value(0).toInt();
    q.finish();

    for (int v = version; v < latestVersion(); ++v)
        if (!runStep(db, steps().at(v), v + 1, error))
            return false;

    if (!q.exec("PRAGMA foreign_keys = ON")) {
        *error = q.lastError().text();
        return false;
    }
    return true;
}

bool Migrations::runStep(QSqlDatabase &db, const Step &step, int toVersion, QString *error)
{
    QSqlQuery q(db);
    // Can't be changed inside a transaction, so it is set before.
    if (!q.exec(QString("PRAGMA foreign_keys = %1").arg(step.needsForeignKeysOff ? "OFF" : "ON"))) {
        *error = q.lastError().text();
        return false;
    }
    if (!db.transaction()) {
        *error = db.lastError().text();
        return false;
    }

    bool ok;
    {
        Sql s(db, error);
        ok = step.apply(s) && s.exec(QString("PRAGMA user_version = %1").arg(toVersion));
    }
    if (ok && db.commit())
        return true;

    if (error->isEmpty())
        *error = db.lastError().text();
    *error = QString("Міграція БД до версії %1 не вдалася: %2").arg(toVersion).arg(*error);
    db.rollback();
    return false;
}

// Index i migrates from version i to i + 1. Append only.
static const QList<Migrations::Step> &steps()
{
    static const QList<Migrations::Step> list = {
        {toV1Galleries, true},
        {toV2RatingAndName, false},
        {toV3AllowUpscale, false},
        {toV4NoCommasInTags, false},
        {toV5NormalizedTags, false},
        {toV6VirtualPath, false},
        {toV7DigitsLast, false},
        {toV8UnderscoreIsSpace, false},
        {toV9Collections, false},
        {toV10CollectionNameKey, false},
        {toV11TagGroups, false},
        {toV12DownloadProfiles, false},
        {toV13GallerySettings, false},
    };
    return list;
}
