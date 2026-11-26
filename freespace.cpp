#include "freespace.h"

#include "database.h"

#include <QCoreApplication>
#include <QLocale>

#include <QDir>
#include <QFileInfo>
#include <QStorageInfo>

namespace {

// The storage of the nearest existing folder at or above `path` (a target folder may not exist yet).
QStorageInfo storageOf(const QString &path)
{
    QString existing = QFileInfo(path).absoluteFilePath();
    while (!existing.isEmpty() && !QFileInfo::exists(existing)) {
        const QString parent = QFileInfo(existing).absolutePath();
        if (parent == existing)
            break;
        existing = parent;
    }
    return QStorageInfo(existing);
}

// Filesystem overhead (clusters, the folder entry): a file that "just" fits may not.
constexpr qint64 Slack = 1024 * 1024;

QString dbFolder()
{
    return QFileInfo(Db::databasePath()).absolutePath();
}

} // namespace

namespace FreeSpace {

qint64 available(const QString &path)
{
    const QStorageInfo storage = storageOf(path);
    return storage.isValid() && storage.isReady() ? storage.bytesAvailable() : -1;
}

QString driveOf(const QString &path)
{
    const QStorageInfo storage = storageOf(path);
    return storage.isValid() ? QDir::toNativeSeparators(storage.rootPath()) : QString();
}

bool sameDrive(const QString &a, const QString &b)
{
    const QString x = driveOf(a);
    return !x.isEmpty() && x.compare(driveOf(b), Qt::CaseInsensitive) == 0;
}

bool isOutOfSpace(const QString &path, qint64 bytes)
{
    const qint64 free = available(path);
    return free >= 0 && free < bytes + Slack;
}

bool dbReserveReached()
{
    const qint64 free = available(dbFolder());
    return free >= 0 && free <= DbReserveBytes;
}

bool keepsDbReserve(const QString &path, qint64 bytes, bool addsToDb)
{
    const QString db = dbFolder();
    const bool same = sameDrive(path, db);
    if (!same && !addsToDb)
        return true;
    const qint64 free = available(db);
    return free < 0 || free - (same ? qMax<qint64>(bytes, 0) : 0) > DbReserveBytes;
}

QString dbDrive()
{
    const QString drive = driveOf(dbFolder());
    return drive.isEmpty() ? QDir::toNativeSeparators(dbFolder()) : drive;
}

QString reserveRefusal()
{
    const qint64 free = available(dbFolder());
    return QCoreApplication::translate("FreeSpace",
                                       "На диску %1 (там база даних) вільно лише %2. Останні %3 лишаються для "
                                       "самої бази, тому додавати файли в галерею зараз не можна.\n\n"
                                       "Звільніть місце на цьому носії.")
        .arg(dbDrive(), QLocale().formattedDataSize(qMax<qint64>(free, 0)),
             QLocale().formattedDataSize(DbReserveBytes));
}

QString reserveStop()
{
    return QCoreApplication::translate("FreeSpace",
                                       "На диску %1 (там база даних) лишився тільки запас для бази (%2): додавання "
                                       "зупинено, уже додане залишилось. Звільніть місце на цьому носії.")
        .arg(dbDrive(), QLocale().formattedDataSize(DbReserveBytes));
}

} // namespace FreeSpace
