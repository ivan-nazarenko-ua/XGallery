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

TagPickerDialog::TagPickerDialog(const QStringList &availableTags, const QStringList &assignedTags, QWidget *parent)
    : QDialog(parent)
    , m_assignedTags(assignedTags)
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

    for (const QString &tag : availableTags)
        m_entries << makeEntry(tag, true, false);
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

QStringList TagPickerDialog::chosenTags() const
{
    QStringList chosen;
    for (const Entry &entry : m_entries)
        if (entry.box->isChecked())
            chosen << entry.tag;
    return chosen;
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

TagPickerDialog::Entry TagPickerDialog::makeEntry(const QString &tag, bool inDatabase, bool checked)
{
    auto *box = new QCheckBox(QString(tag).replace('&', "&&"), m_scrollArea->widget()); // '&' would be a mnemonic
    box->setChecked(checked);
    box->setToolTip(tag);
    // Equal columns: a long name is cut (the tooltip has it) instead of widening its column.
    box->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    box->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(box, &QWidget::customContextMenuRequested, this, [this, box] { confirmDelete(box); });
    return {tag, box, inDatabase};
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
        if (m_assignedTags.contains(tag))
            continue;
        const auto found = std::find_if(m_entries.cbegin(), m_entries.cend(),
                                        [&tag](const Entry &entry) { return entry.tag == tag; });
        if (found != m_entries.cend()) {
            found->box->setChecked(true);
            lastExisting = found->box;
        } else {
            // Typing a tag marked for deletion takes the deletion back.
            const bool wasDeleted = m_deletedTags.removeAll(tag) > 0;
            fresh << makeEntry(tag, wasDeleted, true);
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
                                                  [box](const Entry &entry) { return entry.box == box; }));
    if (index == m_entries.size())
        return;
    const Entry entry = m_entries.at(index);

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
    entry.box->hide();
    entry.box->deleteLater(); // we are inside a signal of this very widget
    layoutEntries();
}

void TagPickerDialog::layoutEntries()
{
    while (QLayoutItem *item = m_grid->takeAt(0))
        delete item; // the widget stays, only its place in the grid goes
    for (int i = 0; i < m_entries.size(); ++i)
        m_grid->addWidget(m_entries.at(i).box, i / ColumnCount, i % ColumnCount);
}
