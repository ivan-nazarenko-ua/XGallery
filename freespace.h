#ifndef FREESPACE_H
#define FREESPACE_H

#include <QString>
#include <QtGlobal>

// Free room on drives: checked before copying many bytes, and asked after a failed write ("was it the room?").
// Any thread; no GUI.
namespace FreeSpace {

// The download manager warns before a run when a drive it writes to has less than this.
inline constexpr qint64 LowSpaceBytes = 100LL * 1024 * 1024;

// The last bytes of the database's drive stay for the database itself (its rollback journal may grow to the
// size of the tables a change touches): at this much free or less, nothing new goes into the gallery and the
// thumbnail cache stores nothing (owner's rule).
inline constexpr qint64 DbReserveBytes = 30LL * 1024 * 1024;

// Bytes the user may still write on the drive of `path` (a file or folder; the nearest existing folder up
// from it counts); -1 if unknown.
qint64 available(const QString &path);
// The drive (or mount point) of `path` as the user knows it ("D:\"); empty if unknown.
QString driveOf(const QString &path);
// The two paths lie on the same drive (both known).
bool sameDrive(const QString &a, const QString &b);
// After a failed write of `bytes` into `path`: the drive has less room than that, so the room is the reason
// (a part written so far must be removed first, or it is counted as taken).
bool isOutOfSpace(const QString &path, qint64 bytes);

// The database's drive has DbReserveBytes free or less (unknown room: false).
bool dbReserveReached();
// Writing `bytes` more into `path` leaves the database's drive more than DbReserveBytes free. A path on another
// drive doesn't count, unless addsToDb (the files get records): then the database's drive must not be at the
// reserve either. Unknown room: true.
bool keepsDbReserve(const QString &path, qint64 bytes, bool addsToDb);
// The drive of the database ("E:\").
QString dbDrive();
// For the user: why nothing can be added now, and to free room on that drive.
QString reserveRefusal();
// For the user: a job adding files stopped at the reserve; what was added stays.
QString reserveStop();

} // namespace FreeSpace

#endif // FREESPACE_H
