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
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override; // right click: rotate 90° CCW
    void closeEvent(QCloseEvent *event) override;

signals:
    // The file on disk was rewritten (e.g. rotation saved).
    void fileChanged(const QString &path);

private:
    void showImage(int index);
    void step(int delta);
    void toggleFullScreen();
    void setTurns(int quarterTurnsCcw);
    void placeSaveBar();
    bool saveRotation();
    bool confirmLeave();
    void updatePixmap();

    QStringList m_filePaths;
    int m_index = 0;
    bool m_allowUpscale;
    bool m_wasMaximized = false; // state to return to when leaving full screen
    QLabel *m_label;
    QPixmap m_loaded;   // as read from disk (EXIF orientation applied)
    QPixmap m_original; // m_loaded with the pending rotation
    int m_turns = 0;    // pending quarter turns counter-clockwise
    QWidget *m_saveBar = nullptr;
};

#endif // IMAGEVIEWER_H
