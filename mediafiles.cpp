#include "mediafiles.h"

#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QImageReader>
#include <QPainter>
#include <QPainterPath>
#include <QSet>

#ifdef Q_OS_WIN
#include <windows.h>
#include <shobjidl.h>
#endif

namespace {

QString suffixOf(const QString &path)
{
    return QFileInfo(path).suffix().toLower();
}

const QSet<QString> &imageSuffixes()
{
    static const QSet<QString> set = [] {
        QSet<QString> s;
        for (const QByteArray &format : QImageReader::supportedImageFormats())
            s.insert(QString::fromLatin1(format).toLower());
        return s;
    }();
    return set;
}

const QSet<QString> &videoSuffixes()
{
    static const QSet<QString> set = {
        "mp4", "m4v", "mov", "qt", "avi", "mpg", "mpeg", "mpe", "m1v", "m2v", "mkv", "webm",
        "wmv", "asf", "flv", "f4v", "3gp", "3g2", "mts", "m2ts", "ts", "vob", "ogv", "divx", "rm", "rmvb",
    };
    return set;
}

// Grey rounded card with the extension in big letters.
QImage extensionCard(const QString &label, int size)
{
    QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF card(size * 0.12, size * 0.06, size * 0.76, size * 0.88);
    p.setPen(QPen(QColor(150, 150, 150), 2));
    p.setBrush(QColor(235, 235, 235));
    p.drawRoundedRect(card, size * 0.06, size * 0.06);

    const QString text = label.isEmpty() ? QStringLiteral("?") : label;
    QFont font;
    font.setBold(true);
    font.setPixelSize(qMax(10, int(size * (text.size() <= 3 ? 0.22 : 0.80 / text.size()))));
    p.setFont(font);
    p.setPen(QColor(90, 90, 90));
    p.drawText(card, Qt::AlignCenter, text);
    return image;
}

// Small dark label in the bottom-left corner, so videos are recognisable in the grid.
void drawBadge(QImage &image, const QString &label)
{
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    QFont font;
    font.setBold(true);
    font.setPixelSize(qMax(10, image.height() / 9));
    p.setFont(font);
    const QString text = QStringLiteral("▶ ") + label;
    const QRect textRect = p.fontMetrics().boundingRect(text).adjusted(-5, -2, 5, 2);
    const QRect badge(4, image.height() - textRect.height() - 4, textRect.width(), textRect.height());
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 170));
    p.drawRoundedRect(badge, 4, 4);
    p.setPen(Qt::white);
    p.drawText(badge, Qt::AlignCenter, text);
}

QImage scaledToFit(const QImage &image, int size)
{
    if (image.isNull() || (image.width() <= size && image.height() <= size))
        return image;
    return image.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

QImage imageThumbnail(const QString &path, int size)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QSize original = reader.size();
    if (original.isValid() && (original.width() > size || original.height() > size))
        reader.setScaledSize(original.scaled(size, size, Qt::KeepAspectRatio)); // cheap for JPEG
    return scaledToFit(reader.read(), size);
}

// The same thumbnail Explorer shows (for videos: a frame from the start), using whatever
// codecs Windows has. No extra libraries needed. Empty if Windows can't make one.
QImage shellThumbnail(const QString &path, int size)
{
#ifdef Q_OS_WIN
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    QImage result;
    const QString native = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
    IShellItemImageFactory *factory = nullptr;
    if (SUCCEEDED(SHCreateItemFromParsingName(reinterpret_cast<const wchar_t *>(native.utf16()), nullptr,
                                              IID_PPV_ARGS(&factory)))) {
        HBITMAP bitmap = nullptr;
        // THUMBNAILONLY: no generic file-type icon if there is no real thumbnail.
        if (SUCCEEDED(factory->GetImage(SIZE{size, size}, SIIGBF_THUMBNAILONLY | SIIGBF_BIGGERSIZEOK,
                                        &bitmap))) {
            result = QImage::fromHBITMAP(bitmap);
            DeleteObject(bitmap);
            // Video thumbnails often come as 32-bit bitmaps whose alpha is all zero, which
            // would make them fully transparent. Treat such images as opaque.
            if (result.hasAlphaChannel() && result.depth() == 32) {
                bool anyAlpha = false;
                for (int y = 0; y < result.height() && !anyAlpha; ++y) {
                    const auto *line = reinterpret_cast<const QRgb *>(result.constScanLine(y));
                    for (int x = 0; x < result.width(); ++x)
                        if (qAlpha(line[x]) != 0) {
                            anyAlpha = true;
                            break;
                        }
                }
                if (!anyAlpha)
                    result.reinterpretAsFormat(QImage::Format_RGB32); // keep the colour bytes as they are
            }
        }
        factory->Release();
    }
    if (SUCCEEDED(init))
        CoUninitialize();
    return scaledToFit(result, size);
#else
    Q_UNUSED(path);
    Q_UNUSED(size);
    return {};
#endif
}

} // namespace

namespace MediaFiles {

Kind kindOf(const QString &path)
{
    const QString suffix = suffixOf(path);
    if (imageSuffixes().contains(suffix))
        return Kind::Image;
    if (videoSuffixes().contains(suffix))
        return Kind::Video;
    return Kind::Other;
}

QString extensionLabel(const QString &path)
{
    return QFileInfo(path).suffix().toUpper();
}

QImage makeThumbnail(const QString &path, int size)
{
    switch (kindOf(path)) {
    case Kind::Image:
        return imageThumbnail(path, size);
    case Kind::Video: {
        QImage frame = shellThumbnail(path, size);
        if (frame.isNull())
            return extensionCard(extensionLabel(path), size);
        frame = frame.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        drawBadge(frame, extensionLabel(path));
        return frame;
    }
    case Kind::Other:
        break;
    }
    return extensionCard(extensionLabel(path), size);
}

} // namespace MediaFiles
