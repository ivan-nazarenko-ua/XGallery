#ifndef MEDIAFILES_H
#define MEDIAFILES_H

#include <QImage>
#include <QString>

// What the gallery knows how to do with a file, decided by its extension.
namespace MediaFiles {

enum class Kind {
    Image, // shown in the built-in viewer
    Video, // opened in the program associated with it in Windows
    Other  // stored in the gallery, but nothing to open
};

Kind kindOf(const QString &path);

// Upper-case extension without the dot ("MP4"), or empty.
QString extensionLabel(const QString &path);

// Thumbnail no bigger than size x size. Safe to call from worker threads.
// Images: the picture itself. Videos: the Windows Explorer thumbnail (first frame) with
// the extension as a badge, or just the extension if Windows has none. Others: the extension.
// Returns a null image only for an image file that can't be read.
QImage makeThumbnail(const QString &path, int size);

} // namespace MediaFiles

#endif // MEDIAFILES_H
