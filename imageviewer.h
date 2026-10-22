#ifndef IMAGEVIEWER_H
#define IMAGEVIEWER_H

#include <QPixmap>
#include <QStringList>
#include <QWidget>

class QLabel;

// Separate window showing the original image, scaled to fit.
// Left/Right arrows go through filePaths in a circle.
class ImageViewer : public QWidget
{
    Q_OBJECT

public:
    // allowUpscale: images smaller than the window are enlarged to fit it;
    // otherwise they are never shown bigger than the original.
    ImageViewer(const QStringList &filePaths, int currentIndex, bool allowUpscale,
                QWidget *parent = nullptr);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void showImage(int index);
    void step(int delta);
    void updatePixmap();

    QStringList m_filePaths;
    int m_index = 0;
    bool m_allowUpscale;
    QLabel *m_label;
    QPixmap m_original;
};

#endif // IMAGEVIEWER_H
