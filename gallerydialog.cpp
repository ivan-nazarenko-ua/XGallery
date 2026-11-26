#include "gallerydialog.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

GalleryDialog::GalleryDialog(const Gallery &gallery, const QStringList &otherNames,
                             const QList<TagGroup> &tagGroups, QWidget *parent)
    : QDialog(parent)
    , m_gallery(gallery)
    , m_otherNames(otherNames)
    , m_nameEdit(new QLineEdit(gallery.name, this))
    , m_rootEdit(new QLineEdit(QDir::toNativeSeparators(gallery.rootFolder), this))
    , m_allowUpscaleCheck(new QCheckBox(tr("Дозволити збільшення зображень"), this))
    , m_groupList(new QListWidget(this))
    , m_renameGroupButton(new QPushButton(tr("Перейменувати…"), this))
    , m_deleteGroupButton(new QPushButton(tr("Видалити"), this))
    , m_upButton(new QPushButton(tr("Вгору"), this))
    , m_downButton(new QPushButton(tr("Вниз"), this))
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

    // Tag groups: the main window's tag list shows tags without a group first, then the groups in this order.
    for (const TagGroup &group : tagGroups) {
        auto *item = new QListWidgetItem(group.name, m_groupList);
        item->setData(Qt::UserRole, group.id);
    }
    m_groupList->setToolTip(tr("У списку тегів головного вікна спершу йдуть теги без групи,\n"
                               "потім групи в цьому порядку. Тег до групи додають правим кліком у тому списку."));
    connect(m_groupList, &QListWidget::currentRowChanged, this, &GalleryDialog::updateGroupButtons);
    connect(m_groupList, &QListWidget::itemDoubleClicked, this, &GalleryDialog::renameGroup);
    auto *addGroupButton = new QPushButton(tr("Додати…"), this);
    connect(addGroupButton, &QPushButton::clicked, this, &GalleryDialog::addGroup);
    connect(m_renameGroupButton, &QPushButton::clicked, this, &GalleryDialog::renameGroup);
    connect(m_deleteGroupButton, &QPushButton::clicked, this, &GalleryDialog::deleteGroup);
    connect(m_upButton, &QPushButton::clicked, this, [this] { moveGroup(-1); });
    connect(m_downButton, &QPushButton::clicked, this, [this] { moveGroup(1); });
    auto *groupButtons = new QVBoxLayout;
    for (QPushButton *button : {addGroupButton, m_renameGroupButton, m_deleteGroupButton, m_upButton, m_downButton})
        groupButtons->addWidget(button);
    groupButtons->addStretch();
    auto *groupBox = new QGroupBox(tr("Групи тегів"), this);
    auto *groupLayout = new QHBoxLayout(groupBox);
    groupLayout->addWidget(m_groupList, 1);
    groupLayout->addLayout(groupButtons);
    updateGroupButtons();

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(groupBox, 1);
    layout->addWidget(buttons);
}

QList<TagGroup> GalleryDialog::tagGroups() const
{
    QList<TagGroup> groups;
    for (int i = 0; i < m_groupList->count(); ++i) {
        const QListWidgetItem *item = m_groupList->item(i);
        groups.append({item->data(Qt::UserRole).toLongLong(), item->text()});
    }
    return groups;
}

void GalleryDialog::updateGroupButtons()
{
    const int row = m_groupList->currentRow();
    m_renameGroupButton->setEnabled(row >= 0);
    m_deleteGroupButton->setEnabled(row >= 0);
    m_upButton->setEnabled(row > 0);
    m_downButton->setEnabled(row >= 0 && row < m_groupList->count() - 1);
}

QString GalleryDialog::askGroupName(const QString &title, const QString &current)
{
    QString text = current;
    for (;;) {
        bool ok = false;
        text = QInputDialog::getText(this, title, tr("Назва групи:"), QLineEdit::Normal, text, &ok).simplified();
        if (!ok || text.isEmpty())
            return {};
        bool taken = false;
        for (int i = 0; i < m_groupList->count(); ++i) {
            const QString other = m_groupList->item(i)->text();
            if (other != current && other.compare(text, Qt::CaseInsensitive) == 0)
                taken = true;
        }
        if (!taken)
            return text;
        QMessageBox::warning(this, title, tr("Група «%1» вже є.").arg(text));
    }
}

void GalleryDialog::addGroup()
{
    const QString name = askGroupName(tr("Нова група"), QString());
    if (name.isEmpty())
        return;
    auto *item = new QListWidgetItem(name, m_groupList);
    item->setData(Qt::UserRole, qint64(0));
    m_groupList->setCurrentItem(item);
}

void GalleryDialog::renameGroup()
{
    QListWidgetItem *item = m_groupList->currentItem();
    if (!item)
        return;
    const QString name = askGroupName(tr("Перейменувати групу"), item->text());
    if (!name.isEmpty())
        item->setText(name);
}

void GalleryDialog::deleteGroup()
{
    // Takes effect on "Save" only: the group's tags then stay without a group.
    const int row = m_groupList->currentRow();
    delete m_groupList->takeItem(row);
    // The group now in its place is selected, the last one if the last was deleted (owner's rule: repeated clicks
    // delete a run).
    if (m_groupList->count() > 0)
        m_groupList->setCurrentRow(qMin(row, m_groupList->count() - 1));
    updateGroupButtons();
}

void GalleryDialog::moveGroup(int step)
{
    const int row = m_groupList->currentRow();
    const int target = row + step;
    if (row < 0 || target < 0 || target >= m_groupList->count())
        return;
    QListWidgetItem *item = m_groupList->takeItem(row);
    m_groupList->insertItem(target, item);
    m_groupList->setCurrentItem(item);
    updateGroupButtons(); // the row number may be the same as before the move
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
