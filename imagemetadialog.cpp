#include "imagemetadialog.h"

#include "mediafiles.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QShortcut>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {

constexpr int PreviewSize = 220;

bool lessCaseInsensitive(const QString &a, const QString &b)
{
    return QString::compare(a, b, Qt::CaseInsensitive) < 0;
}

} // namespace

ImageMetaDialog::ImageMetaDialog(const QString &filePath, const QStringList &imageTags, int rating,
                                 const QStringList &galleryTags, QWidget *parent)
    : QDialog(parent)
    , m_galleryTags(galleryTags)
    , m_tags(imageTags)
    , m_tagList(new QListWidget(this))
    , m_removeButton(new QPushButton(tr("Видалити тег"), this))
    , m_existingCombo(new QComboBox(this))
    , m_addExistingButton(new QPushButton(tr("Додати"), this))
    , m_newTagEdit(new QLineEdit(this))
    , m_ratingSpin(new QSpinBox(this))
{
    setWindowTitle(tr("Метаінформація — %1").arg(QFileInfo(filePath).fileName()));
    setMinimumWidth(560);

    // Preview + path
    auto *preview = new QLabel(this);
    preview->setFixedSize(PreviewSize, PreviewSize);
    preview->setAlignment(Qt::AlignCenter);
    const QImage image = MediaFiles::makeThumbnail(filePath, PreviewSize); // also handles videos
    if (image.isNull())
        preview->setText(tr("(не читається)"));
    else
        preview->setPixmap(QPixmap::fromImage(image));

    auto *pathLabel = new QLabel(QDir::toNativeSeparators(filePath), this);
    pathLabel->setWordWrap(true);
    pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    // Tags
    m_tagList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    connect(m_tagList, &QListWidget::itemSelectionChanged, this, [this] {
        m_removeButton->setEnabled(!m_tagList->selectedItems().isEmpty());
    });
    connect(m_removeButton, &QPushButton::clicked, this, &ImageMetaDialog::removeSelectedTags);
    auto *deleteShortcut = new QShortcut(QKeySequence::Delete, m_tagList);
    connect(deleteShortcut, &QShortcut::activated, this, &ImageMetaDialog::removeSelectedTags);

    auto *existingRow = new QHBoxLayout;
    existingRow->addWidget(m_existingCombo, 1);
    existingRow->addWidget(m_addExistingButton);
    connect(m_addExistingButton, &QPushButton::clicked, this, &ImageMetaDialog::addExistingTag);

    auto *newRow = new QHBoxLayout;
    m_newTagEdit->setPlaceholderText(tr("назва нового тегу"));
    newRow->addWidget(m_newTagEdit, 1);
    auto *addNewButton = new QPushButton(tr("Додати"), this);
    newRow->addWidget(addNewButton);
    connect(addNewButton, &QPushButton::clicked, this, &ImageMetaDialog::addNewTag);
    connect(m_newTagEdit, &QLineEdit::returnPressed, this, &ImageMetaDialog::addNewTag);

    m_ratingSpin->setRange(0, 100);
    m_ratingSpin->setValue(rating);

    auto *form = new QFormLayout;
    form->addRow(tr("Наявний тег:"), existingRow);
    form->addRow(tr("Новий тег:"), newRow);
    form->addRow(tr("Оцінка:"), m_ratingSpin);

    auto *tagsColumn = new QVBoxLayout;
    tagsColumn->addWidget(new QLabel(tr("Теги зображення:"), this));
    tagsColumn->addWidget(m_tagList, 1);
    tagsColumn->addWidget(m_removeButton, 0, Qt::AlignLeft);
    tagsColumn->addLayout(form);

    auto *previewColumn = new QVBoxLayout;
    previewColumn->addWidget(preview);
    previewColumn->addWidget(pathLabel);
    previewColumn->addStretch();

    auto *top = new QHBoxLayout;
    top->addLayout(previewColumn);
    top->addLayout(tagsColumn, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Save)->setText(tr("Зберегти"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Скасувати"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(top, 1);
    layout->addWidget(buttons);

    // Enter in the "new tag" field must add the tag, not press some button of the dialog.
    for (QPushButton *button : findChildren<QPushButton *>())
        button->setAutoDefault(false);

    std::sort(m_tags.begin(), m_tags.end(), lessCaseInsensitive);
    refresh();
}

QStringList ImageMetaDialog::tags() const
{
    return m_tags;
}

int ImageMetaDialog::rating() const
{
    return m_ratingSpin->value();
}

void ImageMetaDialog::addTag(const QString &tag)
{
    if (tag.isEmpty() || m_tags.contains(tag))
        return;
    m_tags << tag;
    std::sort(m_tags.begin(), m_tags.end(), lessCaseInsensitive);
    refresh();
}

void ImageMetaDialog::addExistingTag()
{
    addTag(m_existingCombo->currentText());
}

void ImageMetaDialog::addNewTag()
{
    const QString tag = m_newTagEdit->text().trimmed();
    if (tag.isEmpty())
        return;
    // Commas separate tags in the search fields; '<' '>' are reserved for service tags like "<без тегів>".
    if (tag.contains(',') || tag.contains('<') || tag.contains('>')) {
        QMessageBox::warning(this, windowTitle(), tr("Назва тегу не може містити символи «,», «<» і «>»."));
        return;
    }
    addTag(tag);
    m_newTagEdit->clear();
}

void ImageMetaDialog::removeSelectedTags()
{
    const auto selected = m_tagList->selectedItems();
    for (const QListWidgetItem *item : selected)
        m_tags.removeAll(item->text());
    refresh();
}

void ImageMetaDialog::refresh()
{
    m_tagList->clear();
    m_tagList->addItems(m_tags);
    m_removeButton->setEnabled(false);

    const QString previous = m_existingCombo->currentText();
    m_existingCombo->clear();
    for (const QString &tag : std::as_const(m_galleryTags))
        if (!m_tags.contains(tag))
            m_existingCombo->addItem(tag);
    const int index = m_existingCombo->findText(previous);
    if (index >= 0)
        m_existingCombo->setCurrentIndex(index);
    m_existingCombo->setEnabled(m_existingCombo->count() > 0);
    m_addExistingButton->setEnabled(m_existingCombo->count() > 0);
}
