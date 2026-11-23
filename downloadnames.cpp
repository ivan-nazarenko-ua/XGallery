#include "downloadnames.h"

#include "htmllinks.h"
#include "scanner.h"

#include <QDir>
#include <QMimeDatabase>
#include <QRegularExpression>

namespace DownloadNames {

QString nextFolderNumber(const QStringList &folderNames)
{
    static const QRegularExpression leadingDigits(QStringLiteral("^[0-9]+"));
    qint64 largest = 0;
    for (const QString &name : folderNames) {
        const QRegularExpressionMatch match = leadingDigits.match(name);
        if (!match.hasMatch())
            continue;
        bool ok = false;
        const qint64 number = match.captured().toLongLong(&ok);
        if (ok && number < 1'000'000'000'000LL) // absurdly long numbers would overflow; they don't count
            largest = qMax(largest, number);
    }
    const qint64 next = largest + 1;
    return next < 1000 ? QStringLiteral("%1").arg(next, 3, 10, QLatin1Char('0')) : QString::number(next);
}

QString nextFolderName(const QString &folder)
{
    const QStringList names = QDir(folder).entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden);
    return nextFolderNumber(names) + QLatin1String(FolderSuffix);
}

QString nameFromUrl(const QUrl &url)
{
    static const QRegularExpression forbidden(QStringLiteral(R"([\\/:*?"<>|\x00-\x1f])"));
    QString name = url.fileName(QUrl::FullyDecoded);
    name.replace(forbidden, QStringLiteral("_"));
    name = name.trimmed();
    while (name.endsWith('.') || name.endsWith(' '))
        name.chop(1);

    const qsizetype dot = name.lastIndexOf('.');
    if (dot <= 0 || dot == name.size() - 1)
        return {}; // no extension (or nothing but one)
    QString base = name.left(dot);
    const QString suffix = name.mid(dot);
    if (suffix.size() > 32)
        return {}; // not an extension, just a dot somewhere in a long name
    constexpr int MaxLength = 180; // the gallery path comes before it; Windows' limit is 260 in all
    if (name.size() > MaxLength)
        base = base.left(MaxLength - suffix.size()).trimmed();
    name = base + suffix;
    if (!Scanner::isValidFileName(name)) {
        // CON.jpg, NUL.txt...: Windows device names, as import does with folders.
        const qsizetype stem = name.indexOf('.');
        name = name.left(stem) + '_' + name.mid(stem);
    }
    return Scanner::isValidFileName(name) ? name : QString();
}

QString numberedName(int number, int count, const QString &extension)
{
    const int width = qMax(3, int(QString::number(count).size()));
    return QStringLiteral("file_%1.%2").arg(number, width, 10, QLatin1Char('0')).arg(extension);
}

QString extensionFor(const QByteArray &contentType)
{
    const QString mime = HtmlLinks::mimeTypeOf(contentType);
    if (!mime.isEmpty()) {
        const QMimeType type = QMimeDatabase().mimeTypeForName(mime);
        if (type.isValid() && !type.preferredSuffix().isEmpty())
            return type.preferredSuffix();
    }
    return QStringLiteral("bin");
}

QString pageFileName(const QDir &dir, const QByteArray &contentType)
{
    const QString extension = HtmlLinks::mimeTypeOf(contentType) == QLatin1String("text/html")
                                  ? QStringLiteral("html") : QStringLiteral("bin");
    for (int n = 1;; ++n) {
        const QString name = QStringLiteral("page_%1.%2").arg(n).arg(extension);
        if (!dir.exists(name))
            return name;
    }
}

} // namespace DownloadNames
