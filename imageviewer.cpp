#include "imageviewer.h"

#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImageReader>
#include <QKeyEvent>
#include <QLabel>
#include <QScreen>
#include <QVBoxLayout>

ImageViewer::ImageViewer(const QStringList &filePaths, int currentIndex, bool allowUpscale,
                         QWidget *parent)
    : QWidget(parent, Qt::Window)
    , m_filePaths(filePaths)
    , m_allowUpscale(allowUpscale)
    , m_label(new QLabel(this))
{
    setAttribute(Qt::WA_DeleteOnClose);
    setFocusPolicy(Qt::StrongFocus);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_label);
    m_label->setAlignment(Qt::AlignCenter);
    m_label->setMinimumSize(1, 1);
    m_label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    setStyleSheet("background-color: #202020; color: #dddddd;");

    showImage(currentIndex);

    // Initial window size follows the first image; later images keep the window as it is.
    const QRect available = screen() ? screen()->availableGeometry() : QRect(0, 0, 1280, 800);
    QSize size = m_original.isNull() ? QSize(480, 320) : m_original.size();
    size = size.boundedTo(available.size() * 0.85);
    if (!m_original.isNull())
        size = m_original.size().scaled(size, Qt::KeepAspectRatio);
    resize(size.expandedTo(QSize(320, 240)));
}

void ImageViewer::showImage(int index)
{
    if (m_filePaths.isEmpty())
        return;
    m_index = index;
    const QString filePath = m_filePaths.at(m_index);

    QString title = QFileInfo(filePath).fileName() + " — " + QDir::toNativeSeparators(filePath);
    if (m_filePaths.size() > 1)
        title = QString("[%1 / %2] ").arg(m_index + 1).arg(m_filePaths.size()) + title;
    setWindowTitle(title);

    QImageReader reader(filePath);
    reader.setAutoTransform(true); // respect EXIF orientation
    const QImage image = reader.read();
    if (image.isNull()) {
        m_original = QPixmap();
        m_label->setPixmap(QPixmap());
        m_label->setText(tr("Не вдалося відкрити зображення:\n%1").arg(reader.errorString()));
        return;
    }
    m_original = QPixmap::fromImage(image);
    updatePixmap();
}

void ImageViewer::step(int delta)
{
    const int count = int(m_filePaths.size());
    if (count < 2)
        return;
    showImage(((m_index + delta) % count + count) % count);
}

void ImageViewer::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updatePixmap();
}

void ImageViewer::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Escape:
        if (isFullScreen())
            toggleFullScreen(); // first Esc leaves full screen, the next one closes
        else
            close();
        break;
    case Qt::Key_Left:
    case Qt::Key_Up:
        step(-1);
        break;
    case Qt::Key_Right:
    case Qt::Key_Down:
        step(+1);
        break;
    default:
        QWidget::keyPressEvent(event);
    }
}

void ImageViewer::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        toggleFullScreen();
    else
        QWidget::mouseDoubleClickEvent(event);
}

void ImageViewer::toggleFullScreen()
{
    if (isFullScreen()) {
        // showNormal() restores the geometry the window had before going full screen.
        if (m_wasMaximized)
            showMaximized();
        else
            showNormal();
    } else {
        m_wasMaximized = isMaximized();
        showFullScreen();
    }
}

void ImageViewer::updatePixmap()
{
    if (m_original.isNull())
        return;
    const qreal dpr = devicePixelRatioF();
    const QSize target = m_label->size() * dpr;
    const bool fits = m_original.width() <= target.width() && m_original.height() <= target.height();
    if (fits && !m_allowUpscale) {
        m_label->setPixmap(m_original); // never bigger than the original
        return;
    }
    QPixmap scaled = m_original.scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    scaled.setDevicePixelRatio(dpr);
    m_label->setPixmap(scaled);
}
