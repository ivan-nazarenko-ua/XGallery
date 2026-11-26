#include "imagemetadialog.h"

#include "database.h"
#include "imageviewer.h"
#include "mediafiles.h"
#include "tagpickerdialog.h"

#include <QAction>
#include <QClipboard>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QMessageBox>
#include <QPushButton>
#include <QShortcut>
#include <QStandardPaths>
#include <QUrl>
#include <QVBoxLayout>

namespace {

constexpr int PreviewSize = 220;
const char ExportFolderKey[] = "last_export_folder";

bool lessCaseInsensitive(const QString &a, const QString &b)
{
    return QString::compare(a, b, Qt::CaseInsensitive) < 0;
}

} // namespace

ImageMetaDialog::ImageMetaDialog(const QStringList &filePaths, const QStringList &imageTags,
                                 const QList<TagSection> &galleryTags, QWidget *parent)
    : QDialog(parent)
    , m_galleryTags(galleryTags)
    , m_tags(imageTags)
    , m_initialTags(imageTags)
    , m_tagList(new QListWidget(this))
    , m_removeButton(new QPushButton(tr("Видалити тег"), this))
    , m_nameEdit(new QLineEdit(QFileInfo(filePaths.value(0)).fileName(), this))
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
    // Ctrl+C belongs to the list only (the path label has its own); Ctrl+V works in the whole
    // dialog, text fields keep theirs. Copy stays enabled with nothing selected: a disabled action
    // would hand Ctrl+C to QListWidget, which puts the current row into the clipboard.
    auto *copyAction = new QAction(tr("Копіювати теги"), m_tagList);
    copyAction->setShortcut(QKeySequence::Copy);
    copyAction->setShortcutContext(Qt::WidgetShortcut);
    connect(copyAction, &QAction::triggered, this, &ImageMetaDialog::copySelectedTags);
    auto *pasteAction = new QAction(tr("Вставити теги"), m_tagList);
    pasteAction->setShortcut(QKeySequence::Paste);
    connect(pasteAction, &QAction::triggered, this, &ImageMetaDialog::pasteTags);
    m_tagList->addAction(copyAction);
    m_tagList->addAction(pasteAction);
    m_tagList->setContextMenuPolicy(Qt::ActionsContextMenu);
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

    auto *addTagsButton = new QPushButton(tr("Додати теги"), this);
    connect(addTagsButton, &QPushButton::clicked, this, &ImageMetaDialog::chooseTags);
    auto *tagButtons = new QHBoxLayout;
    tagButtons->addWidget(m_removeButton);
    tagButtons->addWidget(addTagsButton);
    tagButtons->addStretch(1);

    auto *form = new QFormLayout;
    if (!bulk)
        form->addRow(tr("Ім'я файлу:"), m_nameEdit);
    else
        m_nameEdit->hide();

    auto *tagsColumn = new QVBoxLayout;
    tagsColumn->addWidget(new QLabel(bulk ? tr("Спільні теги обраних файлів:") : tr("Теги зображення:"), this));
    tagsColumn->addWidget(m_tagList, 1);
    tagsColumn->addLayout(tagButtons);
    tagsColumn->addLayout(form);

    auto *previewColumn = new QVBoxLayout;
    previewColumn->addWidget(preview);
    if (!bulk && MediaFiles::kindOf(filePath) == MediaFiles::Kind::Image) {
        m_coverButton = new QPushButton(tr("Встановити обкладинкою"), this);
        m_coverButton->setToolTip(tr("Зробити це зображення обкладинкою його колекції (після «Зберегти»)"));
        connect(m_coverButton, &QPushButton::clicked, this, [this] {
            m_coverRequested = true;
            m_coverButton->setEnabled(false);
        });
        previewColumn->addWidget(m_coverButton);
    }
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
    QPushButton *exportButton = buttons->addButton(tr("Експорт…"), QDialogButtonBox::ActionRole);
    exportButton->setVisible(!bulk);
    connect(exportButton, &QPushButton::clicked, this, &ImageMetaDialog::chooseExportDestination);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(top, 1);
    layout->addWidget(buttons);

    // Enter in the file name field must not press some button of the dialog.
    for (QPushButton *button : findChildren<QPushButton *>())
        button->setAutoDefault(false);

    std::sort(m_tags.begin(), m_tags.end(), lessCaseInsensitive);
    refresh();
}

void ImageMetaDialog::setAllowUpscale(bool allow)
{
    m_allowUpscale = allow;
}

void ImageMetaDialog::setKeepExtension(bool keep)
{
    m_keepExtension = keep;
    const QFileInfo original(m_filePaths.value(0));
    m_nameEdit->setText(keep ? original.completeBaseName() : original.fileName());
}

void ImageMetaDialog::setIsCover(bool isCover)
{
    if (!m_coverButton)
        return;
    m_coverButton->setEnabled(!isCover && !m_coverRequested);
    if (isCover)
        m_coverButton->setToolTip(tr("Це зображення вже є обкладинкою своєї колекції"));
}

void ImageMetaDialog::setExporting(bool exporting)
{
    m_exporting = exporting;
    setEnabled(!exporting);
}

void ImageMetaDialog::chooseExportDestination()
{
    const QFileInfo source(m_filePaths.value(0));
    const QString folder = Db::setting(ExportFolderKey,
                                       QStandardPaths::writableLocation(QStandardPaths::PicturesLocation));
    const QString suffix = source.suffix();
    const QString destination = QFileDialog::getSaveFileName(
        this, tr("Експорт файлу"), QDir(folder).filePath(source.fileName()),
        tr("Файли %1 (*.%2)").arg(suffix.toUpper(), suffix.toLower())); // also adds a forgotten extension
    if (destination.isEmpty())
        return;
    Db::setSetting(ExportFolderKey, QFileInfo(destination).absolutePath());
    emit exportRequested(destination);
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
        // This dialog edits the very files the viewer shows: deleting one under it is not allowed.
        viewer->setDeleteRefusal(m_filePaths.size() == 1
            ? tr("Тут файл не видалити: його зараз редагує вікно метаданих.\n"
                 "Закрийте переглядач і натисніть «Видалити файл…» у вікні метаданих.")
            : tr("Тут файл не видалити: зараз відкрите групове редагування тегів цих файлів.\n"
                 "Закрийте вікно метаданих і видаліть файл з переглядача колекції."));
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
    if (m_exporting)
        return;
    // Open viewers may hold an unsaved rotation: let them ask first; "Cancel" keeps the dialog open.
    for (ImageViewer *viewer : findChildren<ImageViewer *>(Qt::FindDirectChildrenOnly))
        if (!viewer->close())
            return;
    if (m_picker)
        m_picker->reject(); // what it wasn't told to save is dropped, as with its cancel button
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

QString ImageMetaDialog::fileName() const
{
    const QString text = m_nameEdit->text();
    if (!m_keepExtension)
        return text;
    const QFileInfo original(m_filePaths.value(0));
    if (text == original.completeBaseName())
        return original.fileName();
    // A cleared field stays empty, otherwise it would turn into the valid name ".jpg".
    const QString base = text.trimmed();
    return base.isEmpty() ? QString() : base + '.' + original.suffix();
}

void ImageMetaDialog::chooseTags()
{
    if (m_picker) {
        m_picker->raise();
        m_picker->activateWindow();
        return;
    }

    // All gallery tags, plus the file's tags the DB doesn't know yet (typed, pasted).
    // (The partial ones of a bulk edit are stored tags anyway.)
    QList<TagSection> sections = m_galleryTags;
    QStringList known;
    for (const TagSection &section : std::as_const(sections))
        known << section.tags;
    QStringList extra;
    for (const QString &tag : std::as_const(m_tags))
        if (!known.contains(tag))
            extra << tag;
    if (!extra.isEmpty()) {
        if (sections.isEmpty() || sections.first().groupId != 0)
            sections.prepend(TagSection{});
        sections.first().tags << extra;
        std::sort(sections.first().tags.begin(), sections.first().tags.end(), lessCaseInsensitive);
    }

    // A child of the dialog, so it works while the dialog is modal.
    m_picker = new TagPickerDialog(sections, m_tags, m_partialTags, m_tagCollectionCounts, this);
    m_picker->setTagGroups(m_tagGroups, m_groupChanges); // the sections are the stored groups
    m_picker->setAttribute(Qt::WA_DeleteOnClose);
    connect(m_picker, &QDialog::accepted, this, [this] {
        const QStringList added = m_picker->addedTags();
        for (const QString &tag : added)
            if (!m_tags.contains(tag))
                m_tags << tag;
        const QStringList removed = m_picker->removedTags();
        for (const QString &tag : removed)
            m_tags.removeAll(tag);
        // A tag deleted from the gallery is gone from these files already: given again, it is a new tag.
        const QStringList deleted = m_picker->deletedTags();
        for (const QString &tag : deleted) {
            m_tags.removeAll(tag);
            m_initialTags.removeAll(tag);
            m_partialTags.removeAll(tag);
        }
        // Written at once, like the download window does it: the caller refreshes m_galleryTags meanwhile.
        const QHash<QString, qint64> groupChanges = m_picker->groupChanges();
        if (!groupChanges.isEmpty() || !deleted.isEmpty())
            emit galleryTagsEdited(groupChanges, deleted);
        // Tags the gallery doesn't have yet keep their group until the file's tags are saved.
        QStringList stored;
        for (const TagSection &section : std::as_const(m_galleryTags))
            stored << section.tags;
        m_groupChanges.clear();
        for (auto it = groupChanges.cbegin(); it != groupChanges.cend(); ++it)
            if (m_tags.contains(it.key()) && !stored.contains(it.key()))
                m_groupChanges.insert(it.key(), it.value());
        std::sort(m_tags.begin(), m_tags.end(), lessCaseInsensitive);
        refresh();
    });
    m_picker->show();
}

void ImageMetaDialog::removeSelectedTags()
{
    const auto selected = m_tagList->selectedItems();
    if (selected.isEmpty())
        return;
    int firstRow = m_tagList->count();
    for (const QListWidgetItem *item : selected) {
        firstRow = qMin(firstRow, m_tagList->row(item));
        m_tags.removeAll(item->text());
    }
    refresh();
    // The tag that took the place of the (first) removed one is selected, the last one if the end was removed:
    // one click after another removes a run of tags (owner's rule).
    if (m_tagList->count() > 0) {
        m_tagList->setCurrentRow(qMin(firstRow, m_tagList->count() - 1)); // selects it; the button follows
        m_tagList->scrollToItem(m_tagList->currentItem());
    }
}

void ImageMetaDialog::copySelectedTags()
{
    QStringList selected;
    const auto items = m_tagList->selectedItems();
    for (const QListWidgetItem *item : items)
        selected << item->text();
    if (selected.isEmpty())
        return;
    std::sort(selected.begin(), selected.end(), lessCaseInsensitive); // items come in click order
    QGuiApplication::clipboard()->setText(selected.join(", "));
}

void ImageMetaDialog::pasteTags()
{
    const QStringList pasted = Db::tagsFromText(QGuiApplication::clipboard()->text());
    for (const QString &tag : pasted)
        if (!m_tags.contains(tag))
            m_tags << tag;
    std::sort(m_tags.begin(), m_tags.end(), lessCaseInsensitive);
    refresh();
}

void ImageMetaDialog::refresh()
{
    m_tagList->clear();
    m_tagList->addItems(m_tags);
    m_removeButton->setEnabled(false);

    if (m_picker)
        m_picker->setAssignedTags(m_tags); // the file's tags can change while the picker is open
}
