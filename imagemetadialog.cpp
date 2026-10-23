#include "imagemetadialog.h"

#include "database.h"
#include "imageviewer.h"
#include "mediafiles.h"

#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QMessageBox>
#include <QPushButton>
#include <QShortcut>
#include <QSpinBox>
#include <QUrl>
#include <QVBoxLayout>

namespace {

constexpr int PreviewSize = 220;

bool lessCaseInsensitive(const QString &a, const QString &b)
{
    return QString::compare(a, b, Qt::CaseInsensitive) < 0;
}

} // namespace

ImageMetaDialog::ImageMetaDialog(const QStringList &filePaths, const QStringList &imageTags, int rating,
                                 const QStringList &galleryTags, QWidget *parent)
    : QDialog(parent)
    , m_galleryTags(galleryTags)
    , m_tags(imageTags)
    , m_initialTags(imageTags)
    , m_tagList(new QListWidget(this))
    , m_removeButton(new QPushButton(tr("Видалити тег"), this))
    , m_existingCombo(new QComboBox(this))
    , m_addExistingButton(new QPushButton(tr("Додати"), this))
    , m_newTagEdit(new QLineEdit(this))
    , m_ratingSpin(new QSpinBox(this))
{
    const bool bulk = filePaths.size() > 1;
    const QString filePath = filePaths.value(0);
    setWindowTitle(bulk ? tr("Групове редагування тегів — файлів: %1").arg(filePaths.size())
                        : tr("Метаінформація — %1").arg(QFileInfo(filePath).fileName()));
    setMinimumWidth(560);

    // Preview + path (single file) or the list of chosen files (bulk)
    m_filePaths = filePaths;
    auto *preview = new QLabel(this);
    m_preview = preview;
    preview->setFixedSize(PreviewSize, PreviewSize);
    preview->setAlignment(Qt::AlignCenter);
    QWidget *pathLabel;
    if (bulk) {
        preview->setText(tr("Обрано файлів: %1").arg(filePaths.size()));
        preview->setFixedHeight(40);
        // May be dozens of files (the selection spans pages): a scrollable list.
        auto *files = new QListWidget(this);
        for (const QString &path : filePaths) {
            auto *item = new QListWidgetItem(QFileInfo(path).fileName(), files);
            item->setToolTip(QDir::toNativeSeparators(path));
        }
        files->setSelectionMode(QAbstractItemView::NoSelection);
        files->setFixedWidth(PreviewSize);
        files->setToolTip(tr("Клік — відкрити файл"));
        connect(files, &QListWidget::itemClicked, this,
                [this, files](QListWidgetItem *item) { openFile(files->row(item)); });
        pathLabel = files;
    } else {
        updatePreview();
        if (MediaFiles::kindOf(filePath) != MediaFiles::Kind::Other) {
            preview->setCursor(Qt::PointingHandCursor);
            preview->setToolTip(tr("Клік — відкрити"));
            preview->installEventFilter(this);
        }
        auto *label = new QLabel(QDir::toNativeSeparators(filePath), this);
        label->setWordWrap(true);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        pathLabel = label;
    }

    // Tags
    m_tagList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    connect(m_tagList, &QListWidget::itemSelectionChanged, this, [this] {
        m_removeButton->setEnabled(!m_tagList->selectedItems().isEmpty());
    });
    connect(m_removeButton, &QPushButton::clicked, this, &ImageMetaDialog::removeSelectedTags);
    m_tagList->setToolTip(tr("Подвійний клік — додати тег у поле «Містить» головного вікна"));
    connect(m_tagList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        emit tagActivated(item->text());
    });
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
    if (!bulk)
        form->addRow(tr("Оцінка:"), m_ratingSpin);
    else
        m_ratingSpin->hide();

    auto *tagsColumn = new QVBoxLayout;
    tagsColumn->addWidget(new QLabel(bulk ? tr("Спільні теги обраних файлів:") : tr("Теги зображення:"), this));
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
    QPushButton *deleteButton = buttons->addButton(tr("Видалити файл…"), QDialogButtonBox::DestructiveRole);
    deleteButton->setVisible(!bulk);
    connect(deleteButton, &QPushButton::clicked, this, [this, filePath] {
        QMessageBox box(QMessageBox::Warning, tr("Видалити файл"),
                        tr("Перемістити файл у Кошик і прибрати його з галереї?\n\n%1")
                            .arg(QDir::toNativeSeparators(filePath)),
                        QMessageBox::NoButton, this);
        QPushButton *yes = box.addButton(tr("Видалити"), QMessageBox::DestructiveRole);
        QPushButton *no = box.addButton(tr("Скасувати"), QMessageBox::RejectRole);
        box.setDefaultButton(no);
        box.exec();
        if (box.clickedButton() == yes)
            done(DeleteRequested); // the caller does the actual deletion
    });

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(top, 1);
    layout->addWidget(buttons);

    // Enter in the "new tag" field must add the tag, not press some button of the dialog.
    for (QPushButton *button : findChildren<QPushButton *>())
        button->setAutoDefault(false);

    std::sort(m_tags.begin(), m_tags.end(), lessCaseInsensitive);
    refresh();
}

void ImageMetaDialog::setAllowUpscale(bool allow)
{
    m_allowUpscale = allow;
}

void ImageMetaDialog::updatePreview()
{
    const QImage image = MediaFiles::makeThumbnail(m_filePaths.value(0), PreviewSize); // also handles videos
    if (image.isNull())
        m_preview->setText(tr("(не читається)"));
    else
        m_preview->setPixmap(QPixmap::fromImage(image));
}

bool ImageMetaDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_preview && event->type() == QEvent::MouseButtonRelease
        && static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton) {
        openFile(0);
        return true;
    }
    return QDialog::eventFilter(watched, event);
}

// Image: viewer walking through the images of this dialog (just one in single mode).
// Video: the program Windows associates with it. Anything else: nothing.
void ImageMetaDialog::openFile(int index)
{
    const QString path = m_filePaths.value(index);
    switch (MediaFiles::kindOf(path)) {
    case MediaFiles::Kind::Image: {
        QStringList images;
        int current = 0;
        for (int i = 0; i < m_filePaths.size(); ++i) {
            if (MediaFiles::kindOf(m_filePaths[i]) != MediaFiles::Kind::Image)
                continue;
            if (i == index)
                current = int(images.size());
            images << m_filePaths[i];
        }
        // A child of the dialog, so it works while the dialog is modal and closes with it.
        auto *viewer = new ImageViewer(images, current, m_allowUpscale, this);
        connect(viewer, &ImageViewer::fileChanged, this, [this](const QString &file) {
            if (m_filePaths.size() == 1)
                updatePreview(); // a saved rotation shows up here too
            emit fileChanged(file);
        });
        viewer->show();
        break;
    }
    case MediaFiles::Kind::Video:
        if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path)))
            QMessageBox::warning(this, windowTitle(),
                                 tr("Не вдалося відкрити файл:\n%1").arg(QDir::toNativeSeparators(path)));
        break;
    case MediaFiles::Kind::Other:
        break;
    }
}

void ImageMetaDialog::done(int result)
{
    // Open viewers may hold an unsaved rotation: let them ask first; "Cancel" keeps the dialog open.
    for (ImageViewer *viewer : findChildren<ImageViewer *>(Qt::FindDirectChildrenOnly))
        if (!viewer->close())
            return;
    QDialog::done(result);
}

QStringList ImageMetaDialog::tags() const
{
    return m_tags;
}

QStringList ImageMetaDialog::addedTags() const
{
    QStringList added;
    for (const QString &tag : m_tags)
        if (!m_initialTags.contains(tag))
            added << tag;
    return added;
}

QStringList ImageMetaDialog::removedTags() const
{
    QStringList removed;
    for (const QString &tag : m_initialTags)
        if (!m_tags.contains(tag))
            removed << tag;
    return removed;
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
    const QString tag = Db::normalizeTag(m_newTagEdit->text());
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
