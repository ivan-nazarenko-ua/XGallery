#ifndef DUPLICATESDIALOG_H
#define DUPLICATESDIALOG_H

#include <QDialog>

#include "database.h"

class QLabel;
class QPushButton;
class QTreeWidget;

// Files of a gallery with the same sha256, one group per hash, as checkboxes.
// "Видалити" moves the checked files to the Recycle Bin and removes them from the DB;
// if a whole group is checked, its first file is kept.
class DuplicatesDialog : public QDialog
{
    Q_OBJECT

public:
    explicit DuplicatesDialog(const Gallery &gallery, QWidget *parent = nullptr);

    // True if anything was deleted (the main window must reload).
    bool changed() const { return m_changed; }

private:
    void reload();
    void updateButtons();
    void deleteChecked();

    Gallery m_gallery;
    bool m_changed = false;
    QTreeWidget *m_tree;
    QLabel *m_summary;
    QPushButton *m_deleteButton;
};

#endif // DUPLICATESDIALOG_H
