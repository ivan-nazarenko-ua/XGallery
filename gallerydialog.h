#ifndef GALLERYDIALOG_H
#define GALLERYDIALOG_H

#include <QDialog>

#include "database.h"

class QCheckBox;
class QLineEdit;
class QListWidget;
class QPushButton;

// Create a gallery, or edit/delete an existing one; also edits the gallery's tag groups.
class GalleryDialog : public QDialog
{
    Q_OBJECT

public:
    // exec() returns this when the user confirmed deletion.
    static constexpr int DeleteRequested = 2;

    // gallery.id == 0 means "create new". tagGroups: the gallery's groups in their order.
    GalleryDialog(const Gallery &gallery, const QStringList &otherNames,
                  const QList<TagGroup> &tagGroups, QWidget *parent = nullptr);

    QString name() const;
    QString rootFolder() const;
    bool allowUpscale() const;
    // The edited list for Db::saveTagGroups: new groups have id 0, deleted ones are missing.
    QList<TagGroup> tagGroups() const;

public slots:
    void accept() override;

private:
    void browse();
    // Portable mode only allows roots on the program's drive; shows an error otherwise.
    bool checkDrive(const QString &folder);
    void requestDelete();
    void addGroup();
    void renameGroup();
    void deleteGroup();
    void moveGroup(int step); // -1 up, +1 down
    void updateGroupButtons();
    // Asks for a group name; empty when cancelled. Names are unique (case-insensitive) in the list.
    QString askGroupName(const QString &title, const QString &current);

    Gallery m_gallery;
    QStringList m_otherNames;
    QLineEdit *m_nameEdit;
    QLineEdit *m_rootEdit;
    QCheckBox *m_allowUpscaleCheck;
    QListWidget *m_groupList; // item data: TagGroup::id
    QPushButton *m_renameGroupButton;
    QPushButton *m_deleteGroupButton;
    QPushButton *m_upButton;
    QPushButton *m_downButton;
};

#endif // GALLERYDIALOG_H
