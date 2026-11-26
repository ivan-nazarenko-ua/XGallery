#ifndef COLLECTIONDIALOG_H
#define COLLECTIONDIALOG_H

#include <QDialog>
#include <QPixmap>

#include "database.h"

class QLineEdit;
class QSpinBox;

// Properties of a collection: name and rating. Nothing is written to the DB here: the caller
// reads name() and rating() after exec() == Accepted or EditTagsRequested, or deletes the collection's files after
// exec() == DeleteRequested (the user has confirmed it here).
class CollectionDialog : public QDialog
{
    Q_OBJECT

public:
    static constexpr int DeleteRequested = 2;
    // "Редагувати теги…": like Save (the caller saves name and rating), then the caller opens the tag edit of all
    // its files.
    static constexpr int EditTagsRequested = 3;

    // cover: its thumbnail as the main window shows it (may be null); defaultName: what the
    // "За замовчуванням" button puts into the name (Db::defaultCollectionName); canEditTags: "Редагувати теги…" is
    // enabled (false: its only file is one the metadata dialog can't open).
    CollectionDialog(const CollectionRecord &collection, const QPixmap &cover, const QString &defaultName,
                     bool canEditTags, QWidget *parent = nullptr);

    QString name() const; // trimmed; may be empty
    // The user typed the name (or it was so and they didn't touch it); false after "За замовчуванням".
    bool nameManual() const { return m_nameManual; }
    int rating() const;

private:
    bool m_nameManual = false;
    QLineEdit *m_nameEdit;
    QSpinBox *m_ratingSpin;
};

#endif // COLLECTIONDIALOG_H
