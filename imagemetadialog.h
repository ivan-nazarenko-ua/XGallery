#ifndef IMAGEMETADIALOG_H
#define IMAGEMETADIALOG_H

#include <QDialog>
#include <QStringList>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;

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

    QStringList tags() const;
    QStringList addedTags() const;   // compared with the tags the dialog started with
    QStringList removedTags() const;
    int rating() const;

signals:
    // Double click on a tag: put it into the main window's search ("Містить").
    void tagActivated(const QString &tag);
    // A viewer opened from here rewrote the file (saved rotation).
    void fileChanged(const QString &path);

public slots:
    void done(int result) override;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void addTag(const QString &tag);
    void addExistingTag();
    void addNewTag();
    void removeSelectedTags();
    void copySelectedTags();
    void pasteTags();
    void refresh();
    void updatePreview();
    void openFile(int index); // click on the preview / on a row of the bulk list

    QStringList m_galleryTags;
    QStringList m_tags;
    QStringList m_initialTags;
    QStringList m_filePaths;
    bool m_allowUpscale = false;
    QLabel *m_preview = nullptr;

    QListWidget *m_tagList;
    QPushButton *m_removeButton;
    QComboBox *m_existingCombo;
    QPushButton *m_addExistingButton;
    QLineEdit *m_newTagEdit;
    QSpinBox *m_ratingSpin;
};

#endif // IMAGEMETADIALOG_H
