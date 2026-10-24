#ifndef IMAGEROTATION_H
#define IMAGEROTATION_H

#include <QString>

// Rotating image files on disk by quarter turns.
//
// JPEG: only the EXIF orientation tag is changed (added if missing). Pixels and metadata are
// untouched, so any number of rotations loses nothing. Only a JPEG with a broken/odd
// structure falls back to re-encoding (quality 100, original EXIF block copied over).
// Other writable formats (PNG, BMP, TIFF, WebP, ...): re-encoded losslessly; their own
// metadata may be lost.
namespace ImageRotation {

// Can the file be written back at all (e.g. GIF can't)?
bool canSave(const QString &path);

// Animated or multi-page file (WebP, TIFF...): re-encoding would keep only the first frame.
bool isMultiFrame(const QString &path);

// True if rotating this file only touches the EXIF orientation tag.
bool isLossless(const QString &path);

// quarterTurnsCcw: 1 = 90° counter-clockwise, 2 = 180°, 3 = 90° clockwise.
bool rotateFile(const QString &path, int quarterTurnsCcw, QString *error);

} // namespace ImageRotation

#endif // IMAGEROTATION_H
