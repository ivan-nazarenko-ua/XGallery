#include "mainwindow.h"
#include "./ui_mainwindow.h"

#include "busydialog.h"
#include "gallerydialog.h"
#include "imagemetadialog.h"
#include "imageviewer.h"
#include "scanner.h"

#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtConcurrent>

namespace {

const char *CurrentGalleryKey = "current_gallery";

QImage makeThumbnail(const QString &path, int size)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QSize original = reader.size();
    if (original.isValid() && (original.width() > size || original.height() > size))
        reader.setScaledSize(original.scaled(size, size, Qt::KeepAspectRatio)); // cheap for JPEG
    QImage image = reader.read();
    if (!image.isNull() && (image.width() > size || image.height() > size))
        image = image.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return image;
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_busyDialog(new BusyDialog(this))
{
    ui->setupUi(this);
    setWindowTitle("XGallery");

    m_busyShowTimer.setSingleShot(true);
    m_busyShowTimer.setInterval(250); // don't flash the dialog for very short jobs
    connect(&m_busyShowTimer, &QTimer::timeout, m_busyDialog, &QDialog::show);

    buildUi();

    if (!Db::open(Db::MainConnection).isOpen()) {
        QMessageBox::critical(this, tr("Помилка БД"),
                              tr("Не вдалося відкрити базу даних %1:\n%2")
                                  .arg(QDir::toNativeSeparators(Db::databasePath()), Db::lastError()));
        return;
    }

    reloadGalleries(Db::setting(CurrentGalleryKey).toLongLong());
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::buildUi()
{
    // Menu
    QMenu *fileMenu = menuBar()->addMenu(tr("&Файл"));
    fileMenu->addAction(tr("&Створити галерею…"), QKeySequence::New, this, &MainWindow::createGallery);
    fileMenu->addAction(tr("&Редагувати галерею…"), this, &MainWindow::editGallery);
    fileMenu->addAction(tr("Рес&кан"), QKeySequence(Qt::Key_F5), this, &MainWindow::startRescan);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("&Вийти"), QKeySequence::Quit, this, &QWidget::close);

    auto *central = ui->centralwidget;
    auto *mainLayout = new QVBoxLayout(central);

    // Gallery row
    auto *rootRow = new QHBoxLayout;
    rootRow->addWidget(new QLabel(tr("Галерея:")));
    m_galleryCombo = new QComboBox;
    m_galleryCombo->setMinimumContentsLength(20);
    m_galleryCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    connect(m_galleryCombo, &QComboBox::currentIndexChanged, this, &MainWindow::onGalleryChanged);
    rootRow->addWidget(m_galleryCombo);
    auto *createButton = new QPushButton(tr("Створити"));
    connect(createButton, &QPushButton::clicked, this, &MainWindow::createGallery);
    rootRow->addWidget(createButton);
    m_editGalleryButton = new QPushButton(tr("Редагувати"));
    connect(m_editGalleryButton, &QPushButton::clicked, this, &MainWindow::editGallery);
    rootRow->addWidget(m_editGalleryButton);
    m_rootLabel = new QLabel;
    m_rootLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_rootLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_rootLabel->setIndent(8);
    rootRow->addWidget(m_rootLabel, 1);
    m_rescanButton = new QPushButton(tr("Рескан"));
    m_rescanButton->setToolTip(tr("Перечитати папку: додати нові файли, прибрати зниклі (F5)"));
    connect(m_rescanButton, &QPushButton::clicked, this, &MainWindow::startRescan);
    rootRow->addWidget(m_rescanButton);
    mainLayout->addLayout(rootRow);

    // Search row
    auto *searchRow = new QHBoxLayout;
    searchRow->addWidget(new QLabel(tr("Містить теги:")));
    m_includeEdit = new QLineEdit;
    m_includeEdit->setPlaceholderText(tr("тег1, тег2 (усі одночасно)"));
    m_includeEdit->setClearButtonEnabled(true);
    searchRow->addWidget(m_includeEdit, 1);
    searchRow->addWidget(new QLabel(tr("Не містить:")));
    m_excludeEdit = new QLineEdit;
    m_excludeEdit->setPlaceholderText(tr("тег3, тег4 (жодного з них)"));
    m_excludeEdit->setClearButtonEnabled(true);
    searchRow->addWidget(m_excludeEdit, 1);
    auto *searchButton = new QPushButton(tr("Шукати"));
    searchButton->setDefault(true);
    connect(searchButton, &QPushButton::clicked, this, &MainWindow::applySearch);
    connect(m_includeEdit, &QLineEdit::returnPressed, this, &MainWindow::applySearch);
    connect(m_excludeEdit, &QLineEdit::returnPressed, this, &MainWindow::applySearch);
    searchRow->addWidget(searchButton);
    auto *clearButton = new QPushButton(tr("Скинути"));
    connect(clearButton, &QPushButton::clicked, this, &MainWindow::clearSearch);
    searchRow->addWidget(clearButton);
    mainLayout->addLayout(searchRow);

    // Tags list | thumbnail grid
    auto *splitter = new QSplitter(Qt::Horizontal);

    auto *tagsBox = new QGroupBox(tr("Теги"));
    auto *tagsLayout = new QVBoxLayout(tagsBox);
    m_tagList = new QListWidget;
    m_tagList->setToolTip(tr("Подвійний клік — додати до «Містить».\n"
                             "Правий клік — вибрати, куди додати."));
    m_tagList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tagList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        addTagToEdit(m_includeEdit, item->text());
        applySearch();
    });
    connect(m_tagList, &QWidget::customContextMenuRequested, this, &MainWindow::showTagMenu);
    tagsLayout->addWidget(m_tagList);
    splitter->addWidget(tagsBox);

    auto *gridHost = new QWidget;
    auto *gridLayout = new QGridLayout(gridHost);
    gridLayout->setSpacing(6);
    for (int i = 0; i < PageSize; ++i) {
        auto *button = new QToolButton;
        button->setIconSize(QSize(ThumbnailSize, ThumbnailSize));
        button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        button->setAutoRaise(true);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        button->setMinimumSize(ThumbnailSize + 16, ThumbnailSize + 32);
        button->setEnabled(false);
        connect(button, &QToolButton::clicked, this, [this, i] { openThumbnail(i); });
        button->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(button, &QWidget::customContextMenuRequested, this, [this, i] { editImageMeta(i); });
        gridLayout->addWidget(button, i / GridColumns, i % GridColumns);
        m_thumbButtons << button;
    }
    splitter->addWidget(gridHost);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({200, 800});
    mainLayout->addWidget(splitter, 1);

    // Paging row
    auto *pageRow = new QHBoxLayout;
    pageRow->addStretch();
    m_prevButton = new QPushButton(tr("◀ Попередня"));
    connect(m_prevButton, &QPushButton::clicked, this, &MainWindow::showPreviousPage);
    pageRow->addWidget(m_prevButton);
    m_pageLabel = new QLabel;
    m_pageLabel->setMinimumWidth(160);
    m_pageLabel->setAlignment(Qt::AlignCenter);
    pageRow->addWidget(m_pageLabel);
    m_nextButton = new QPushButton(tr("Наступна ▶"));
    connect(m_nextButton, &QPushButton::clicked, this, &MainWindow::showNextPage);
    pageRow->addWidget(m_nextButton);
    pageRow->addStretch();
    mainLayout->addLayout(pageRow);

    resize(1150, 900);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (isBusy()) {
        event->ignore(); // a background job still uses the DB
        return;
    }
    QMainWindow::closeEvent(event);
}

const Gallery *MainWindow::currentGallery() const
{
    const int index = m_galleryCombo->currentIndex();
    return index >= 0 && index < m_galleries.size() ? &m_galleries[index] : nullptr;
}

void MainWindow::reloadGalleries(qint64 selectId)
{
    m_galleries = Db::galleries();

    int index = m_galleries.isEmpty() ? -1 : 0;
    {
        const QSignalBlocker blocker(m_galleryCombo);
        m_galleryCombo->clear();
        for (int i = 0; i < m_galleries.size(); ++i) {
            m_galleryCombo->addItem(m_galleries[i].name, m_galleries[i].id);
            if (m_galleries[i].id == selectId)
                index = i;
        }
        m_galleryCombo->setCurrentIndex(index);
    }
    onGalleryChanged(index);
}

void MainWindow::onGalleryChanged(int index)
{
    Q_UNUSED(index);
    const Gallery *gallery = currentGallery();

    m_galleryCombo->setEnabled(!m_galleries.isEmpty());
    m_editGalleryButton->setEnabled(gallery);
    m_rescanButton->setEnabled(gallery);
    if (gallery) {
        m_rootLabel->setText(QDir::toNativeSeparators(gallery->rootFolder));
        Db::setSetting(CurrentGalleryKey, QString::number(gallery->id));
    } else {
        m_rootLabel->setText(tr("Галерей ще немає — натисни «Створити»."));
    }

    m_includeEdit->clear();
    m_excludeEdit->clear();
    m_includeTags.clear();
    m_excludeTags.clear();
    reloadTags();
    loadPage(0);
}

void MainWindow::askForRescan(const QString &question)
{
    if (QMessageBox::question(this, tr("Рескан"), question) == QMessageBox::Yes)
        startRescan();
}

QStringList MainWindow::galleryNamesExcept(qint64 galleryId) const
{
    QStringList names;
    for (const Gallery &g : m_galleries)
        if (g.id != galleryId)
            names << g.name;
    return names;
}

void MainWindow::createGallery()
{
    if (isBusy())
        return;
    GalleryDialog dialog(Gallery{}, galleryNamesExcept(0), this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const qint64 id = Db::createGallery({0, dialog.name(), dialog.rootFolder(), dialog.allowUpscale()});
    if (id == 0) {
        QMessageBox::warning(this, tr("Помилка БД"), tr("Не вдалося створити галерею:\n%1").arg(Db::lastError()));
        return;
    }
    reloadGalleries(id);
    askForRescan(tr("Галерею «%1» створено. Запустити рескан зараз?").arg(dialog.name()));
}

void MainWindow::editGallery()
{
    const Gallery *current = currentGallery();
    if (isBusy() || !current)
        return;
    const Gallery original = *current;

    GalleryDialog dialog(original, galleryNamesExcept(original.id), this);
    const int result = dialog.exec();

    if (result == GalleryDialog::DeleteRequested) {
        auto *watcher = new QFutureWatcher<QString>(this);
        runBusy(watcher, tr("Видаляю галерею «%1»…").arg(original.name));
        connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, original] {
            const QString error = watcher->result();
            reloadGalleries(0);
            if (error.isEmpty())
                statusBar()->showMessage(tr("Галерею «%1» видалено. Файли на диску не змінювались.").arg(original.name));
            else
                QMessageBox::warning(this, tr("Помилка БД"), tr("Не вдалося видалити галерею:\n%1").arg(error));
        });
        const qint64 id = original.id;
        watcher->setFuture(QtConcurrent::run([id] {
            const QString connection = QStringLiteral("xgallery_delete");
            QString error;
            {
                QSqlDatabase db = Db::open(connection);
                if (!db.isOpen() || !Db::deleteGallery(db, id))
                    error = Db::lastError();
            }
            Db::close(connection);
            return error;
        }));
        return;
    }
    if (result != QDialog::Accepted)
        return;

    Gallery updated = original;
    updated.name = dialog.name();
    updated.rootFolder = dialog.rootFolder();
    updated.allowUpscale = dialog.allowUpscale();
    if (!Db::updateGallery(updated)) {
        QMessageBox::warning(this, tr("Помилка БД"), tr("Не вдалося зберегти галерею:\n%1").arg(Db::lastError()));
        return;
    }
    reloadGalleries(updated.id);
    if (QDir(updated.rootFolder) != QDir(original.rootFolder))
        askForRescan(tr("Кореневу папку змінено. Запустити рескан зараз?\n\n"
                        "Записи про файли, яких немає в новій папці, буде прибрано з бази."));
}

void MainWindow::startRescan()
{
    const Gallery *gallery = currentGallery();
    if (isBusy() || !gallery)
        return;
    if (!QFileInfo(gallery->rootFolder).isDir()) {
        QMessageBox::warning(this, tr("Рескан"),
                             tr("Папка недоступна:\n%1").arg(QDir::toNativeSeparators(gallery->rootFolder)));
        return;
    }

    auto *watcher = new QFutureWatcher<ScanStats>(this);
    runBusy(watcher, tr("Рескан…"));
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher] {
        const ScanStats stats = watcher->future().resultCount() > 0 ? watcher->result() : ScanStats{};
        reloadTags();
        loadPage(0);
        if (!stats.error.isEmpty()) {
            QMessageBox::warning(this, tr("Рескан"), tr("Рескан не вдався:\n%1").arg(stats.error));
            return;
        }
        QString message = tr("Додано: %1, оновлено: %2, видалено: %3, без змін: %4")
                              .arg(stats.added).arg(stats.updated).arg(stats.removed).arg(stats.unchanged);
        if (stats.failed > 0)
            message += tr(", не вдалося прочитати: %1").arg(stats.failed);
        statusBar()->showMessage(tr("Рескан завершено. ") + message);
    });

    const qint64 galleryId = gallery->id;
    const QString root = gallery->rootFolder;
    watcher->setFuture(QtConcurrent::run([galleryId, root](QPromise<ScanStats> &promise) {
        Scanner::rescan(promise, galleryId, root);
    }));
}

QStringList MainWindow::parseTags(const QString &text)
{
    QStringList tags;
    for (const QString &part : text.split(',', Qt::SkipEmptyParts)) {
        const QString tag = part.trimmed();
        if (!tag.isEmpty() && !tags.contains(tag))
            tags << tag;
    }
    return tags;
}

void MainWindow::applySearch()
{
    m_includeTags = parseTags(m_includeEdit->text());
    m_excludeTags = parseTags(m_excludeEdit->text());
    loadPage(0);
}

void MainWindow::clearSearch()
{
    m_includeEdit->clear();
    m_excludeEdit->clear();
    applySearch();
}

void MainWindow::addTagToEdit(QLineEdit *edit, const QString &tag)
{
    QStringList tags = parseTags(edit->text());
    if (!tags.contains(tag))
        tags << tag;
    edit->setText(tags.join(", "));
}

void MainWindow::showTagMenu(const QPoint &pos)
{
    QListWidgetItem *item = m_tagList->itemAt(pos);
    if (!item)
        return;
    const QString tag = item->text();
    QMenu menu(this);
    menu.addAction(tr("Додати до «Містить»"), this, [this, tag] {
        addTagToEdit(m_includeEdit, tag);
        applySearch();
    });
    menu.addAction(tr("Додати до «Не містить»"), this, [this, tag] {
        addTagToEdit(m_excludeEdit, tag);
        applySearch();
    });
    menu.exec(m_tagList->viewport()->mapToGlobal(pos));
}

void MainWindow::reloadTags()
{
    m_tagList->clear();
    const Gallery *gallery = currentGallery();
    if (!gallery)
        return;

    // Service tag: always on top, always present, not stored in the DB.
    auto *noTags = new QListWidgetItem(Db::NoTagsPseudoTag, m_tagList);
    QFont font = noTags->font();
    font.setItalic(true);
    noTags->setFont(font);
    noTags->setForeground(palette().color(QPalette::PlaceholderText));
    noTags->setToolTip(tr("Зображення, у яких немає жодного тегу"));

    m_tagList->addItems(Db::allTags(gallery->id));
}

QString MainWindow::absolutePath(const QString &relativePath) const
{
    const Gallery *gallery = currentGallery();
    return gallery ? QDir(gallery->rootFolder).filePath(relativePath) : QString();
}

void MainWindow::showPreviousPage()
{
    if (m_page > 0)
        loadPage(m_page - 1);
}

void MainWindow::showNextPage()
{
    if ((m_page + 1) * PageSize < m_totalCount)
        loadPage(m_page + 1);
}

void MainWindow::loadPage(int page)
{
    if (isBusy())
        return;

    m_page = page;
    const Gallery *gallery = currentGallery();
    m_pageRecords = Db::searchImages(gallery ? gallery->id : 0, m_includeTags, m_excludeTags, page * PageSize, PageSize,
                                     &m_totalCount);
    if (m_pageRecords.isEmpty() && page > 0 && m_totalCount > 0) {
        // The page shrank (e.g. after editing tags): show the last one that exists.
        loadPage((m_totalCount - 1) / PageSize);
        return;
    }

    const int pageCount = qMax(1, (m_totalCount + PageSize - 1) / PageSize);
    m_pageLabel->setText(tr("Сторінка %1 з %2 (%3 зобр.)").arg(m_page + 1).arg(pageCount).arg(m_totalCount));
    m_prevButton->setEnabled(m_page > 0);
    m_nextButton->setEnabled(m_page + 1 < pageCount);

    for (int i = 0; i < m_thumbButtons.size(); ++i) {
        QToolButton *button = m_thumbButtons[i];
        button->setIcon(QIcon());
        if (i < m_pageRecords.size()) {
            const ImageRecord &rec = m_pageRecords[i];
            const QString name = QFileInfo(rec.relativePath).fileName();
            button->setText(button->fontMetrics().elidedText(name, Qt::ElideMiddle, ThumbnailSize));
            button->setToolTip(QDir::toNativeSeparators(rec.relativePath) + "\n"
                               + tr("Оцінка: ") + QString::number(rec.rating) + "\n"
                               + tr("Теги: ") + Db::tagsForImage(rec.id).join(", ") + "\n\n"
                               + tr("Клік — відкрити, правий клік — редагувати"));
            button->setEnabled(true);
        } else {
            button->setText(QString());
            button->setToolTip(QString());
            button->setEnabled(false);
        }
    }

    if (m_pageRecords.isEmpty())
        return;

    QStringList paths;
    for (const ImageRecord &rec : std::as_const(m_pageRecords))
        paths << absolutePath(rec.relativePath);

    auto *watcher = new QFutureWatcher<QImage>(this);
    runBusy(watcher, tr("Завантажую прев'юшки…"));
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher] {
        const QList<QImage> images = watcher->future().results();
        for (int i = 0; i < images.size() && i < m_thumbButtons.size(); ++i) {
            if (images[i].isNull())
                m_thumbButtons[i]->setText(tr("(не читається)"));
            else
                m_thumbButtons[i]->setIcon(QIcon(QPixmap::fromImage(images[i])));
        }
    });
    watcher->setFuture(QtConcurrent::mapped(std::move(paths),[](const QString &path) {
        return makeThumbnail(path, ThumbnailSize);
    }));
}

void MainWindow::openThumbnail(int index)
{
    const Gallery *gallery = currentGallery();
    if (!gallery || index < 0 || index >= m_pageRecords.size())
        return;
    const QString path = absolutePath(m_pageRecords[index].relativePath);
    if (!QFileInfo::exists(path)) {
        QMessageBox::warning(this, tr("Файл не знайдено"),
                             tr("Файлу більше немає:\n%1\n\nЗроби рескан.").arg(QDir::toNativeSeparators(path)));
        return;
    }

    // The arrows in the viewer walk through the whole search result, not just this page.
    const QList<ImageRecord> all = Db::searchImages(gallery->id, m_includeTags, m_excludeTags, 0, -1, nullptr);
    QStringList paths;
    paths.reserve(all.size());
    int current = 0;
    for (const ImageRecord &rec : all) {
        if (rec.id == m_pageRecords[index].id)
            current = int(paths.size());
        paths << absolutePath(rec.relativePath);
    }
    if (paths.isEmpty())
        paths << path;

    auto *viewer = new ImageViewer(paths, current, gallery->allowUpscale, this);
    viewer->show();
}

void MainWindow::editImageMeta(int index)
{
    const Gallery *gallery = currentGallery();
    if (isBusy() || !gallery || index < 0 || index >= m_pageRecords.size())
        return;
    const ImageRecord record = m_pageRecords[index];
    const qint64 galleryId = gallery->id;

    ImageMetaDialog dialog(absolutePath(record.relativePath), Db::tagsForImage(record.id), record.rating,
                           Db::allTags(galleryId), this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    if (!Db::saveImageMeta(galleryId, record.id, dialog.rating(), dialog.tags())) {
        QMessageBox::warning(this, tr("Помилка БД"), tr("Не вдалося зберегти зміни:\n%1").arg(Db::lastError()));
        return;
    }
    statusBar()->showMessage(tr("Збережено: %1").arg(QFileInfo(record.relativePath).fileName()), 5000);
    reloadTags();
    loadPage(m_page); // rating may have moved the image
}

void MainWindow::runBusy(QFutureWatcherBase *watcher, const QString &text)
{
    beginBusy(text);
    connect(watcher, &QFutureWatcherBase::progressRangeChanged, m_busyDialog, &BusyDialog::setRange);
    connect(watcher, &QFutureWatcherBase::progressValueChanged, m_busyDialog, &BusyDialog::setValue);
    connect(watcher, &QFutureWatcherBase::progressTextChanged, m_busyDialog, &BusyDialog::setText);
    // Must be connected before the caller's own finished-handler, so the UI is unlocked
    // by the time that handler runs (it may start another job).
    connect(watcher, &QFutureWatcherBase::finished, this, &MainWindow::endBusy);
    connect(watcher, &QFutureWatcherBase::finished, watcher, &QObject::deleteLater);
}

void MainWindow::beginBusy(const QString &text)
{
    if (m_busyDepth++ > 0)
        return;
    m_busyDialog->setText(text);
    m_busyDialog->setRange(0, 0);
    ui->centralwidget->setEnabled(false);
    menuBar()->setEnabled(false);
    QApplication::setOverrideCursor(Qt::WaitCursor);
    m_busyShowTimer.start();
}

void MainWindow::endBusy()
{
    if (m_busyDepth == 0 || --m_busyDepth > 0)
        return;
    m_busyShowTimer.stop();
    m_busyDialog->hide();
    ui->centralwidget->setEnabled(true);
    menuBar()->setEnabled(true);
    QApplication::restoreOverrideCursor();
}
