#include "tagpickerdialog.h"

#include "database.h"
#include "wrappedtext.h"

#include <QActionGroup>
#include <QCheckBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyleOptionButton>
#include <QTextLayout>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {

constexpr int MinColumnWidth = 150; // as many columns as fit, each at least this wide

// The height of text wrapped in lines of the given width (as the main window's tag list does it).
qreal wrappedHeight(const QString &text, const QFont &font, int width)
{
    QTextLayout layout(text, font);
    return WrappedText::layout(layout, qMax(1, width));
}

// A size policy for equal columns (a long name never widens its column) whose height follows the width.
QSizePolicy wrappingPolicy()
{
    QSizePolicy policy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    policy.setHeightForWidth(true);
    return policy;
}

// A checkbox whose text wraps onto more lines instead of being cut. The box stands beside the first line;
// the whole widget, text included, is clickable. The text is plain (no '&' mnemonic).
class WrappingCheckBox : public QCheckBox
{
public:
    WrappingCheckBox(const QString &text, QWidget *parent)
        : QCheckBox(parent)
        , m_text(text)
    {
        setSizePolicy(wrappingPolicy());
    }

    bool hasHeightForWidth() const override { return true; }

    int heightForWidth(int width) const override
    {
        const int textHeight = int(std::ceil(wrappedHeight(m_text, font(), width - textLeft())));
        return qMax(indicatorSize().height(), textHeight);
    }

    QSize sizeHint() const override
    {
        const int width = textLeft() + fontMetrics().horizontalAdvance(m_text);
        return QSize(width, heightForWidth(width));
    }

    QSize minimumSizeHint() const override { return indicatorSize(); }

protected:
    bool hitButton(const QPoint &pos) const override { return rect().contains(pos); }

    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        QTextLayout layout(m_text, font());
        const qreal textHeight = WrappedText::layout(layout, qMax(1, width() - textLeft()));
        const QSize box = indicatorSize();
        const qreal firstLine = layout.lineCount() > 0 ? layout.lineAt(0).height() : box.height();

        QStyleOptionButton option;
        initStyleOption(&option);
        option.rect = QRect(QPoint(0, qMax(0, int(std::round((firstLine - box.height()) / 2)))), box);
        style()->drawPrimitive(QStyle::PE_IndicatorCheckBox, &option, &painter, this);

        const QPointF textTopLeft(textLeft(), qMax(0.0, (box.height() - firstLine) / 2));
        painter.setPen(palette().color(isEnabled() ? QPalette::Active : QPalette::Disabled, QPalette::WindowText));
        layout.draw(&painter, textTopLeft);

        if (option.state & QStyle::State_HasFocus) {
            QStyleOptionFocusRect focus;
            focus.initFrom(this);
            focus.rect = QRectF(textTopLeft, QSizeF(layout.boundingRect().width(), textHeight))
                             .toAlignedRect()
                             .adjusted(-1, 0, 1, 0)
                             .intersected(rect());
            style()->drawPrimitive(QStyle::PE_FrameFocusRect, &focus, &painter, this);
        }
    }

private:
    QSize indicatorSize() const
    {
        return QSize(style()->pixelMetric(QStyle::PM_IndicatorWidth, nullptr, this),
                     style()->pixelMetric(QStyle::PM_IndicatorHeight, nullptr, this));
    }

    int textLeft() const
    {
        return indicatorSize().width() + style()->pixelMetric(QStyle::PM_CheckBoxLabelSpacing, nullptr, this);
    }

    QString m_text;
};

// A label whose text wraps like WrappingCheckBox's (QLabel can't break a word longer than a line).
class WrappingLabel : public QWidget
{
public:
    WrappingLabel(const QString &text, QWidget *parent)
        : QWidget(parent)
        , m_text(text)
    {
        setSizePolicy(wrappingPolicy());
    }

    bool hasHeightForWidth() const override { return true; }

    int heightForWidth(int width) const override
    {
        return qMax(fontMetrics().height(), int(std::ceil(wrappedHeight(m_text, font(), width))));
    }

    QSize sizeHint() const override
    {
        const int width = fontMetrics().horizontalAdvance(m_text);
        return QSize(width, heightForWidth(width));
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        QTextLayout layout(m_text, font());
        WrappedText::layout(layout, qMax(1, width()));
        painter.setPen(palette().color(QPalette::WindowText));
        layout.draw(&painter, QPointF(0, 0));
    }

private:
    QString m_text;
};

} // namespace

TagPickerDialog::TagPickerDialog(const QList<TagSection> &sections, const QStringList &assignedTags,
                                 const QStringList &partialTags, const QHash<QString, int> &collectionCounts,
                                 QWidget *parent)
    : QDialog(parent)
    , m_assignedTags(assignedTags)
    , m_partialTags(partialTags)
    , m_collectionCounts(collectionCounts)
    , m_newTagsEdit(new QLineEdit(this))
    , m_scrollArea(new QScrollArea(this))
    , m_grid(new QGridLayout)
{
    setWindowTitle(tr("Додати теги"));
    resize(800, 600); // as the main window starts

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

    m_scrollArea->setWidget(content);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->installEventFilter(this); // its width sets the number of columns

    for (const TagSection &section : sections) {
        if (section.groupId != 0) {
            m_groupOrder << section.groupId;
            m_groupNames.insert(section.groupId, section.groupName);
        }
        for (const QString &tag : section.tags) {
            Entry entry = makeEntry(tag, true);
            entry.groupId = entry.originalGroupId = section.groupId;
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
    return entry.box->isEnabled() && entry.box->isChecked() != entry.initiallyChecked;
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

QHash<QString, qint64> TagPickerDialog::groupChanges() const
{
    QHash<QString, qint64> changes;
    for (const Entry &entry : m_entries)
        if (entry.groupId != entry.originalGroupId)
            changes.insert(entry.tag, entry.groupId);
    return changes;
}

void TagPickerDialog::setTagGroups(const QList<TagGroup> &groups, const QHash<QString, qint64> &pending)
{
    m_groupsEditable = true;
    m_groupOrder.clear();
    for (const TagGroup &group : groups) {
        m_groupOrder << group.id;
        m_groupNames.insert(group.id, group.name);
    }
    bool moved = false;
    for (auto it = pending.cbegin(); it != pending.cend(); ++it) {
        const qsizetype index = std::distance(m_entries.cbegin(),
                                              std::find_if(m_entries.cbegin(), m_entries.cend(),
                                                           [&it](const Entry &entry) { return entry.tag == it.key(); }));
        if (index == m_entries.size() || (it.value() != 0 && !m_groupOrder.contains(it.value())))
            continue; // the tag isn't shown, or its group is gone
        Entry entry = m_entries.takeAt(index);
        entry.groupId = it.value();
        insertEntry(entry);
        moved = true;
    }
    if (moved)
        layoutEntries();
}

void TagPickerDialog::accept()
{
    // Tags typed but not yet added with the button count as well.
    if (addTagsFromField())
        QDialog::accept();
}

TagPickerDialog::Entry TagPickerDialog::makeEntry(const QString &tag, bool inDatabase)
{
    // Equal columns: a long name wraps onto more lines instead of widening its column.
    auto *box = new WrappingCheckBox(QStringLiteral("%1 (%2)").arg(tag).arg(m_collectionCounts.value(tag)),
                                     m_scrollArea->widget());
    box->setToolTip(tag);
    box->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(box, &QWidget::customContextMenuRequested, this, [this, box](const QPoint &pos) { showTagMenu(box, pos); });
    Entry entry;
    entry.tag = tag;
    entry.box = box;
    entry.inDatabase = inDatabase;
    return entry;
}

QWidget *TagPickerDialog::groupLabel(qint64 groupId)
{
    QWidget *&label = m_groupLabels[groupId];
    if (!label) {
        const QString name = m_groupNames.value(groupId);
        label = new WrappingLabel(name, m_scrollArea->widget());
        QFont font = label->font();
        font.setBold(true);
        label->setFont(font);
        label->setToolTip(tr("Група тегів «%1»").arg(name));
    }
    return label;
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
                                        [&tag](const Entry &entry) { return entry.tag == tag; });
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

qsizetype TagPickerDialog::indexOf(const QCheckBox *box) const
{
    for (qsizetype i = 0; i < m_entries.size(); ++i)
        if (m_entries.at(i).box == box)
            return i;
    return -1;
}

void TagPickerDialog::showTagMenu(QCheckBox *box, const QPoint &pos)
{
    const qsizetype index = indexOf(box);
    if (index < 0)
        return;
    const Entry &entry = m_entries.at(index);

    QMenu menu(this);
    menu.addAction(tr("Видалити…"), this, [this, box] { confirmDelete(box); });

    if (m_groupsEditable) {
        // As in the main window's tag list: one exclusive choice, the group the tag is shown in ticked.
        QMenu *groupMenu = menu.addMenu(tr("Група"));
        auto *choices = new QActionGroup(groupMenu);
        const auto addChoice = [&](const QString &text, qint64 groupId) {
            QAction *action = groupMenu->addAction(text);
            action->setCheckable(true);
            action->setChecked(groupId == entry.groupId);
            choices->addAction(action);
            connect(action, &QAction::triggered, this, [this, box, groupId] { moveToGroup(box, groupId); });
        };
        addChoice(tr("Без групи"), 0);
        if (!m_groupOrder.isEmpty())
            groupMenu->addSeparator();
        for (const qint64 groupId : std::as_const(m_groupOrder))
            addChoice(QString(m_groupNames.value(groupId)).replace('&', "&&"), groupId); // '&' would be a mnemonic
    }
    menu.exec(box->mapToGlobal(pos));
}

void TagPickerDialog::moveToGroup(QCheckBox *box, qint64 groupId)
{
    const qsizetype index = indexOf(box);
    if (index < 0 || m_entries.at(index).groupId == groupId)
        return;
    Entry entry = m_entries.takeAt(index);
    entry.groupId = groupId;
    insertEntry(entry);
    layoutEntries();
    // Show where it went (once the scroll area has taken the new layout).
    QTimer::singleShot(0, this, [this, target = QPointer<QCheckBox>(box)] {
        if (target)
            m_scrollArea->ensureWidgetVisible(target);
    });
}

int TagPickerDialog::groupRank(qint64 groupId) const
{
    if (groupId == 0)
        return -1;
    const qsizetype rank = m_groupOrder.indexOf(groupId);
    return rank >= 0 ? int(rank) : int(m_groupOrder.size()); // unknown: last
}

void TagPickerDialog::insertEntry(const Entry &entry)
{
    const int rank = groupRank(entry.groupId);
    const auto place = std::find_if(m_entries.cbegin(), m_entries.cend(), [&](const Entry &other) {
        const int otherRank = groupRank(other.groupId);
        return otherRank > rank
               || (otherRank == rank && QString::compare(entry.tag, other.tag, Qt::CaseInsensitive) < 0);
    });
    m_entries.insert(std::distance(m_entries.cbegin(), place), entry);
}

void TagPickerDialog::confirmDelete(QCheckBox *box)
{
    const qsizetype index = indexOf(box);
    if (index < 0)
        return;
    const Entry entry = m_entries.at(index);

    const QString text = entry.inDatabase
        ? tr("Ви впевнені, що хочете видалити тег «%1» з усіх файлів галереї?\n\n"
             "Видалення відбудеться, коли ви натиснете «Зберегти» в цьому вікні.").arg(entry.tag)
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
    entry.box->deleteLater(); // we are inside its context menu
    layoutEntries();
}

bool TagPickerDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_scrollArea && event->type() == QEvent::Resize) {
        // Counted as if the vertical scroll bar were always there: its coming and going can't flip the count.
        const int width = m_scrollArea->contentsRect().width()
                          - m_scrollArea->verticalScrollBar()->sizeHint().width();
        const int columns = qMax(1, width / MinColumnWidth);
        if (columns != m_columns) {
            m_columns = columns;
            layoutEntries();
        }
    }
    return QDialog::eventFilter(watched, event);
}

void TagPickerDialog::layoutEntries()
{
    // Equal columns: only the ones in use stretch (an empty stretching column would still take space).
    for (int column = 0; column < qMax(m_columns, m_grid->columnCount()); ++column)
        m_grid->setColumnStretch(column, column < m_columns ? 1 : 0);
    while (QLayoutItem *item = m_grid->takeAt(0))
        delete item; // the widget stays, only its place in the grid goes
    // A group's name only while some tag of the group is shown: it takes a cell before them.
    for (QWidget *label : std::as_const(m_groupLabels))
        label->hide();
    int cell = 0;
    qint64 group = 0;
    for (const Entry &entry : std::as_const(m_entries)) {
        if (entry.groupId != group) {
            group = entry.groupId;
            // Every group starts a new row.
            if (cell % m_columns != 0)
                cell += m_columns - cell % m_columns;
            if (group != 0) {
                QWidget *label = groupLabel(group);
                m_grid->addWidget(label, cell / m_columns, cell % m_columns);
                label->show();
                ++cell;
            }
        }
        m_grid->addWidget(entry.box, cell / m_columns, cell % m_columns);
        ++cell;
    }
}
