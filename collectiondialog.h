#ifndef COLLECTIONDIALOG_H
#define COLLECTIONDIALOG_H

#include <QDialog>
#include <QPixmap>

#include "database.h"

class QLineEdit;
class QSpinBox;

// Properties of a collection: name and rating. Nothing is written to the DB here: the caller
// reads name() and rating() after exec() == Accepted, or deletes the collection's files after
// exec() == DeleteRequested (the user has confirmed it here).
class CollectionDialog : public QDialog
{
    Q_OBJECT

public:
    static constexpr int DeleteRequested = 2;

    // cover: its thumbnail as the main window shows it (may be null); defaultName: what the
    // "За замовчуванням" button puts into the name (Db::defaultCollectionName).
    CollectionDialog(const CollectionRecord &collection, const QPixmap &cover, const QString &defaultName,
                     QWidget *parent = nullptr);

    QString name() const; // trimmed; may be empty
    int rating() const;

private:
    QLineEdit *m_nameEdit;
    QSpinBox *m_ratingSpin;
};

#endif // COLLECTIONDIALOG_H
