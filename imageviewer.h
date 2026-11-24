#ifndef IMAGEVIEWER_H
#define IMAGEVIEWER_H

#include <QPixmap>
#include <QStringList>
#include <QWidget>

#include <functional>

class QLabel;

// Separate window showing the original image, scaled to fit.
// Left/Right arrows go through filePaths in a circle; Delete (with a delete handler) removes the file.
class ImageViewer : public QWidget
{
    Q_OBJECT

public:
    // allowUpscale: images smaller than the window are enlarged to fit it;
    // otherwise they are never shown bigger than the original.
    ImageViewer(const QStringList &filePaths, int currentIndex, bool allowUpscale,
                QWidget *parent = nullptr);

    // Called for the Delete key after the user confirmed: moves the file to the Recycle Bin and drops
    // its record, telling the user what went wrong itself. True = the file is gone, the viewer goes on
    // to the next one (or closes after the last). Not set: Delete shows the refusal text (if any).
    using DeleteHandler = std::function<bool(const QString &path)>;
    void setDeleteHandler(DeleteHandler handler) { m_deleteHandler = std::move(handler); }
    // Why Delete can't work in this viewer: shown as a warning instead of deleting.
    void setDeleteRefusal(const QString &text) { m_deleteRefusal = text; }

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
    void deleteCurrent();
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
    DeleteHandler m_deleteHandler;
    QString m_deleteRefusal;
};

#endif // IMAGEVIEWER_H
