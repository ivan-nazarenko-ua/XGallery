#include "gallerydialog.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

GalleryDialog::GalleryDialog(const Gallery &gallery, const QStringList &otherNames, QWidget *parent)
    : QDialog(parent)
    , m_gallery(gallery)
    , m_otherNames(otherNames)
    , m_nameEdit(new QLineEdit(gallery.name, this))
    , m_rootEdit(new QLineEdit(QDir::toNativeSeparators(gallery.rootFolder), this))
{
    const bool creating = gallery.id == 0;
    setWindowTitle(creating ? tr("Нова галерея") : tr("Редагування галереї"));
    setMinimumWidth(520);

    auto *rootRow = new QHBoxLayout;
    rootRow->addWidget(m_rootEdit, 1);
    auto *browseButton = new QPushButton(tr("Обрати…"), this);
    connect(browseButton, &QPushButton::clicked, this, &GalleryDialog::browse);
    rootRow->addWidget(browseButton);

    auto *form = new QFormLayout;
    form->addRow(tr("Назва:"), m_nameEdit);
    form->addRow(tr("Коренева папка:"), rootRow);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(creating ? tr("Створити") : tr("Зберегти"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Скасувати"));
    connect(buttons, &QDialogButtonBox::accepted, this, &GalleryDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &GalleryDialog::reject);
    if (!creating) {
        // DestructiveRole doesn't close the dialog by itself; we ask for confirmation first.
        QPushButton *deleteButton = buttons->addButton(tr("Видалити галерею…"), QDialogButtonBox::DestructiveRole);
        connect(deleteButton, &QPushButton::clicked, this, &GalleryDialog::requestDelete);
    }

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addStretch();
    layout->addWidget(buttons);
}

QString GalleryDialog::name() const
{
    return m_nameEdit->text().trimmed();
}

QString GalleryDialog::rootFolder() const
{
    const QString text = m_rootEdit->text().trimmed();
    return text.isEmpty() ? QString() : QDir::cleanPath(QDir::fromNativeSeparators(text));
}

void GalleryDialog::browse()
{
    const QString current = rootFolder();
    const QString folder = QFileDialog::getExistingDirectory(
        this, tr("Обери кореневу папку галереї"),
        current.isEmpty() ? QDir::homePath() : current);
    if (folder.isEmpty())
        return;
    m_rootEdit->setText(QDir::toNativeSeparators(QDir::cleanPath(folder)));
    if (m_nameEdit->text().trimmed().isEmpty())
        m_nameEdit->setText(QFileInfo(folder).fileName());
}

void GalleryDialog::accept()
{
    if (name().isEmpty()) {
        QMessageBox::warning(this, windowTitle(), tr("Вкажи назву галереї."));
        m_nameEdit->setFocus();
        return;
    }
    if (m_otherNames.contains(name(), Qt::CaseInsensitive)) {
        QMessageBox::warning(this, windowTitle(), tr("Галерея з назвою «%1» вже існує.").arg(name()));
        m_nameEdit->setFocus();
        return;
    }
    if (rootFolder().isEmpty() || !QFileInfo(rootFolder()).isDir()) {
        QMessageBox::warning(this, windowTitle(), tr("Обери наявну кореневу папку."));
        m_rootEdit->setFocus();
        return;
    }
    QDialog::accept();
}

void GalleryDialog::requestDelete()
{
    QMessageBox box(QMessageBox::Warning, tr("Видалити галерею"),
                    tr("Видалити галерею «%1»?").arg(m_gallery.name),
                    QMessageBox::NoButton, this);
    box.setInformativeText(tr("Усю інформацію про цю галерею (зображення, теги, хеші) буде видалено з бази.\n"
                              "Файли зображень на диску залишаться без змін."));
    QPushButton *yes = box.addButton(tr("Видалити"), QMessageBox::DestructiveRole);
    QPushButton *no = box.addButton(tr("Скасувати"), QMessageBox::RejectRole);
    box.setDefaultButton(no);
    box.exec();
    if (box.clickedButton() == yes)
        done(DeleteRequested);
}
