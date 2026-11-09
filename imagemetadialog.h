#ifndef IMAGEMETADIALOG_H
#define IMAGEMETADIALOG_H

#include <QDialog>
#include <QPointer>
#include <QStringList>

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class TagPickerDialog;

// Edits an image's tags and rating, or (several files) the tags they have in common.
// Nothing is written to the DB here: the caller reads the result after exec() == Accepted.
class ImageMetaDialog : public QDialog
{
    Q_OBJECT

public:
    // exec() returns this when the user confirmed deleting the file (to the Recycle Bin).
    static constexpr int DeleteRequested = 2;

    // One path: single-file mode (preview, rating, delete button).
    // Several paths: bulk mode; imageTags are the tags common to all of them, rating is hidden.
    ImageMetaDialog(const QStringList &filePaths, const QStringList &imageTags, int rating,
                    const QStringList &galleryTags, QWidget *parent = nullptr);

    // Viewer setting of the gallery, for images opened from this dialog.
    void setAllowUpscale(bool allow);

    // On: the "file name" field shows and edits the name without its extension.
    void setKeepExtension(bool keep);

    // While a file export runs the dialog is locked and can't be closed.
    void setExporting(bool exporting);

    QString filePath() const { return m_filePaths.value(0); } // the first one in bulk mode
    QString fileName() const; // single-file mode: the full name the "file name" field asks for
    QStringList tags() const;
    QStringList addedTags() const;  // compared with the tags the dialog started with
    QStringList removedTags() const;
    QStringList deletedTags() const { return m_deletedTags; } // to delete from the whole gallery
    int rating() const;

signals:
    // Double click on a tag: put it into the main window's search ("Містить").
    void tagActivated(const QString &tag);
    // A viewer opened from here rewrote the file (saved rotation).
    void fileChanged(const QString &path);
    // The user chose where to save a copy of the file; the caller does the copying.
    void exportRequested(const QString &destination);

public slots:
    void done(int result) override;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void chooseTags();
    void removeSelectedTags();
    void copySelectedTags();
    void pasteTags();
    void refresh();
    void updatePreview();
    void openFile(int index); // click on the preview / on a row of the bulk list
    void chooseExportDestination();

    QStringList m_galleryTags;
    QStringList m_tags;
    QStringList m_initialTags;
    QStringList m_deletedTags;
    QStringList m_filePaths;
    bool m_allowUpscale = false;
    bool m_exporting = false;
    bool m_keepExtension = false;
    QLabel *m_preview = nullptr;
    QPointer<TagPickerDialog> m_picker; // not modal: the preview and the viewer stay usable

    QListWidget *m_tagList;
    QPushButton *m_removeButton;
    QSpinBox *m_ratingSpin;
    QLineEdit *m_nameEdit;
};

#endif // IMAGEMETADIALOG_H
