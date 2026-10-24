#include "gallerydialog.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
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
    , m_allowUpscaleCheck(new QCheckBox(tr("Дозволити збільшення зображень"), this))
{
    m_allowUpscaleCheck->setChecked(gallery.allowUpscale);
    m_allowUpscaleCheck->setToolTip(tr("У вікні перегляду маленькі зображення розтягуватимуться під розмір вікна.\n"
                                       "Якщо вимкнено — зображення не буде більшим за оригінал."));
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
    if (Db::isPortable()) {
        auto *hint = new QLabel(tr("Портабл-режим: коренева папка має бути на диску %1, де лежить програма.")
                                    .arg(Db::programDrive()),
                                this);
        hint->setWordWrap(true);
        QFont font = hint->font();
        font.setItalic(true);
        hint->setFont(font);
        form->addRow(QString(), hint);
    }
    form->addRow(QString(), m_allowUpscaleCheck);

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

bool GalleryDialog::allowUpscale() const
{
    return m_allowUpscaleCheck->isChecked();
}

void GalleryDialog::browse()
{
    QString start = rootFolder();
    if (Db::isPortable() && (start.isEmpty() || !Db::isOnProgramDrive(start)))
        start = QCoreApplication::applicationDirPath();
    else if (start.isEmpty())
        start = QDir::homePath();

    const QString folder = QFileDialog::getExistingDirectory(this, tr("Обери кореневу папку галереї"), start);
    if (folder.isEmpty() || !checkDrive(folder))
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
    // An existing root on another drive (e.g. a DB copied from AppData) stays editable;
    // only a newly chosen root must be on the program's drive.
    const bool rootChanged = QDir::cleanPath(rootFolder()).compare(QDir::cleanPath(m_gallery.rootFolder),
                                                                    Qt::CaseInsensitive) != 0;
    if (rootChanged && !checkDrive(rootFolder())) {
        m_rootEdit->setFocus();
        return;
    }
    QDialog::accept();
}

bool GalleryDialog::checkDrive(const QString &folder)
{
    if (!Db::isPortable() || Db::isOnProgramDrive(folder))
        return true;
    QMessageBox::warning(this, windowTitle(),
                         tr("Програма працює в портабл-режимі (база даних лежить поруч з exe), тому "
                            "коренева папка має бути на тому ж диску, що й програма: %1\n\n"
                            "Обрана папка: %2")
                             .arg(Db::programDrive(), QDir::toNativeSeparators(folder)));
    return false;
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
