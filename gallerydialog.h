#ifndef GALLERYDIALOG_H
#define GALLERYDIALOG_H

#include <QDialog>

#include "database.h"

class QCheckBox;
class QLineEdit;

// Create a gallery, or edit/delete an existing one.
class GalleryDialog : public QDialog
{
    Q_OBJECT

public:
    // exec() returns this when the user confirmed deletion.
    static constexpr int DeleteRequested = 2;

    // gallery.id == 0 means "create new".
    GalleryDialog(const Gallery &gallery, const QStringList &otherNames, QWidget *parent = nullptr);

    QString name() const;
    QString rootFolder() const;
    bool allowUpscale() const;

public slots:
    void accept() override;

private:
    void browse();
    // Portable mode only allows roots on the program's drive; shows an error otherwise.
    bool checkDrive(const QString &folder);
    void requestDelete();

    Gallery m_gallery;
    QStringList m_otherNames;
    QLineEdit *m_nameEdit;
    QLineEdit *m_rootEdit;
    QCheckBox *m_allowUpscaleCheck;
};

#endif // GALLERYDIALOG_H
