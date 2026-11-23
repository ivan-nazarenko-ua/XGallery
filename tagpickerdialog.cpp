#include "tagpickerdialog.h"

#include "database.h"

#include <QCheckBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QVBoxLayout>

#include <algorithm>

namespace {

constexpr int ColumnCount = 6;

} // namespace

TagPickerDialog::TagPickerDialog(const QList<TagSection> &sections, const QStringList &assignedTags,
                                 const QStringList &partialTags, QWidget *parent)
    : QDialog(parent)
    , m_assignedTags(assignedTags)
    , m_partialTags(partialTags)
    , m_newTagsEdit(new QLineEdit(this))
    , m_scrollArea(new QScrollArea(this))
    , m_grid(new QGridLayout)
{
    setWindowTitle(tr("Додати теги"));
    resize(1100, 720);

    auto *newRow = new QHBoxLayout;
    newRow->addWidget(new QLabel(tr("Нові теги:"), this));
    m_newTagsEdit->setPlaceholderText(tr("теги через кому"));
    newRow->addWidget(m_newTagsEdit, 1);
    auto *addButton = new QPushButton(tr("Додати"), this);
    newRow->addWidget(addButton);
    connect(addButton, &QPushButton::clicked, this, &TagPickerDialog::addTagsFromField);
    connect(m_newTagsEdit, &QLineEdit::returnPressed, this, &TagPickerDialog::addTagsFromField);

    auto *content = new QWidget(m_scrollArea);
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->addLayout(m_grid);
    contentLayout->addStretch(1); // few tags stay at the top instead of spreading over the height
    for (int column = 0; column < ColumnCount; ++column)
        m_grid->setColumnStretch(column, 1);
    m_scrollArea->setWidget(content);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    for (const TagSection &section : sections) {
        if (section.groupId != 0)
            m_entries << makeGroupLabel(section.groupName);
        for (const QString &tag : section.tags) {
            Entry entry = makeEntry(tag, true);
            syncEntry(entry);
            m_entries << entry;
        }
    }
    layoutEntries();

    auto *saveButton = new QPushButton(tr("Зберегти"), this);
    auto *cancelButton = new QPushButton(tr("Відмінити"), this);
    connect(saveButton, &QPushButton::clicked, this, &TagPickerDialog::accept);
    connect(cancelButton, &QPushButton::clicked, this, &TagPickerDialog::reject);
    auto *buttons = new QHBoxLayout;
    buttons->addStretch(1);
    buttons->addWidget(saveButton);
    buttons->addWidget(cancelButton);
    buttons->addStretch(1);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(newRow);
    layout->addWidget(m_scrollArea, 1);
    layout->addLayout(buttons);

    // Enter in the "new tags" field must add the tags, not press some button of the dialog.
    for (QPushButton *button : findChildren<QPushButton *>())
        button->setAutoDefault(false);
}

void TagPickerDialog::setAssignedTags(const QStringList &tags)
{
    m_assignedTags = tags;
    QStringList shown;
    for (Entry &entry : m_entries) {
        if (!entry.box)
            continue;
        shown << entry.tag;
        if (!isTouched(entry))
            syncEntry(entry);
    }

    // Tags the file got meanwhile (pasted) that this window doesn't show yet.
    QList<Entry> fresh;
    for (const QString &tag : tags) {
        if (shown.contains(tag))
            continue;
        const bool wasDeleted = m_deletedTags.removeAll(tag) > 0; // given again: not deleted after all
        Entry entry = makeEntry(tag, wasDeleted);
        syncEntry(entry);
        fresh << entry;
    }
    if (!fresh.isEmpty()) {
        m_entries = fresh + m_entries;
        layoutEntries();
    }
}

bool TagPickerDialog::isTouched(const Entry &entry)
{
    return entry.box && entry.box->isEnabled() && entry.box->isChecked() != entry.initiallyChecked;
}

void TagPickerDialog::syncEntry(Entry &entry)
{
    const bool assigned = m_assignedTags.contains(entry.tag);
    const bool partial = !assigned && m_partialTags.contains(entry.tag);
    entry.initiallyChecked = assigned;
    entry.box->setTristate(partial);
    entry.box->setCheckState(partial ? Qt::PartiallyChecked : assigned ? Qt::Checked : Qt::Unchecked);
    entry.box->setEnabled(!partial);
    entry.box->setToolTip(partial ? tr("%1\nЦей тег є лише в частини обраних файлів, тут його не змінити.\n"
                                       "Щоб дати його всім файлам, впиши його в «Нові теги».").arg(entry.tag)
                                  : entry.tag);
}

QStringList TagPickerDialog::addedTags() const
{
    QStringList added;
    for (const Entry &entry : m_entries)
        if (isTouched(entry) && entry.box->isChecked())
            added << entry.tag;
    return added;
}

QStringList TagPickerDialog::removedTags() const
{
    QStringList removed;
    for (const Entry &entry : m_entries)
        if (isTouched(entry) && !entry.box->isChecked())
            removed << entry.tag;
    return removed;
}

QStringList TagPickerDialog::deletedTags() const
{
    return m_deletedTags;
}

void TagPickerDialog::accept()
{
    // Tags typed but not yet added with the button count as well.
    if (addTagsFromField())
        QDialog::accept();
}

TagPickerDialog::Entry TagPickerDialog::makeEntry(const QString &tag, bool inDatabase)
{
    auto *box = new QCheckBox(QString(tag).replace('&', "&&"), m_scrollArea->widget()); // '&' would be a mnemonic
    box->setToolTip(tag);
    // Equal columns: a long name is cut (the tooltip has it) instead of widening its column.
    box->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    box->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(box, &QWidget::customContextMenuRequested, this, [this, box] { confirmDelete(box); });
    Entry entry;
    entry.tag = tag;
    entry.widget = box;
    entry.box = box;
    entry.inDatabase = inDatabase;
    return entry;
}

TagPickerDialog::Entry TagPickerDialog::makeGroupLabel(const QString &name)
{
    auto *label = new QLabel(name, m_scrollArea->widget());
    label->setTextFormat(Qt::PlainText);
    QFont font = label->font();
    font.setBold(true);
    label->setFont(font);
    label->setToolTip(tr("Група тегів «%1»").arg(name));
    label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    Entry entry;
    entry.widget = label;
    return entry;
}

bool TagPickerDialog::addTagsFromField()
{
    QStringList tags;
    const QStringList parts = m_newTagsEdit->text().split(',', Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        const QString tag = Db::normalizeTag(part);
        if (tag.isEmpty() || tags.contains(tag))
            continue;
        if (!Db::isValidTagName(tag)) {
            QMessageBox::warning(this, windowTitle(), tr("Назва тегу не може містити символи «,», «<», «>», «/» і «\\»."));
            return false;
        }
        tags << tag;
    }
    m_newTagsEdit->clear();

    QList<Entry> fresh;
    QCheckBox *lastExisting = nullptr;
    for (const QString &tag : std::as_const(tags)) {
        const auto found = std::find_if(m_entries.cbegin(), m_entries.cend(),
                                        [&tag](const Entry &entry) { return entry.box && entry.tag == tag; });
        if (found != m_entries.cend()) {
            // A locked partial tag typed here goes to all the files.
            found->box->setTristate(false);
            found->box->setEnabled(true);
            found->box->setToolTip(tag);
            found->box->setChecked(true);
            lastExisting = found->box;
        } else {
            // Typing a tag marked for deletion takes the deletion back.
            const bool wasDeleted = m_deletedTags.removeAll(tag) > 0;
            Entry entry = makeEntry(tag, wasDeleted);
            entry.initiallyChecked = m_assignedTags.contains(tag);
            entry.box->setChecked(true);
            fresh << entry;
        }
    }

    if (!fresh.isEmpty()) {
        // First in the table: in alphabetical order they could land far below the visible part.
        m_entries = fresh + m_entries;
        layoutEntries();
        m_scrollArea->verticalScrollBar()->setValue(0);
    } else if (lastExisting) {
        m_scrollArea->ensureWidgetVisible(lastExisting);
    }
    return true;
}

void TagPickerDialog::confirmDelete(QCheckBox *box)
{
    const auto index = std::distance(m_entries.cbegin(),
                                     std::find_if(m_entries.cbegin(), m_entries.cend(),
                                                  [box](const Entry &entry) { return entry.box && entry.box == box; }));
    if (index == m_entries.size())
        return;
    const Entry entry = m_entries.at(index);
    if (entry.inDatabase && !m_galleryDeletionAllowed)
        return;

    const QString text = entry.inDatabase
        ? tr("Ви впевнені, що хочете видалити тег «%1» з усіх файлів галереї?\n\n"
             "Видалення відбудеться після збереження у вікні метаданих.").arg(entry.tag)
        : tr("Прибрати новий тег «%1» зі списку?").arg(entry.tag);
    QMessageBox question(QMessageBox::Warning, tr("Видалити тег"), text, QMessageBox::NoButton, this);
    QPushButton *yes = question.addButton(tr("Видалити"), QMessageBox::DestructiveRole);
    QPushButton *no = question.addButton(tr("Скасувати"), QMessageBox::RejectRole);
    question.setDefaultButton(no);
    question.exec();
    if (question.clickedButton() != yes)
        return;

    if (entry.inDatabase)
        m_deletedTags << entry.tag;
    m_entries.removeAt(index);
    entry.widget->hide();
    entry.widget->deleteLater(); // we are inside a signal of this very widget
    layoutEntries();
}

void TagPickerDialog::layoutEntries()
{
    while (QLayoutItem *item = m_grid->takeAt(0))
        delete item; // the widget stays, only its place in the grid goes
    int cell = 0;
    for (int i = 0; i < m_entries.size(); ++i) {
        const Entry &entry = m_entries.at(i);
        if (!entry.box) {
            // A group's name only while some tag of the group follows it.
            const bool hasTags = i + 1 < m_entries.size() && m_entries.at(i + 1).box;
            entry.widget->setVisible(hasTags);
            if (!hasTags)
                continue;
        }
        m_grid->addWidget(entry.widget, cell / ColumnCount, cell % ColumnCount);
        ++cell;
    }
}
