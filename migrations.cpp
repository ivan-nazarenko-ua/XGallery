#include "migrations.h"

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
    };
    return list;
}
