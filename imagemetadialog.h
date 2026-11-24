#ifndef IMAGEMETADIALOG_H
#define IMAGEMETADIALOG_H

#include <QDialog>
#include <QPointer>
#include <QStringList>

#include "database.h"

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class TagPickerDialog;

// Edits an image's tags and file name, or (several files) the tags they have in common.
// (The rating belongs to the file's collection: CollectionDialog.)
// Nothing is written to the DB here: the caller reads the result after exec() == Accepted.
class ImageMetaDialog : public QDialog
{
    Q_OBJECT

public:
    // exec() returns this when the user confirmed deleting the file (to the Recycle Bin).
    static constexpr int DeleteRequested = 2;

    // One path: single-file mode (preview, file name, delete and export buttons).
    // Several paths: bulk mode; imageTags are the tags common to all of them.
    // galleryTags: all tags of the gallery, group by group (for the tag picker).
    ImageMetaDialog(const QStringList &filePaths, const QStringList &imageTags,
                    const QList<TagSection> &galleryTags, QWidget *parent = nullptr);

    // Bulk mode: tags some of the files have, but not all (the tag picker shows them locked).
    void setPartialTags(const QStringList &tags) { m_partialTags = tags; }
    // Shown after each tag in the tag picker: in how many collections of the gallery it is.
    void setTagCollectionCounts(const QHash<QString, int> &counts) { m_tagCollectionCounts = counts; }

    // Viewer setting of the gallery, for images opened from this dialog.
    void setAllowUpscale(bool allow);

    // On: the "file name" field shows and edits the name without its extension.
    void setKeepExtension(bool keep);

    // While a file export runs the dialog is locked and can't be closed.
    void setExporting(bool exporting);

    // Single image: the file already is its collection's cover (the button is then disabled).
    void setIsCover(bool isCover);
    // "Встановити обкладинкою" was pressed: the caller makes the file the cover after saving.
    bool coverRequested() const { return m_coverRequested; }

    QString filePath() const { return m_filePaths.value(0); } // the first one in bulk mode
    QString fileName() const; // single-file mode: the full name the "file name" field asks for
    QStringList tags() const;
    QStringList addedTags() const;  // compared with the tags the dialog started with
    QStringList removedTags() const;
    QStringList deletedTags() const { return m_deletedTags; } // to delete from the whole gallery

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

    QList<TagSection> m_galleryTags;
    QHash<QString, int> m_tagCollectionCounts;
    QStringList m_partialTags;
    QStringList m_tags;
    QStringList m_initialTags;
    QStringList m_deletedTags;
    QStringList m_filePaths;
    bool m_allowUpscale = false;
    bool m_exporting = false;
    bool m_keepExtension = false;
    bool m_coverRequested = false;
    QPushButton *m_coverButton = nullptr; // single image only
    QLabel *m_preview = nullptr;
    QPointer<TagPickerDialog> m_picker; // not modal: the preview and the viewer stay usable

    QListWidget *m_tagList;
    QPushButton *m_removeButton;
    QLineEdit *m_nameEdit;
};

#endif // IMAGEMETADIALOG_H
