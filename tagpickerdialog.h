#ifndef TAGPICKERDIALOG_H
#define TAGPICKERDIALOG_H

#include <QDialog>
#include <QHash>
#include <QList>
#include <QStringList>

#include "database.h"

class QCheckBox;
class QGridLayout;
class QLineEdit;
class QScrollArea;

// Chooses tags for the metadata dialog: a list of new ones and checkboxes of the gallery's tags,
// group by group (a group's name takes a cell before its tags). The file's tags are ticked;
// clearing one takes it off the file. Tags only some files of a bulk edit have are locked.
// Nothing is written to the DB here: the metadata dialog takes the result on accepted().
class TagPickerDialog : public QDialog
{
    Q_OBJECT

public:
    // sections: every tag to show; assignedTags: the file's (or the common) tags;
    // partialTags: bulk edit, tags some of the files have; collectionCounts: shown as "tag (N)",
    // N = collections of the gallery with the tag (0 when absent, e.g. a new tag).
    TagPickerDialog(const QList<TagSection> &sections, const QStringList &assignedTags,
                    const QStringList &partialTags, const QHash<QString, int> &collectionCounts,
                    QWidget *parent = nullptr);

    // The file's tags changed in the metadata dialog meanwhile: boxes the user hasn't touched follow.
    void setAssignedTags(const QStringList &tags);

    // Off: right click can't mark a gallery tag for deletion (only take a new, typed one off the list).
    void setGalleryDeletionAllowed(bool allowed) { m_galleryDeletionAllowed = allowed; }

    QStringList addedTags() const;   // ticked here, the file didn't have them (new tags included)
    QStringList removedTags() const; // the file had them, cleared here
    QStringList deletedTags() const; // gallery tags the user asked to delete everywhere

public slots:
    void accept() override;

private:
    struct Entry
    {
        QString tag;              // empty for a group name
        QWidget *widget = nullptr; // the checkbox, or the group name's label
        QCheckBox *box = nullptr;  // null for a group name
        bool inDatabase = false;   // false: typed here, only leaves this window when removed
        bool initiallyChecked = false; // the file has it: unticking removes it
    };

    Entry makeEntry(const QString &tag, bool inDatabase);
    Entry makeGroupLabel(const QString &name);
    // Shows the file's state of the tag: ticked, cleared or (partial) locked.
    void syncEntry(Entry &entry);
    static bool isTouched(const Entry &entry);
    bool addTagsFromField(); // false: the text has a forbidden character, nothing was added
    void confirmDelete(QCheckBox *box);
    void layoutEntries();
    bool eventFilter(QObject *watched, QEvent *event) override;

    QStringList m_assignedTags;
    QStringList m_partialTags;
    QHash<QString, int> m_collectionCounts;
    QStringList m_deletedTags;
    bool m_galleryDeletionAllowed = true;
    QList<Entry> m_entries; // in display order
    QLineEdit *m_newTagsEdit;
    QScrollArea *m_scrollArea;
    QGridLayout *m_grid;
    int m_columns = 6; // follows the window width (eventFilter)
};

#endif // TAGPICKERDIALOG_H
