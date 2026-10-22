#include "imageviewer.h"

#include "imagerotation.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QPushButton>
#include <QTransform>
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
    setStyleSheet("ImageViewer, QLabel { background-color: #202020; color: #dddddd; }");

    // Floating bar shown while there is an unsaved rotation.
    m_saveBar = new QWidget(this);
    m_saveBar->setObjectName("saveBar");
    m_saveBar->setAttribute(Qt::WA_StyledBackground);
    m_saveBar->setStyleSheet("#saveBar { background-color: rgba(0, 0, 0, 190); border-radius: 6px; }"
                             "#saveBar QLabel { background: transparent; color: white; }");
    auto *barLayout = new QHBoxLayout(m_saveBar);
    barLayout->setContentsMargins(10, 6, 10, 6);
    barLayout->addWidget(new QLabel(tr("Зображення повернуто"), m_saveBar));
    auto *saveButton = new QPushButton(tr("Зберегти у файл"), m_saveBar);
    auto *resetButton = new QPushButton(tr("Скасувати"), m_saveBar);
    saveButton->setFocusPolicy(Qt::NoFocus); // keep arrows/Esc for the viewer
    resetButton->setFocusPolicy(Qt::NoFocus);
    connect(saveButton, &QPushButton::clicked, this, &ImageViewer::saveRotation);
    connect(resetButton, &QPushButton::clicked, this, [this] { setTurns(0); });
    barLayout->addWidget(saveButton);
    barLayout->addWidget(resetButton);
    m_saveBar->hide();

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
        m_loaded = QPixmap();
        m_original = QPixmap();
        m_turns = 0;
        m_saveBar->hide();
        m_label->setPixmap(QPixmap());
        m_label->setText(tr("Не вдалося відкрити зображення:\n%1").arg(reader.errorString()));
        return;
    }
    m_loaded = QPixmap::fromImage(image);
    setTurns(0);
}

void ImageViewer::setTurns(int quarterTurnsCcw)
{
    m_turns = ((quarterTurnsCcw % 4) + 4) % 4;
    m_original = m_turns == 0 ? m_loaded : m_loaded.transformed(QTransform().rotate(-90.0 * m_turns));
    m_saveBar->setVisible(m_turns != 0 && !m_loaded.isNull());
    placeSaveBar();
    updatePixmap();
}

void ImageViewer::placeSaveBar()
{
    m_saveBar->adjustSize();
    m_saveBar->move((width() - m_saveBar->width()) / 2, height() - m_saveBar->height() - 16);
    m_saveBar->raise();
}

bool ImageViewer::saveRotation()
{
    if (m_turns == 0 || m_filePaths.isEmpty())
        return true;
    const QString path = m_filePaths.at(m_index);
    if (!ImageRotation::canSave(path)) {
        QMessageBox::warning(this, tr("Поворот"), tr("Файли цього формату не можна перезаписати."));
        return false;
    }
    if (!ImageRotation::isLossless(path)
        && QMessageBox::question(this, tr("Поворот"),
                                 tr("Щоб зберегти поворот, файл буде перезаписано.\n"
                                    "PNG, BMP, TIFF, WebP — без втрати якості, але їхні метадані можуть "
                                    "не зберегтися; JPEG з нестандартною структурою — з якістю 100 і "
                                    "збереженням EXIF.\n\n"
                                    "Перезаписати оригінал?"))
               != QMessageBox::Yes)
        return false;

    QString error;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool ok = ImageRotation::rotateFile(path, m_turns, &error);
    QApplication::restoreOverrideCursor();
    if (!ok) {
        QMessageBox::warning(this, tr("Поворот"), tr("Не вдалося зберегти файл:\n%1").arg(error));
        return false;
    }
    showImage(m_index); // re-read from disk: what you see is what was saved
    emit fileChanged(path);
    return true;
}

// Unsaved rotation before leaving the image: save / discard / stay.
bool ImageViewer::confirmLeave()
{
    if (m_turns == 0)
        return true;
    const auto answer = QMessageBox::question(this, tr("Поворот"), tr("Зберегти поворот у файл?"),
                                              QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
                                              QMessageBox::Save);
    if (answer == QMessageBox::Cancel)
        return false;
    if (answer == QMessageBox::Save)
        return saveRotation();
    setTurns(0);
    return true;
}

void ImageViewer::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::RightButton && !m_loaded.isNull())
        setTurns(m_turns + 1); // 90° counter-clockwise per click, like the Windows viewer
    else
        QWidget::mousePressEvent(event);
}

void ImageViewer::closeEvent(QCloseEvent *event)
{
    if (!confirmLeave()) {
        event->ignore();
        return;
    }
    QWidget::closeEvent(event);
}

void ImageViewer::step(int delta)
{
    const int count = int(m_filePaths.size());
    if (count < 2 || !confirmLeave())
        return;
    showImage(((m_index + delta) % count + count) % count);
}

void ImageViewer::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    placeSaveBar();
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
