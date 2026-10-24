#ifndef VIRTUALPATH_H
#define VIRTUALPATH_H

#include <QList>
#include <QSqlDatabase>
#include <QString>
#include <QStringList>

// The "virtual path" of a file: all its tags in alphabetical order joined with '/'
// ("2024/Маша/Море"). Files with the same tag set get the same path, wherever they lie on
// disk, so sorting by it keeps such files together in a stable order.
// Stored in images.vpath (shown) and images.vpath_key (sorted on, see sortKey()).
// Tags therefore must not contain '/' or '\'.
namespace VirtualPath {

QString build(QStringList tags);

// Binary-comparable form of a virtual path that follows the alphabet: Latin (accents
// ignored), then Ukrainian/Russian letters in their alphabetical order, then digits
// ("Маша/Море/001"); '/' sorts before everything, so "Маша" comes before "Маша Петрівна"
// and "Маша/Море". Changing this order needs a migration step that recomputes the paths.
QString sortKey(const QString &virtualPath);

// Recomputes vpath/vpath_key of the images from their current tags (on `db`, inside the
// caller's transaction if there is one).
bool update(const QSqlDatabase &db, const QList<qint64> &imageIds, QString *error);

} // namespace VirtualPath

#endif // VIRTUALPATH_H
