#ifndef IMAGEMETADIALOG_H
#define IMAGEMETADIALOG_H

#include <QDialog>
#include <QStringList>

class QComboBox;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;

// Edits an image's tags and rating. Nothing is written to the DB here:
// the caller reads tags()/rating() after exec() == Accepted ("Зберегти").
class ImageMetaDialog : public QDialog
{
    Q_OBJECT

public:
    ImageMetaDialog(const QString &filePath, const QStringList &imageTags, int rating,
                    const QStringList &galleryTags, QWidget *parent = nullptr);

    QStringList tags() const;
    int rating() const;

private:
    void addTag(const QString &tag);
    void addExistingTag();
    void addNewTag();
    void removeSelectedTags();
    void refresh();

    QStringList m_galleryTags;
    QStringList m_tags;

    QListWidget *m_tagList;
    QPushButton *m_removeButton;
    QComboBox *m_existingCombo;
    QPushButton *m_addExistingButton;
    QLineEdit *m_newTagEdit;
    QSpinBox *m_ratingSpin;
};

#endif // IMAGEMETADIALOG_H
