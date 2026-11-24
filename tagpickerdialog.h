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
// Nothing is written to the DB here: the caller takes the result on accepted() and writes the gallery-wide
// part (deleted tags, group changes) at once.
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

    // Turns on the "Група" submenu of the right-click menu. groups: all groups of the gallery, by position;
    // pending: group changes chosen earlier and not saved yet (tag → group, 0 = none), shown as made.
    void setTagGroups(const QList<TagGroup> &groups, const QHash<QString, qint64> &pending = {});

    QStringList addedTags() const;   // ticked here, the file didn't have them (new tags included)
    QStringList removedTags() const; // the file had them, cleared here
    QStringList deletedTags() const; // gallery tags the user asked to delete everywhere
    // Tags shown in another group than the sections given to the constructor: tag → group (0 = none).
    QHash<QString, qint64> groupChanges() const;

public slots:
    void accept() override;

private:
    struct Entry
    {
        QString tag;
        QCheckBox *box = nullptr;
        bool inDatabase = false;   // false: typed here, only leaves this window when removed
        bool initiallyChecked = false; // the file has it: unticking removes it
        qint64 groupId = 0;         // the group it is shown in (0 = none)
        qint64 originalGroupId = 0; // the group it came with
    };

    Entry makeEntry(const QString &tag, bool inDatabase);
    QWidget *groupLabel(qint64 groupId); // made on first use
    // Shows the file's state of the tag: ticked, cleared or (partial) locked.
    void syncEntry(Entry &entry);
    static bool isTouched(const Entry &entry);
    bool addTagsFromField(); // false: the text has a forbidden character, nothing was added
    qsizetype indexOf(const QCheckBox *box) const;
    void showTagMenu(QCheckBox *box, const QPoint &pos);
    void confirmDelete(QCheckBox *box);
    void moveToGroup(QCheckBox *box, qint64 groupId);
    // Inserts the entry into its group: groups in their order, the tags of one group alphabetically.
    void insertEntry(const Entry &entry);
    int groupRank(qint64 groupId) const; // -1 = no group
    void layoutEntries();
    bool eventFilter(QObject *watched, QEvent *event) override;

    QStringList m_assignedTags;
    QStringList m_partialTags;
    QHash<QString, int> m_collectionCounts;
    QStringList m_deletedTags;
    bool m_groupsEditable = false; // setTagGroups was called
    QList<qint64> m_groupOrder;           // group ids by position
    QHash<qint64, QString> m_groupNames;
    QHash<qint64, QWidget *> m_groupLabels;
    QList<Entry> m_entries; // the tags in display order: no group first, then group by group
    QLineEdit *m_newTagsEdit;
    QScrollArea *m_scrollArea;
    QGridLayout *m_grid;
    int m_columns = 6; // follows the window width (eventFilter)
};

#endif // TAGPICKERDIALOG_H
