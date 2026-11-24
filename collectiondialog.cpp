#include "collectiondialog.h"

#include "collections.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

CollectionDialog::CollectionDialog(const CollectionRecord &collection, const QPixmap &cover, QWidget *parent)
    : QDialog(parent)
    , m_nameEdit(new QLineEdit(collection.name, this))
    , m_ratingSpin(new QSpinBox(this))
{
    setWindowTitle(tr("Властивості колекції"));
    setMinimumWidth(480);

    auto *preview = new QLabel(this);
    preview->setFixedSize(180, 180);
    preview->setAlignment(Qt::AlignCenter);
    if (!cover.isNull())
        preview->setPixmap(cover);

    const QString defaultName = Collections::defaultName(collection.virtualPath);
    auto *nameRow = new QHBoxLayout;
    nameRow->addWidget(m_nameEdit, 1);
    auto *defaultButton = new QPushButton(tr("За замовчуванням"), this);
    defaultButton->setToolTip(tr("Два останні теги колекції через пробіл: «%1»").arg(defaultName));
    connect(defaultButton, &QPushButton::clicked, this, [this, defaultName] { m_nameEdit->setText(defaultName); });
    nameRow->addWidget(defaultButton);

    m_ratingSpin->setRange(Collections::MinRating, Collections::MaxRating);
    m_ratingSpin->setValue(collection.rating);

    auto *tags = new QLabel(collection.virtualPath.isEmpty() ? tr("(файли без тегів)") : collection.virtualPath, this);
    tags->setWordWrap(true);
    tags->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto *form = new QFormLayout;
    form->addRow(tr("Теги:"), tags);
    form->addRow(tr("Файлів:"), new QLabel(QString::number(collection.fileCount), this));
    form->addRow(tr("Ім'я:"), nameRow);
    form->addRow(tr("Оцінка:"), m_ratingSpin);

    auto *top = new QHBoxLayout;
    top->addWidget(preview, 0, Qt::AlignTop);
    top->addLayout(form, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Save)->setText(tr("Зберегти"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Скасувати"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    // All its files at once, after one question (the caller moves them to the Recycle Bin).
    QPushButton *deleteButton = buttons->addButton(tr("Видалити колекцію…"), QDialogButtonBox::DestructiveRole);
    connect(deleteButton, &QPushButton::clicked, this, [this, collection] {
        const QString shown = collection.name.isEmpty() ? collection.virtualPath : collection.name;
        QMessageBox box(QMessageBox::Warning, tr("Видалити колекцію"),
                        tr("Перемістити в Кошик усі файли колекції «%1» (%2) і прибрати їх з галереї?")
                            .arg(shown.isEmpty() ? tr("(файли без тегів)") : shown)
                            .arg(collection.fileCount),
                        QMessageBox::NoButton, this);
        QPushButton *yes = box.addButton(tr("Видалити"), QMessageBox::DestructiveRole);
        QPushButton *no = box.addButton(tr("Скасувати"), QMessageBox::RejectRole);
        box.setDefaultButton(no);
        box.exec();
        if (box.clickedButton() == yes)
            done(DeleteRequested);
    });

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(top, 1);
    layout->addWidget(buttons);
    m_nameEdit->setFocus();
    m_nameEdit->selectAll();
}

QString CollectionDialog::name() const
{
    return m_nameEdit->text().trimmed();
}

int CollectionDialog::rating() const
{
    return m_ratingSpin->value();
}
