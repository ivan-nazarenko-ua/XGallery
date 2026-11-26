#include "foldersync.h"

#include "collections.h"
#include "database.h"
#include "thumbcache.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>
#include <QTimeZone>

#include <algorithm>
#include <functional>
#include <optional>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace FolderSync {

bool Comparison::differs() const
{
    return !moved.isEmpty() || !changed.isEmpty() || !onlyInGallery.isEmpty() || !onlyInFolder.isEmpty();
}

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("FolderSync", text);
}

constexpr qint64 ChunkSize = 1 << 20;
constexpr qint64 PercentFrom = 16 << 20; // files at least this big show how far the current one is
constexpr qint64 StepWeight = 1 << 20;   // a delete or a move counts like a megabyte copied
// The job's own temporary files: never compared, never imported.
const QLatin1String PartSuffix(".xgsync-part");
const QLatin1String AsideSuffix(".xgsync-tmp");

// Windows paths: "Море" and "море" are one folder.
QString pathKey(const QString &path)
{
    return path.toCaseFolded();
}

QString contentKey(const QString &sha256, qint64 size)
{
    return sha256 + ':' + QString::number(size);
}

QString parentOf(const QString &relative)
{
    const qsizetype slash = relative.lastIndexOf('/');
    return slash < 0 ? QString() : relative.left(slash);
}

QString nameOf(const QString &relative)
{
    return relative.mid(relative.lastIndexOf('/') + 1);
}

QString joined(const QString &dir, const QString &name)
{
    return dir.isEmpty() ? name : dir + '/' + name;
}

QString shown(const QString &relative)
{
    return QDir::toNativeSeparators(relative);
}

// Waits while paused; true = stop.
bool stopRequested(Control *control)
{
    while (control->pause && !control->stop)
        QThread::msleep(50);
    return control->stop;
}

// A job's progress in bytes (or byte-like units) in the int range of a progress bar.
class Progress
{
public:
    template<typename T>
    explicit Progress(QPromise<T> &promise)
        : m_setRange([&promise](int minimum, int maximum) { promise.setProgressRange(minimum, maximum); })
        , m_report([&promise](int value, const QString &text) { promise.setProgressValueAndText(value, text); })
    {}

    void indeterminate(const QString &text)
    {
        m_setRange(0, 0);
        m_report(0, text);
    }
    void begin(qint64 total)
    {
        m_total = std::max<qint64>(total, 1);
        m_scale = std::max<qint64>(1, m_total / 1000000);
        m_done = 0;
        m_setRange(0, int(m_total / m_scale));
    }
    void add(qint64 units) { m_done += units; }
    void show(const QString &text) { m_report(int(std::min(m_done, m_total) / m_scale), text); }

private:
    std::function<void(int, int)> m_setRange;
    std::function<void(int, const QString &)> m_report;
    qint64 m_total = 1;
    qint64 m_scale = 1;
    qint64 m_done = 0;
};

QString withPercent(const QString &text, qint64 done, qint64 size)
{
    return size >= PercentFrom ? text + QStringLiteral(" (%1 %)").arg(done * 100 / std::max<qint64>(size, 1)) : text;
}

// sha256 read chunk by chunk (progress; a stop works in the middle of a big file). Empty on a read error or a stop.
QString hashFile(const QString &path, Progress &progress, Control *control, const QString &text, bool *stopped)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    const qint64 size = file.size();
    QByteArray buffer(ChunkSize, Qt::Uninitialized);
    qint64 read = 0;
    for (;;) {
        if (stopRequested(control)) {
            *stopped = true;
            return {};
        }
        const qint64 n = file.read(buffer.data(), ChunkSize);
        if (n < 0)
            return {};
        if (n == 0)
            break;
        hash.addData(QByteArrayView(buffer.constData(), n));
        read += n;
        progress.add(n);
        progress.show(withPercent(text, read, size));
    }
    return QString::fromLatin1(hash.result().toHex());
}

bool isOwnTemporary(const QString &path)
{
    return path.endsWith(PartSuffix, Qt::CaseInsensitive) || path.contains(AsideSuffix, Qt::CaseInsensitive);
}

Comparison doCompare(QPromise<Comparison> &promise, const QSqlDatabase &db, qint64 galleryId, const QString &folder,
                     Mode mode, Control *control)
{
    Comparison result;
    Progress progress(promise);

    QList<File> gallery;
    {
        QSqlQuery q(db);
        q.prepare("SELECT id, path, size, sha256 FROM images WHERE gallery_id = ? ORDER BY path");
        q.addBindValue(galleryId);
        if (!q.exec()) {
            result.error = q.lastError().text();
            return result;
        }
        while (q.next())
            gallery.append({q.value(1).toString(), q.value(2).toLongLong(), q.value(3).toString(),
                            q.value(0).toLongLong()});
    }

    progress.indeterminate(tr("Читаю папку…"));
    QList<File> files;
    const QDir base(folder);
    const bool complete = Scanner::forEachFileInfo(folder, false, [&](const QFileInfo &info) {
        const QString path = info.absoluteFilePath();
        if (!Scanner::isIgnoredFile(path) && !isOwnTemporary(path)) {
            files.append({base.relativeFilePath(path), info.size()});
            if (files.size() % 200 == 0)
                progress.indeterminate(tr("Читаю папку… знайдено файлів: %1").arg(files.size()));
        }
        return !stopRequested(control);
    });
    if (!complete) {
        result.stopped = true;
        return result;
    }
    std::sort(files.begin(), files.end(), [](const File &a, const File &b) {
        return QString::compare(a.path, b.path, Qt::CaseInsensitive) < 0;
    });

    // The gallery file at the same path (case ignored) as each folder file.
    QHash<QString, int> galleryAt;
    for (int g = 0; g < gallery.size(); ++g)
        if (!galleryAt.contains(pathKey(gallery[g].path)))
            galleryAt.insert(pathKey(gallery[g].path), g);
    QList<int> pathMate(files.size(), -1);
    for (int f = 0; f < files.size(); ++f)
        pathMate[f] = galleryAt.value(pathKey(files[f].path), -1);
    QList<bool> galleryDone(gallery.size(), false);
    QList<bool> folderDone(files.size(), false);

    if (mode == Mode::Quick) {
        for (int f = 0; f < files.size(); ++f) {
            const int g = pathMate[f];
            if (g >= 0 && !galleryDone[g] && gallery[g].size == files[f].size) {
                galleryDone[g] = folderDone[f] = true;
                ++result.synced;
            }
        }
    }

    // Only a size some open gallery file has can be the same content.
    QSet<qint64> openSizes;
    for (int g = 0; g < gallery.size(); ++g)
        if (!galleryDone[g])
            openSizes.insert(gallery[g].size);
    QList<int> toRead;
    qint64 bytes = 0;
    for (int f = 0; f < files.size(); ++f)
        if (!folderDone[f] && openSizes.contains(files[f].size)) {
            toRead << f;
            bytes += files[f].size;
        }
    progress.begin(bytes);
    for (int i = 0; i < toRead.size(); ++i) {
        File &file = files[toRead[i]];
        const QString text = tr("Перевіряю вміст: %1 з %2\n%3")
                                 .arg(QString::number(i + 1), QString::number(toRead.size()), shown(file.path));
        progress.show(text);
        bool stopped = false;
        file.sha256 = hashFile(base.filePath(file.path), progress, control, text, &stopped);
        if (stopped) {
            result.stopped = true;
            return result;
        }
        if (file.sha256.isEmpty()) {
            // Unknown content: this file and the gallery file at its path stay as they are.
            result.unreadable << file.path;
            folderDone[toRead[i]] = true;
            if (pathMate[toRead[i]] >= 0)
                galleryDone[pathMate[toRead[i]]] = true;
        }
    }

    // Same path, same content.
    for (int f = 0; f < files.size(); ++f) {
        const int g = pathMate[f];
        if (!folderDone[f] && g >= 0 && !galleryDone[g] && !files[f].sha256.isEmpty()
            && files[f].sha256 == gallery[g].sha256 && files[f].size == gallery[g].size) {
            galleryDone[g] = folderDone[f] = true;
            ++result.synced;
        }
    }

    // Same content at another path: moved. A file with the same name is the likelier partner.
    QHash<QString, QList<int>> openByContent;
    for (int g = 0; g < gallery.size(); ++g)
        if (!galleryDone[g])
            openByContent[contentKey(gallery[g].sha256, gallery[g].size)] << g;
    for (int f = 0; f < files.size(); ++f) {
        if (folderDone[f] || files[f].sha256.isEmpty())
            continue;
        const auto it = openByContent.find(contentKey(files[f].sha256, files[f].size));
        if (it == openByContent.end() || it->isEmpty())
            continue;
        const QString name = nameOf(files[f].path);
        qsizetype pick = 0;
        for (qsizetype i = 0; i < it->size(); ++i)
            if (nameOf(gallery[it->at(i)].path).compare(name, Qt::CaseInsensitive) == 0) {
                pick = i;
                break;
            }
        const int g = it->takeAt(pick);
        galleryDone[g] = folderDone[f] = true;
        result.moved.append({gallery[g], files[f]});
    }

    // Both sides still have a file at the same path: changed.
    for (int f = 0; f < files.size(); ++f) {
        const int g = pathMate[f];
        if (!folderDone[f] && g >= 0 && !galleryDone[g]) {
            galleryDone[g] = folderDone[f] = true;
            result.changed.append({gallery[g], files[f]});
        }
    }

    for (int g = 0; g < gallery.size(); ++g)
        if (!galleryDone[g])
            result.onlyInGallery << gallery[g];
    for (int f = 0; f < files.size(); ++f)
        if (!folderDone[f])
            result.onlyInFolder << files[f];
    return result;
}

// --- Applying -----------------------------------------------------------------------------

struct DeleteStep
{
    QString path; // relative to the target root
    qint64 imageId = 0;
    QString sha256;
};

struct MoveStep
{
    QString from; // relative to the target root
    QString to;
    qint64 imageId = 0;
};

struct CopyStep
{
    QString source; // relative to the source root
    QString target; // relative to the target root
    qint64 size = 0;
    bool replace = false; // the target has an old version, deleted once the copy is ready
    qint64 imageId = 0;   // replace in the gallery: the record
    QString oldSha256;
    bool numberOnClash = false; // the name is taken: "name (1).ext" (merge)
};

struct Plan
{
    QString sourceRoot;
    QString targetRoot;
    QList<DeleteStep> deletes;
    QList<MoveStep> moves;
    QList<CopyStep> copies;
};

// The gallery side's records, in one transaction.
class GalleryWriter
{
public:
    GalleryWriter(const QSqlDatabase &db, qint64 galleryId) : m_db(db), m_galleryId(galleryId) {}

    bool begin(QString *error)
    {
        if (m_db.transaction())
            return true;
        *error = m_db.lastError().text();
        return false;
    }
    bool deleted(qint64 imageId, const QString &sha256, QString *error)
    {
        QSqlQuery q(m_db);
        q.prepare("DELETE FROM images WHERE id = ? AND gallery_id = ?"); // image_tags: ON DELETE CASCADE
        q.addBindValue(imageId);
        q.addBindValue(m_galleryId);
        if (!q.exec()) {
            *error = q.lastError().text();
            return false;
        }
        m_goneContent << sha256;
        return true;
    }
    // Paths are unique per gallery and moves may swap them: the record gets a placeholder path now and its
    // new one in finish(), after every move.
    bool moved(qint64 imageId, const QString &oldPath, const QString &newPath, qint64 mtime, QString *error)
    {
        QSqlQuery q(m_db);
        q.prepare("UPDATE images SET path = ? WHERE id = ? AND gallery_id = ?");
        q.addBindValue(QStringLiteral("\x01sync-move-%1").arg(imageId)); // never a real relative path
        q.addBindValue(imageId);
        q.addBindValue(m_galleryId);
        if (!q.exec()) {
            *error = q.lastError().text();
            return false;
        }
        m_moves.append({imageId, oldPath, newPath, mtime});
        return true;
    }
    bool replaced(qint64 imageId, qint64 size, qint64 mtime, const QString &sha256, const QString &oldSha256,
                  QString *error)
    {
        QSqlQuery q(m_db);
        q.prepare("UPDATE images SET size = ?, mtime = ?, sha256 = ? WHERE id = ? AND gallery_id = ?");
        q.addBindValue(size);
        q.addBindValue(mtime);
        q.addBindValue(sha256);
        q.addBindValue(imageId);
        q.addBindValue(m_galleryId);
        if (!q.exec()) {
            *error = q.lastError().text();
            return false;
        }
        m_goneContent << oldSha256;
        return true;
    }
    // Recorded in finish(): the path may still be a moved record's old one until then.
    void added(const QString &path, qint64 size, qint64 mtime, const QString &sha256)
    {
        m_adds.append({path, size, mtime, sha256});
    }
    bool finish(QString *error)
    {
        bool ok = true;
        for (const MovedRecord &move : std::as_const(m_moves)) {
            if (!(ok = Scanner::moveRecord(m_db, m_galleryId, move.imageId, move.oldPath, move.newPath, move.mtime,
                                           error)))
                break;
            m_retagged << move.imageId;
        }
        for (qsizetype i = 0; ok && i < m_adds.size(); ++i) {
            const AddedFile &add = m_adds.at(i);
            const qint64 id = Scanner::addRecord(m_db, m_galleryId, add.path, add.size, add.mtime, add.sha256,
                                                 m_addedAt, error);
            if (!(ok = id != 0))
                break;
            m_retagged << id;
        }
        QList<Collections::Move> moves;
        if (!ok || !Collections::retag(m_db, m_retagged, &moves, error) || !Db::deleteUnusedTags(m_db, m_galleryId, error)
            || !Collections::sync(m_db, m_galleryId, moves, error)) {
            m_db.rollback();
            return false;
        }
        if (!m_db.commit()) {
            *error = m_db.lastError().text();
            m_db.rollback();
            return false;
        }
        ThumbCache::removeUnused(m_db, m_goneContent);
        return true;
    }
    void abort() { m_db.rollback(); }

private:
    struct MovedRecord
    {
        qint64 imageId;
        QString oldPath;
        QString newPath;
        qint64 mtime;
    };
    struct AddedFile
    {
        QString path;
        qint64 size;
        qint64 mtime;
        QString sha256;
    };

    QSqlDatabase m_db;
    qint64 m_galleryId;
    QList<MovedRecord> m_moves;
    QList<AddedFile> m_adds;
    // One timestamp for the whole run, so "newest first" keeps a batch together.
    const qint64 m_addedAt = QDateTime::currentSecsSinceEpoch();
    QList<qint64> m_retagged;
    QStringList m_goneContent;
};

// Folders of the target as they are spelled on disk ("море" finds an existing "Море"), created when missing.
class Folders
{
public:
    explicit Folders(const QString &root) : m_root(root) {}

    // The relative folder's real spelling; nullopt if it can't be created.
    std::optional<QString> make(const QString &relative)
    {
        QString current;
        for (const QString &segment : relative.split('/', Qt::SkipEmptyParts)) {
            const QString wanted = joined(current, segment);
            const auto cached = m_known.constFind(pathKey(wanted));
            if (cached != m_known.constEnd()) {
                current = *cached;
                continue;
            }
            const QDir parent(m_root.filePath(current.isEmpty() ? "." : current));
            QString real;
            for (const QString &name :
                 parent.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System))
                if (name.compare(segment, Qt::CaseInsensitive) == 0) {
                    real = name;
                    break;
                }
            if (real.isEmpty()) {
                if (!parent.mkdir(segment))
                    return std::nullopt;
                real = segment;
            }
            m_known.insert(pathKey(wanted), joined(current, real));
            current = joined(current, real);
        }
        return current;
    }

private:
    QDir m_root;
    QHash<QString, QString> m_known; // pathKey(relative as asked) -> real relative path
};

enum class Removal { Gone, DeletedPermanently, Left, Stop };

// Deletes one file like the program deletes user files: Recycle Bin, or permanently only with the user's yes.
Removal removeFile(const QString &path, const Scanner::TrashQuestion &ask, bool *allPermanently, QString *error)
{
    if (!QFileInfo::exists(path))
        return Removal::Gone;
    Scanner::RecycleOutcome outcome = Scanner::recycleFile(path, error);
    if (outcome == Scanner::RecycleOutcome::NotRecyclable) {
        const Scanner::TrashChoice choice = *allPermanently
                                                ? Scanner::TrashChoice::DeleteAllPermanently
                                                : ask(path, Scanner::TrashProblem::NotRecyclable, QString());
        if (choice == Scanner::TrashChoice::DeleteAllPermanently)
            *allPermanently = true;
        if (choice != Scanner::TrashChoice::DeletePermanently && choice != Scanner::TrashChoice::DeleteAllPermanently)
            return Removal::Stop;
        QFile file(path);
        if (file.remove())
            return Removal::DeletedPermanently;
        outcome = Scanner::RecycleOutcome::Failed;
        *error = file.errorString();
    }
    if (outcome == Scanner::RecycleOutcome::Failed)
        return ask(path, Scanner::TrashProblem::Failed, *error) == Scanner::TrashChoice::Continue ? Removal::Left
                                                                                                   : Removal::Stop;
    return Removal::Gone;
}

enum class CopyOutcome { Done, Failed, Stopped };

// Copies source into the new file `part` chunk by chunk (progress; a stop removes the part), keeping the
// source's modification time. *sha256: the content's hash, when asked for.
CopyOutcome copyToPart(const QString &source, const QString &part, Progress &progress, Control *control,
                       const QString &text, QString *sha256, QString *error)
{
    QFile in(source);
    if (!in.open(QIODevice::ReadOnly)) {
        *error = in.errorString();
        return CopyOutcome::Failed;
    }
    QFile out(part);
    if (!out.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        *error = out.errorString();
        return CopyOutcome::Failed;
    }
    const auto discard = [&out] {
        out.close();
        out.remove();
    };
    QCryptographicHash hash(QCryptographicHash::Sha256);
    const qint64 size = in.size();
    QByteArray buffer(ChunkSize, Qt::Uninitialized);
    qint64 copied = 0;
    for (;;) {
        if (stopRequested(control)) {
            discard();
            return CopyOutcome::Stopped;
        }
        const qint64 n = in.read(buffer.data(), ChunkSize);
        if (n < 0) {
            *error = in.errorString();
            discard();
            return CopyOutcome::Failed;
        }
        if (n == 0)
            break;
        if (out.write(buffer.constData(), n) != n) {
            *error = out.errorString();
            discard();
            return CopyOutcome::Failed;
        }
        if (sha256)
            hash.addData(QByteArrayView(buffer.constData(), n));
        copied += n;
        progress.add(n);
        progress.show(withPercent(text, copied, size));
    }
    // Flushed first: a later write would set the time again.
    if (!out.flush()) {
        *error = out.errorString();
        discard();
        return CopyOutcome::Failed;
    }
    out.setFileTime(QFileInfo(source).lastModified(QTimeZone::UTC), QFileDevice::FileModificationTime);
    out.close();
    if (out.error() != QFileDevice::NoError) {
        *error = out.errorString();
        out.remove();
        return CopyOutcome::Failed;
    }
    if (sha256)
        *sha256 = QString::fromLatin1(hash.result().toHex());
    return CopyOutcome::Done;
}

// "name<suffix>", "name<suffix>1", ... : a name no file in dir has.
QString freeName(const QDir &dir, const QString &name, const QString &suffix)
{
    QString candidate = name + suffix;
    for (int n = 1; dir.exists(candidate); ++n)
        candidate = name + suffix + QString::number(n);
    return candidate;
}

qint64 mtimeOf(const QString &path)
{
    return QFileInfo(path).lastModified(QTimeZone::UTC).toMSecsSinceEpoch();
}

// "name.ext" -> "name (1).ext", "name (2).ext", ... like Windows: the first name no file in dir has.
QString numberedName(const QDir &dir, const QString &fileName)
{
    if (!dir.exists(fileName))
        return fileName;
    const QFileInfo info(fileName);
    const QString base = info.completeBaseName();
    const QString suffix = info.suffix().isEmpty() ? QString() : '.' + info.suffix();
    for (int n = 1;; ++n) {
        // One arg() call: chained ones would treat a "%1" in the file name as a placeholder.
        const QString candidate = QStringLiteral("%1 (%2)%3").arg(base, QString::number(n), suffix);
        if (!dir.exists(candidate))
            return candidate;
    }
}

// The progress units a plan needs.
qint64 workOf(const Plan &plan)
{
    qint64 total = qint64(plan.deletes.size() + plan.moves.size()) * StepWeight;
    for (const CopyStep &copy : plan.copies)
        total += copy.size;
    return total;
}

// Carries the plan out into `result` (counts added to it). The caller begins the progress and, for the
// gallery side, finishes the writer (finishWriter).
void run(const Plan &plan, Progress &progress, Control *control, const Scanner::TrashQuestion &ask,
         GalleryWriter *writer, ApplyResult &result, QString &dbError)
{
    const QDir source(plan.sourceRoot);
    const QDir target(plan.targetRoot);
    Folders folders(plan.targetRoot);
    QSet<QString> touched; // relative folders that lost a file: maybe empty now
    bool allPermanently = false;

    const auto fail = [&result](const QString &relative, const QString &reason) {
        result.failed << shown(relative) + QStringLiteral(" — ") + reason;
    };

    // 1. Files the source doesn't have: their places may be needed below.
    for (int i = 0; i < plan.deletes.size() && dbError.isEmpty(); ++i) {
        if (stopRequested(control)) {
            result.stopped = true;
            break;
        }
        const DeleteStep &step = plan.deletes.at(i);
        progress.show(tr("Видаляю %1 з %2\n%3")
                          .arg(QString::number(i + 1), QString::number(plan.deletes.size()), shown(step.path)));
        QString error;
        const Removal removal = removeFile(target.filePath(step.path), ask, &allPermanently, &error);
        progress.add(StepWeight);
        if (removal == Removal::Gone || removal == Removal::DeletedPermanently) {
            ++(removal == Removal::Gone ? result.deleted : result.deletedPermanently);
            touched.insert(parentOf(step.path));
            if (writer) {
                if (writer->deleted(step.imageId, step.sha256, &dbError))
                    result.removedImageIds << step.imageId;
            }
            continue;
        }
        if (!error.isEmpty())
            fail(step.path, error);
        if (removal == Removal::Stop) {
            result.stopped = true;
            break;
        }
    }

    // 2. Moves. A target still taken by a file that moves away itself (a swap, a cycle): that file steps aside
    //    under a temporary name first.
    struct Pending
    {
        QString from;
        QString original;
        QString to;
        qint64 imageId;
    };
    QList<Pending> pending;
    for (const MoveStep &move : plan.moves)
        pending.append({move.from, move.from, move.to, move.imageId});
    const auto targetOf = [&folders](const Pending &p) -> std::optional<QString> {
        const auto dir = folders.make(parentOf(p.to));
        return dir ? std::optional<QString>(joined(*dir, nameOf(p.to))) : std::nullopt;
    };
    int movesDone = 0;
    while (!pending.isEmpty() && !result.stopped && dbError.isEmpty()) {
        bool progressed = false;
        for (qsizetype i = 0; i < pending.size() && dbError.isEmpty();) {
            if (stopRequested(control)) {
                result.stopped = true;
                break;
            }
            const Pending p = pending.at(i);
            const auto to = targetOf(p);
            if (to && QFileInfo::exists(target.filePath(*to))) {
                ++i; // still taken
                continue;
            }
            pending.removeAt(i);
            progressed = true;
            ++movesDone;
            progress.add(StepWeight);
            progress.show(tr("Переміщую %1 з %2\n%3")
                              .arg(QString::number(movesDone), QString::number(plan.moves.size()), shown(p.original)));
            if (!to) {
                fail(p.original, tr("не вдалося створити папку для %1").arg(shown(p.to)));
                continue;
            }
            QFile file(target.filePath(p.from));
            if (!file.rename(target.filePath(*to))) {
                fail(p.original, file.errorString());
                continue;
            }
            ++result.moved;
            touched.insert(parentOf(p.original));
            if (writer)
                writer->moved(p.imageId, p.original, *to, mtimeOf(target.filePath(*to)), &dbError);
        }
        if (result.stopped || progressed || !dbError.isEmpty())
            continue;
        // Every move left is blocked.
        QHash<QString, qsizetype> sourceAt;
        for (qsizetype j = 0; j < pending.size(); ++j)
            sourceAt.insert(pathKey(pending.at(j).from), j);
        bool unblocked = false;
        for (const Pending &p : std::as_const(pending)) {
            const auto to = targetOf(p);
            const qsizetype j = to ? sourceAt.value(pathKey(*to), -1) : -1;
            if (j < 0)
                continue;
            Pending &blocker = pending[j];
            const QDir dir(target.filePath(parentOf(blocker.from).isEmpty() ? "." : parentOf(blocker.from)));
            const QString aside = joined(parentOf(blocker.from), freeName(dir, nameOf(blocker.from), AsideSuffix));
            if (QFile::rename(target.filePath(blocker.from), target.filePath(aside))) {
                blocker.from = aside;
                unblocked = true;
                break;
            }
        }
        if (!unblocked) {
            for (const Pending &p : std::as_const(pending)) {
                fail(p.original, tr("на місці вже є інший файл"));
                progress.add(StepWeight);
            }
            pending.clear();
        }
    }
    // Stopped: files that stepped aside get their names back.
    for (const Pending &p : std::as_const(pending))
        if (p.from != p.original)
            QFile::rename(target.filePath(p.from), target.filePath(p.original));

    // 3. Copies, old versions replaced only once the new one is complete.
    for (int i = 0; i < plan.copies.size() && !result.stopped && dbError.isEmpty(); ++i) {
        if (stopRequested(control)) {
            result.stopped = true;
            break;
        }
        const CopyStep &step = plan.copies.at(i);
        const QString text = tr("Копіюю %1 з %2\n%3")
                                 .arg(QString::number(i + 1), QString::number(plan.copies.size()), shown(step.target));
        progress.show(text);
        const auto dir = folders.make(parentOf(step.target));
        if (!dir) {
            fail(step.target, tr("не вдалося створити папку"));
            progress.add(step.size);
            continue;
        }
        const QDir partDir(target.filePath(dir->isEmpty() ? "." : *dir));
        QString relative = joined(*dir, nameOf(step.target));
        if (step.numberOnClash)
            relative = joined(*dir, numberedName(partDir, nameOf(step.target)));
        const QString destination = target.filePath(relative);
        if (!step.replace && QFileInfo::exists(destination)) {
            fail(step.target, tr("на місці вже є інший файл"));
            progress.add(step.size);
            continue;
        }
        const QString part = partDir.filePath(freeName(partDir, nameOf(relative), PartSuffix));
        QString sha256;
        QString error;
        const CopyOutcome outcome = copyToPart(source.filePath(step.source), part, progress, control, text,
                                               writer ? &sha256 : nullptr, &error);
        if (outcome == CopyOutcome::Stopped) {
            result.stopped = true;
            break;
        }
        if (outcome == CopyOutcome::Failed) {
            fail(step.target, error);
            continue;
        }
        if (step.replace) {
            const Removal removal = removeFile(destination, ask, &allPermanently, &error);
            if (removal == Removal::Left || removal == Removal::Stop) {
                QFile::remove(part);
                if (!error.isEmpty())
                    fail(step.target, error);
                if (removal == Removal::Stop) {
                    result.stopped = true;
                    break;
                }
                continue;
            }
            if (removal == Removal::DeletedPermanently)
                ++result.deletedPermanently;
        }
        QFile file(part);
        if (!file.rename(destination)) {
            fail(step.target, file.errorString());
            QFile::remove(part);
            continue;
        }
        ++(step.replace ? result.replaced : writer ? result.added : result.copied);
        if (nameOf(relative) != nameOf(step.target))
            ++result.renamed;
        if (writer) {
            const QFileInfo info(destination);
            const qint64 mtime = info.lastModified(QTimeZone::UTC).toMSecsSinceEpoch();
            if (step.replace)
                writer->replaced(step.imageId, info.size(), mtime, sha256, step.oldSha256, &dbError);
            else
                writer->added(relative, info.size(), mtime, sha256);
        }
    }

    // 4. Folders the job emptied, deepest first. Only a courtesy: whatever Windows refuses stays.
    QStringList emptied(touched.cbegin(), touched.cend());
    std::sort(emptied.begin(), emptied.end(), [](const QString &a, const QString &b) {
        return a.count('/') > b.count('/');
    });
    QSet<QString> removed;
    for (QString dir : std::as_const(emptied)) {
        while (!dir.isEmpty() && !removed.contains(pathKey(dir))) {
            const QString absolute = target.filePath(dir);
            if (!Scanner::isRealFolder(QFileInfo(absolute)) || !QDir().rmdir(absolute))
                break;
            removed.insert(pathKey(dir));
            ++result.removedFolders;
            dir = parentOf(dir);
        }
    }
}

// Commits what the gallery side did (also when stopped), or rolls back after a DB error.
void finishWriter(GalleryWriter &writer, QString &dbError, ApplyResult &result)
{
    if (dbError.isEmpty())
        writer.finish(&dbError);
    else
        writer.abort();
    result.error = dbError;
}

// COM for the Recycle Bin, once for the whole job (a pool thread).
class ComScope
{
public:
#ifdef Q_OS_WIN
    ComScope() : m_init(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)) {}
    ~ComScope()
    {
        if (SUCCEEDED(m_init))
            CoUninitialize();
    }

private:
    HRESULT m_init;
#endif
};

} // namespace

void compare(QPromise<Comparison> &promise, qint64 galleryId, const QString &folder, Mode mode, Control *control)
{
    const QString connection = QStringLiteral("xgallery_sync_compare_%1").arg(quintptr(QThread::currentThreadId()));
    Comparison result;
    {
        QSqlDatabase db = Db::open(connection);
        if (!db.isOpen())
            result.error = Db::lastError();
        else
            result = doCompare(promise, db, galleryId, folder, mode, control);
    }
    Db::close(connection);
    promise.addResult(result);
}

void applyToFolder(QPromise<ApplyResult> &promise, const QString &rootFolder, const QString &folder,
                   const Comparison &comparison, Control *control, const Scanner::TrashQuestion &ask)
{
    const ComScope com;
    Plan plan{rootFolder, folder, {}, {}, {}};
    for (const File &file : comparison.onlyInFolder)
        plan.deletes.append({file.path});
    for (const Pair &pair : comparison.moved)
        plan.moves.append({pair.folder.path, pair.gallery.path});
    for (const File &file : comparison.onlyInGallery)
        plan.copies.append({file.path, file.path, file.size});
    for (const Pair &pair : comparison.changed)
        plan.copies.append({pair.gallery.path, pair.folder.path, pair.gallery.size, true});
    ApplyResult result;
    QString dbError;
    Progress progress(promise);
    progress.begin(workOf(plan));
    run(plan, progress, control, ask, nullptr, result, dbError);
    promise.addResult(result);
}

void applyToGallery(QPromise<ApplyResult> &promise, qint64 galleryId, const QString &rootFolder,
                    const QString &folder, const Comparison &comparison, Control *control,
                    const Scanner::TrashQuestion &ask)
{
    const ComScope com;
    const QString connection = QStringLiteral("xgallery_sync_apply_%1").arg(quintptr(QThread::currentThreadId()));
    ApplyResult result;
    {
        QSqlDatabase db = Db::open(connection);
        GalleryWriter writer(db, galleryId);
        if (!db.isOpen()) {
            result.error = Db::lastError();
        } else if (writer.begin(&result.error)) {
            Plan plan{folder, rootFolder, {}, {}, {}};
            for (const File &file : comparison.onlyInGallery)
                plan.deletes.append({file.path, file.imageId, file.sha256});
            for (const Pair &pair : comparison.moved)
                plan.moves.append({pair.gallery.path, pair.folder.path, pair.gallery.imageId});
            for (const File &file : comparison.onlyInFolder)
                plan.copies.append({file.path, file.path, file.size});
            for (const Pair &pair : comparison.changed)
                plan.copies.append({pair.folder.path, pair.gallery.path, pair.folder.size, true,
                                    pair.gallery.imageId, pair.gallery.sha256});
            QString dbError;
            Progress progress(promise);
            progress.begin(workOf(plan));
            run(plan, progress, control, ask, &writer, result, dbError);
            finishWriter(writer, dbError, result);
        }
    }
    Db::close(connection);
    promise.addResult(result);
}

void merge(QPromise<ApplyResult> &promise, qint64 galleryId, const QString &rootFolder, const QString &folder,
           const Comparison &comparison, Control *control)
{
    // Nothing is deleted: no question can come up.
    const Scanner::TrashQuestion never = [](const QString &, Scanner::TrashProblem, const QString &) {
        return Scanner::TrashChoice::Stop;
    };
    const QString connection = QStringLiteral("xgallery_sync_merge_%1").arg(quintptr(QThread::currentThreadId()));
    ApplyResult result;
    {
        QSqlDatabase db = Db::open(connection);
        GalleryWriter writer(db, galleryId);
        if (!db.isOpen()) {
            result.error = Db::lastError();
        } else if (writer.begin(&result.error)) {
            // Each side gets the other's content it lacks, at the same path; a taken name gets " (1)".
            Plan toFolder{rootFolder, folder, {}, {}, {}};
            Plan toGallery{folder, rootFolder, {}, {}, {}};
            for (const File &file : comparison.onlyInGallery)
                toFolder.copies.append({file.path, file.path, file.size, false, 0, {}, true});
            for (const File &file : comparison.onlyInFolder)
                toGallery.copies.append({file.path, file.path, file.size, false, 0, {}, true});
            for (const Pair &pair : comparison.changed) {
                toFolder.copies.append({pair.gallery.path, pair.folder.path, pair.gallery.size, false, 0, {}, true});
                toGallery.copies.append({pair.folder.path, pair.gallery.path, pair.folder.size, false, 0, {}, true});
            }
            QString dbError;
            Progress progress(promise);
            progress.begin(workOf(toFolder) + workOf(toGallery));
            run(toFolder, progress, control, never, nullptr, result, dbError);
            if (!result.stopped)
                run(toGallery, progress, control, never, &writer, result, dbError);
            finishWriter(writer, dbError, result);
        }
    }
    Db::close(connection);
    promise.addResult(result);
}

} // namespace FolderSync
