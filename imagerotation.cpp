#include "imagerotation.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>
#include <QSaveFile>
#include <QTransform>

namespace {

bool isJpeg(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == "jpg" || suffix == "jpeg" || suffix == "jpe" || suffix == "jfif";
}

struct ExifInfo
{
    qsizetype segmentStart = -1;     // APP1 marker position (0xFF 0xE1)
    qsizetype segmentLength = 0;     // whole segment incl. marker
    qsizetype orientationPos = -1;   // file position of the 2-byte orientation value
    bool bigEndian = false;
    int orientation = 0;             // 0 = no tag
};

quint16 read16(const QByteArray &d, qsizetype pos, bool bigEndian)
{
    const auto b0 = quint8(d[pos]), b1 = quint8(d[pos + 1]);
    return bigEndian ? quint16(b0 << 8 | b1) : quint16(b1 << 8 | b0);
}

quint32 read32(const QByteArray &d, qsizetype pos, bool bigEndian)
{
    const quint32 hi = read16(d, pos, bigEndian), lo = read16(d, pos + 2, bigEndian);
    return bigEndian ? (hi << 16 | lo) : (lo << 16 | hi);
}

void write16(QByteArray &d, qsizetype pos, quint16 value, bool bigEndian)
{
    d[pos] = char(bigEndian ? value >> 8 : value & 0xFF);
    d[pos + 1] = char(bigEndian ? value & 0xFF : value >> 8);
}

// Finds the Exif APP1 segment and the orientation tag in IFD0 (if any).
ExifInfo findExif(const QByteArray &d)
{
    ExifInfo info;
    if (d.size() < 4 || quint8(d[0]) != 0xFF || quint8(d[1]) != 0xD8)
        return info;
    qsizetype pos = 2;
    while (pos + 4 <= d.size() && quint8(d[pos]) == 0xFF) {
        const quint8 marker = quint8(d[pos + 1]);
        if (marker == 0xDA || marker == 0xD9) // start of scan / end of image: no more metadata
            break;
        const qsizetype length = read16(d, pos + 2, true);
        if (length < 2 || pos + 2 + length > d.size())
            break;
        if (marker == 0xE1 && d.mid(pos + 4, 6) == QByteArray("Exif\0\0", 6)) {
            info.segmentStart = pos;
            info.segmentLength = 2 + length;
            const qsizetype tiff = pos + 10;
            const qsizetype end = pos + 2 + length;
            if (tiff + 8 > end)
                return info;
            info.bigEndian = d.mid(tiff, 2) == "MM";
            const qsizetype ifd = tiff + read32(d, tiff + 4, info.bigEndian);
            if (ifd + 2 > end)
                return info;
            const int count = read16(d, ifd, info.bigEndian);
            for (int i = 0; i < count; ++i) {
                const qsizetype entry = ifd + 2 + qsizetype(i) * 12;
                if (entry + 12 > end)
                    break;
                if (read16(d, entry, info.bigEndian) == 0x0112) { // Orientation, SHORT
                    info.orientationPos = entry + 8;
                    info.orientation = read16(d, entry + 8, info.bigEndian);
                    break;
                }
            }
            return info;
        }
        pos += 2 + length;
    }
    return info;
}

// EXIF orientations 1..8 as 2x2 matrices (x right, y down) mapping stored pixels to the
// displayed picture: {a, b, c, d} = [[a, b], [c, d]].
struct Matrix
{
    int a, b, c, d;
    bool operator==(const Matrix &o) const { return a == o.a && b == o.b && c == o.c && d == o.d; }
    Matrix operator*(const Matrix &o) const
    {
        return {a * o.a + b * o.c, a * o.b + b * o.d, c * o.a + d * o.c, c * o.b + d * o.d};
    }
};

const Matrix OrientationMatrix[9] = {
    {1, 0, 0, 1},   // (0: unused)
    {1, 0, 0, 1},   // 1 normal
    {-1, 0, 0, 1},  // 2 mirrored horizontally
    {-1, 0, 0, -1}, // 3 rotated 180
    {1, 0, 0, -1},  // 4 mirrored vertically
    {0, 1, 1, 0},   // 5 transposed
    {0, -1, 1, 0},  // 6 rotated 90 CW
    {0, -1, -1, 0}, // 7 transversed
    {0, 1, -1, 0},  // 8 rotated 270 CW (90 CCW)
};

// Orientation after turning the displayed picture by `turns` x 90° counter-clockwise.
int rotatedOrientation(int orientation, int turns)
{
    if (orientation < 1 || orientation > 8)
        orientation = 1; // missing/invalid tag means "normal"
    Matrix m = OrientationMatrix[orientation];
    for (int i = 0; i < turns; ++i)
        m = OrientationMatrix[8] * m;
    for (int o = 1; o <= 8; ++o)
        if (OrientationMatrix[o] == m)
            return o;
    return 1;
}

QByteArray be16(quint16 v)
{
    QByteArray b;
    b.append(char(v >> 8));
    b.append(char(v & 0xFF));
    return b;
}

// Where a new APP1 goes: right after SOI, or after a JFIF APP0 segment that's there.
qsizetype app1InsertPos(const QByteArray &d)
{
    if (d.size() > 6 && quint8(d[2]) == 0xFF && quint8(d[3]) == 0xE0)
        return 4 + read16(d, 4, true);
    return 2;
}

// Rotation of a JPEG done purely in the EXIF orientation tag: pixels are never touched, so
// any number of rotations loses nothing. Returns the new file content, or empty if the file's
// structure is unusual (then the caller re-encodes).
QByteArray rotateJpegLosslessly(QByteArray data, int turns)
{
    const ExifInfo exif = findExif(data);
    if (data.size() < 4 || quint8(data[0]) != 0xFF || quint8(data[1]) != 0xD8)
        return {};

    // 1. The tag exists: change it in place.
    if (exif.orientationPos >= 0) {
        write16(data, exif.orientationPos, quint16(rotatedOrientation(exif.orientation, turns)), exif.bigEndian);
        return data;
    }

    const quint16 orientation = quint16(rotatedOrientation(1, turns));

    // 2. No EXIF at all: add a minimal one that only holds the orientation.
    if (exif.segmentStart < 0) {
        QByteArray tiff = QByteArray("MM\0\x2A\0\0\0\x08", 8);                    // big endian, IFD0 at 8
        tiff += be16(1);                                                         // 1 entry
        tiff += be16(0x0112) + be16(3) + QByteArray("\0\0\0\x01", 4) + be16(orientation) + be16(0);
        tiff += QByteArray(4, '\0');                                             // no next IFD
        const QByteArray body = QByteArray("Exif\0\0", 6) + tiff;
        data.insert(app1InsertPos(data), QByteArray("\xFF\xE1", 2) + be16(quint16(body.size() + 2)) + body);
        return data;
    }

    // 3. EXIF without the tag: write a copy of IFD0 with the tag added at the end of the
    //    segment and point the header to it. Nothing else moves, so every other offset
    //    (sub-IFDs, thumbnail, maker notes) stays valid.
    const qsizetype tiff = exif.segmentStart + 10;
    const qsizetype end = exif.segmentStart + exif.segmentLength;
    const bool be = exif.bigEndian;
    const qsizetype ifd = tiff + read32(data, tiff + 4, be);
    if (ifd + 2 > end)
        return {};
    const int count = read16(data, ifd, be);
    if (ifd + 2 + qsizetype(count) * 12 + 4 > end)
        return {};

    QByteArray entry(12, '\0');
    write16(entry, 0, 0x0112, be);
    write16(entry, 2, 3, be);
    write16(entry, be ? 6 : 4, 1, be); // count = 1 (32-bit)
    write16(entry, 8, orientation, be);

    QByteArray newIfd(2, '\0');
    write16(newIfd, 0, quint16(count + 1), be);
    bool inserted = false;
    for (int i = 0; i < count; ++i) {
        const QByteArray old = data.mid(ifd + 2 + qsizetype(i) * 12, 12);
        if (!inserted && read16(old, 0, be) > 0x0112) { // entries are sorted by tag
            newIfd += entry;
            inserted = true;
        }
        newIfd += old;
    }
    if (!inserted)
        newIfd += entry;
    newIfd += data.mid(ifd + 2 + qsizetype(count) * 12, 4); // next IFD pointer as it was

    QByteArray padding((end - tiff) % 2, '\0'); // IFDs start on a word boundary
    const qsizetype newOffset = end - tiff + padding.size();
    const qsizetype newLength = exif.segmentLength - 2 + padding.size() + newIfd.size();
    if (newLength > 0xFFFF)
        return {};

    data.insert(end, padding + newIfd);
    write16(data, exif.segmentStart + 2, quint16(newLength), true);
    write16(data, tiff + 4, be ? 0 : quint16(newOffset), be);       // 32-bit offset, low/high
    write16(data, tiff + 6, be ? quint16(newOffset) : 0, be);
    return data;
}

bool readAll(const QString &path, QByteArray *data, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        *error = f.errorString();
        return false;
    }
    *data = f.readAll();
    return true;
}

bool writeAll(const QString &path, const QByteArray &data, QString *error)
{
    QSaveFile f(path); // the original stays intact if anything goes wrong
    if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size() || !f.commit()) {
        *error = f.errorString();
        return false;
    }
    return true;
}

} // namespace

namespace ImageRotation {

bool canSave(const QString &path)
{
    QByteArray format = QFileInfo(path).suffix().toLower().toLatin1();
    if (format == "jpeg" || format == "jpe" || format == "jfif")
        format = "jpg";
    if (format == "tif")
        format = "tiff";
    return QImageWriter::supportedImageFormats().contains(format);
}

bool isMultiFrame(const QString &path)
{
    return QImageReader(path).imageCount() > 1;
}

bool isLossless(const QString &path)
{
    if (!isJpeg(path))
        return false;
    QByteArray data;
    QString error;
    if (!readAll(path, &data, &error))
        return false;
    return !rotateJpegLosslessly(data, 1).isEmpty();
}

bool rotateFile(const QString &path, int quarterTurnsCcw, QString *error)
{
    const int turns = ((quarterTurnsCcw % 4) + 4) % 4;
    if (turns == 0)
        return true;
    if (!canSave(path)) {
        *error = QCoreApplication::translate("ImageRotation", "Цей формат файлу не можна зберегти.");
        return false;
    }

    QByteArray data;
    if (!readAll(path, &data, error))
        return false;

    const bool jpeg = isJpeg(path);
    const ExifInfo exif = jpeg ? findExif(data) : ExifInfo{};

    // JPEG: only the EXIF orientation changes, so repeated rotations never lose quality.
    if (jpeg) {
        const QByteArray rotated = rotateJpegLosslessly(data, turns);
        if (!rotated.isEmpty())
            return writeAll(path, rotated, error);
    }

    // Other formats (and odd JPEGs): re-encode the pixels as shown (EXIF orientation applied).
    // PNG/BMP/TIFF and WebP at quality 100 are lossless, so this doesn't degrade them either.
    QBuffer source(&data);
    source.open(QIODevice::ReadOnly);
    QImageReader reader(&source);
    reader.setAutoTransform(true);
    QImage image = reader.read();
    if (image.isNull()) {
        *error = reader.errorString();
        return false;
    }
    if (reader.imageCount() > 1) {
        *error = QCoreApplication::translate("ImageRotation", "Файл має кілька кадрів, а зберегти можна лише один.");
        return false;
    }
    image = image.transformed(QTransform().rotate(-90.0 * turns));

    QByteArray encoded;
    QBuffer target(&encoded);
    target.open(QIODevice::WriteOnly);
    QByteArray format = QFileInfo(path).suffix().toLower().toLatin1();
    if (jpeg)
        format = "jpg";
    else if (format == "tif")
        format = "tiff";
    QImageWriter writer(&target, format);
    writer.setQuality(100); // best JPEG quality; lossless for WebP
    if (!writer.write(image)) {
        *error = writer.errorString();
        return false;
    }

    // JPEG: keep the original EXIF block (date, camera, GPS...). The pixels are upright now,
    // so its orientation (if any) must say "normal".
    if (jpeg && exif.segmentStart >= 0) {
        QByteArray app1 = data.mid(exif.segmentStart, exif.segmentLength);
        if (exif.orientationPos >= 0)
            write16(app1, exif.orientationPos - exif.segmentStart, 1, exif.bigEndian);
        encoded.insert(app1InsertPos(encoded), app1);
    }
    return writeAll(path, encoded, error);
}

} // namespace ImageRotation
