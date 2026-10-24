#include "virtualpath.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include <algorithm>

namespace {

const QString Digits = QStringLiteral("0123456789");
const QString Latin = QStringLiteral("abcdefghijklmnopqrstuvwxyz");
// Ukrainian alphabet with the Russian-only letters in their places.
const QString Cyrillic = QStringLiteral("абвгґдеєёжзиіїйклмнопрстуфхцчшщъыьэюя");

QChar keyChar(QChar c)
{
    if (c == u'/')
        return QChar(0x0001);
    if (c.isSpace())
        return QChar(0x0002);
    // Letters first, digits after them: "Маша/Море/001".
    if (qsizetype i = Latin.indexOf(c); i >= 0)
        return QChar(char16_t(0xE000 + i));
    if (qsizetype i = Cyrillic.indexOf(c); i >= 0)
        return QChar(char16_t(0xE020 + i));
    if (qsizetype i = Digits.indexOf(c); i >= 0)
        return QChar(char16_t(0xE050 + i));
    return c; // punctuation, other scripts: as is
}

} // namespace

namespace VirtualPath {

QString sortKey(const QString &virtualPath)
{
    // Lower case + decomposition, so "É" sorts like "e". But "й"/"ї"/"ё" are letters of their
    // own: decomposing them would turn them into "и"/"і"/"е", so they are kept whole.
    QString key;
    const QString lower = virtualPath.toLower();
    key.reserve(lower.size());
    for (QChar c : lower) {
        if (Cyrillic.contains(c)) {
            key += keyChar(c);
            continue;
        }
        for (QChar d : QString(c).normalized(QString::NormalizationForm_D))
            if (d.category() != QChar::Mark_NonSpacing)
                key += keyChar(d);
    }
    return key;
}

QString build(QStringList tags)
{
    std::sort(tags.begin(), tags.end(), [](const QString &a, const QString &b) {
        const QString ka = sortKey(a), kb = sortKey(b);
        return ka != kb ? ka < kb : a < b; // the plain text breaks ties, so the order is total
    });
    return tags.join(u'/');
}

bool update(const QSqlDatabase &db, const QList<qint64> &imageIds, QString *error)
{
    QSqlQuery tags(db), store(db);
    tags.prepare("SELECT t.name FROM tags t JOIN image_tags it ON it.tag_id = t.id WHERE it.image_id = ?");
    store.prepare("UPDATE images SET vpath = ?, vpath_key = ? WHERE id = ?");
    for (qint64 id : imageIds) {
        tags.addBindValue(id);
        if (!tags.exec()) {
            *error = tags.lastError().text();
            return false;
        }
        QStringList names;
        while (tags.next())
            names << tags.value(0).toString();
        tags.finish();

        // An image without tags has an empty path. It must be bound as '' : a null QString
        // becomes NULL and breaks the NOT NULL columns.
        const QString path = build(names);
        const QString key = sortKey(path);
        store.addBindValue(path.isNull() ? QStringLiteral("") : path);
        store.addBindValue(key.isNull() ? QStringLiteral("") : key);
        store.addBindValue(id);
        if (!store.exec()) {
            *error = store.lastError().text();
            return false;
        }
    }
    return true;
}

} // namespace VirtualPath
