#ifndef TAGPICKERDIALOG_H
#define TAGPICKERDIALOG_H

#include <QDialog>
#include <QList>
#include <QStringList>

class QCheckBox;
class QGridLayout;
class QLineEdit;
class QScrollArea;

// Chooses tags for the metadata dialog: a list of new ones and checkboxes of the gallery's tags.
// Nothing is written to the DB here: the metadata dialog takes the result on accepted().
class TagPickerDialog : public QDialog
{
    Q_OBJECT

public:
    // availableTags get a checkbox each; assignedTags (already on the file) are ignored when typed.
    TagPickerDialog(const QStringList &availableTags, const QStringList &assignedTags,
                    QWidget *parent = nullptr);

    void setAssignedTags(const QStringList &tags) { m_assignedTags = tags; }

    QStringList chosenTags() const;  // ticked checkboxes, new tags included
    QStringList deletedTags() const; // gallery tags the user asked to delete everywhere

public slots:
    void accept() override;

private:
    struct Entry
    {
        QString tag;
        QCheckBox *box = nullptr;
        bool inDatabase = false; // false: typed here, only leaves this window when removed
    };

    Entry makeEntry(const QString &tag, bool inDatabase, bool checked);
    bool addTagsFromField(); // false: the text has a forbidden character, nothing was added
    void confirmDelete(QCheckBox *box);
    void layoutEntries();

    QStringList m_assignedTags;
    QStringList m_deletedTags;
    QList<Entry> m_entries; // in display order
    QLineEdit *m_newTagsEdit;
    QScrollArea *m_scrollArea;
    QGridLayout *m_grid;
};

#endif // TAGPICKERDIALOG_H
