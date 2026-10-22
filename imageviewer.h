#ifndef IMAGEVIEWER_H
#define IMAGEVIEWER_H

#include <QPixmap>
#include <QWidget>

class QLabel;

// Separate window showing the original image, scaled to fit.
class ImageViewer : public QWidget
{
    Q_OBJECT

public:
    explicit ImageViewer(const QString &filePath, QWidget *parent = nullptr);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void updatePixmap();

    QLabel *m_label;
    QPixmap m_original;
};

#endif // IMAGEVIEWER_H
