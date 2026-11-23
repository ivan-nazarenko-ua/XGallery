#include "collections.h"

#include "mediafiles.h"
#include "virtualpath.h"

#include <QHash>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QVariant>

#define XXH_INLINE_ALL
#include "third_party/xxhash/xxhash.h"

namespace {

bool fail(const QSqlQuery &q, QString *error)
{
    *error = q.lastError().text();
    return false;
}

// The cover chosen by default: the first image of the collection by file name; 0 if it has none.
bool firstImage(const QSqlDatabase &db, qint64 galleryId, const QByteArray &hash, qint64 *imageId, QString *error)
{
    QSqlQuery q(db);
    q.prepare("SELECT id, path FROM images WHERE gallery_id = ? AND vpath_hash = ? "
              "ORDER BY name COLLATE NOCASE, id");
    q.addBindValue(galleryId);
    q.addBindValue(hash);
    if (!q.exec())
        return fail(q, error);
    *imageId = 0;
    while (q.next())
        if (MediaFiles::kindOf(q.value(1).toString()) == MediaFiles::Kind::Image) {
            *imageId = q.value(0).toLongLong();
            break;
        }
    return true;
}

} // namespace

namespace Collections {

QByteArray hashOf(const QString &virtualPath)
{
    const QByteArray utf8 = virtualPath.toUtf8();
    XXH128_canonical_t canonical;
    XXH128_canonicalFromHash(&canonical, XXH3_128bits(utf8.constData(), size_t(utf8.size())));
    return QByteArray(reinterpret_cast<const char *>(canonical.digest), sizeof canonical.digest);
}

QString nameKey(const QString &name, const QString &virtualPath)
{
    const QString key = VirtualPath::sortKey(name.isEmpty() ? virtualPath : name);
    return key.isNull() ? QStringLiteral("") : key;
}

QString defaultName(const QString &virtualPath)
{
    const QStringList tags = virtualPath.split(u'/', Qt::SkipEmptyParts);
    return tags.mid(qMax(qsizetype(0), tags.size() - 2)).join(u' ');
}

bool retag(const QSqlDatabase &db, const QList<qint64> &imageIds, QList<Move> *moves, QString *error)
{
    QSqlQuery select(db), store(db);
    select.prepare("SELECT vpath, vpath_hash FROM images WHERE id = ?");
    store.prepare("UPDATE images SET vpath_hash = ? WHERE id = ?");

    QList<QByteArray> before;
    before.reserve(imageIds.size());
    for (qint64 id : imageIds) {
        select.addBindValue(id);
        if (!select.exec())
            return fail(select, error);
        before << (select.next() ? select.value(1).toByteArray() : QByteArray());
        select.finish();
    }

    if (!VirtualPath::update(db, imageIds, error))
        return false;

    for (qsizetype i = 0; i < imageIds.size(); ++i) {
        select.addBindValue(imageIds[i]);
        if (!select.exec())
            return fail(select, error);
        if (!select.next())
            continue; // no such record
        const QByteArray hash = hashOf(select.value(0).toString());
        select.finish();
        if (hash == before[i])
            continue;
        store.addBindValue(hash);
        store.addBindValue(imageIds[i]);
        if (!store.exec())
            return fail(store, error);
        moves->append({before[i], hash});
    }
    return true;
}

bool sync(const QSqlDatabase &db, qint64 galleryId, const QList<Move> &moves, QString *error)
{
    QSqlQuery q(db);

    // Collections that have no files any more.
    QList<QPair<qint64, QByteArray>> vanished;
    q.prepare("SELECT c.id, c.vpath_hash FROM collections c WHERE c.gallery_id = ? AND NOT EXISTS "
              "(SELECT 1 FROM images i WHERE i.gallery_id = c.gallery_id AND i.vpath_hash = c.vpath_hash)");
    q.addBindValue(galleryId);
    if (!q.exec())
        return fail(q, error);
    while (q.next())
        vanished.append({q.value(0).toLongLong(), q.value(1).toByteArray()});
    q.finish();

    // Tag sets of files that have no collection yet (hash -> virtual path).
    QHash<QByteArray, QString> orphans;
    q.prepare("SELECT i.vpath_hash, MIN(i.vpath) FROM images i WHERE i.gallery_id = ? AND i.vpath_hash IS NOT NULL "
              "AND NOT EXISTS (SELECT 1 FROM collections c WHERE c.gallery_id = i.gallery_id "
              "AND c.vpath_hash = i.vpath_hash) GROUP BY i.vpath_hash");
    q.addBindValue(galleryId);
    if (!q.exec())
        return fail(q, error);
    while (q.next())
        orphans.insert(q.value(0).toByteArray(), q.value(1).toString());
    q.finish();

    // A vanished collection moves to where most of its files went, if nobody is there yet;
    // otherwise it is deleted (an existing collection keeps its own properties).
    for (const auto &[collectionId, hash] : std::as_const(vanished)) {
        QHash<QByteArray, int> targets;
        for (const Move &move : moves)
            if (move.from == hash && orphans.contains(move.to))
                ++targets[move.to];
        QByteArray best;
        int bestCount = 0;
        for (auto t = targets.constBegin(); t != targets.constEnd(); ++t)
            if (t.value() > bestCount || (t.value() == bestCount && t.key() < best)) {
                best = t.key();
                bestCount = t.value();
            }

        if (bestCount > 0) {
            // Without a name of its own the collection is shown by its path, which just changed.
            q.prepare("UPDATE collections SET vpath_hash = ?, "
                      "name_key = CASE WHEN name = '' THEN ? ELSE name_key END WHERE id = ?");
            q.addBindValue(best);
            q.addBindValue(nameKey(QString(), orphans.value(best)));
            q.addBindValue(collectionId);
            orphans.remove(best);
        } else {
            q.prepare("DELETE FROM collections WHERE id = ?");
            q.addBindValue(collectionId);
        }
        if (!q.exec())
            return fail(q, error);
    }

    // New collections: default properties.
    q.prepare("INSERT INTO collections(gallery_id, vpath_hash, name, name_key, rating) VALUES(?, ?, ?, ?, ?)");
    for (auto o = orphans.constBegin(); o != orphans.constEnd(); ++o) {
        const QString name = defaultName(o.value());
        q.addBindValue(galleryId);
        q.addBindValue(o.key());
        q.addBindValue(name.isNull() ? QStringLiteral("") : name); // a null QString would be NULL
        q.addBindValue(nameKey(name, o.value()));
        q.addBindValue(DefaultRating);
        if (!q.exec())
            return fail(q, error);
    }

    // Covers: none yet (or a collection of videos only), or the cover left the collection.
    QList<QPair<qint64, QByteArray>> withoutCover;
    q.prepare("SELECT c.id, c.vpath_hash FROM collections c WHERE c.gallery_id = ? AND (c.cover_image_id IS NULL "
              "OR NOT EXISTS (SELECT 1 FROM images i WHERE i.id = c.cover_image_id "
              "AND i.gallery_id = c.gallery_id AND i.vpath_hash = c.vpath_hash))");
    q.addBindValue(galleryId);
    if (!q.exec())
        return fail(q, error);
    while (q.next())
        withoutCover.append({q.value(0).toLongLong(), q.value(1).toByteArray()});
    q.finish();

    QSqlQuery store(db);
    store.prepare("UPDATE collections SET cover_image_id = ? WHERE id = ?");
    for (const auto &[collectionId, hash] : std::as_const(withoutCover)) {
        qint64 cover = 0;
        if (!firstImage(db, galleryId, hash, &cover, error))
            return false;
        store.addBindValue(cover ? QVariant(cover) : QVariant(QMetaType::fromType<qint64>()));
        store.addBindValue(collectionId);
        if (!store.exec())
            return fail(store, error);
    }
    return true;
}

bool update(const QSqlDatabase &db, qint64 galleryId, const QList<qint64> &imageIds, QString *error)
{
    QList<Move> moves;
    return retag(db, imageIds, &moves, error) && sync(db, galleryId, moves, error);
}

} // namespace Collections
