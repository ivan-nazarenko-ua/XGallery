#include "mainwindow.h"
#include "./ui_mainwindow.h"

#include "busydialog.h"
#include "duplicatesdialog.h"
#include "gallerydialog.h"
#include "i18n.h"
#include "imagemetadialog.h"
#include "imageviewer.h"
#include "mediafiles.h"
#include "scanner.h"

#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QDirIterator>
#include <QInputDialog>
#include <QRegularExpression>
#include <QFutureWatcher>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QToolButton>
#include <QVBoxLayout>
#include <QTimeZone>
#include <QUrl>
#include <QtConcurrent>

namespace {

const char *CurrentGalleryKey = "current_gallery";
const char *SortOrderKey = "sort_order";

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_busyDialog(new BusyDialog(this))
{
    ui->setupUi(this);
    // Mode in the title, so two running copies (portable and installed) can be told apart.
    setWindowTitle(Db::isPortable() ? "XGallery [Portable]" : "XGallery [Full]");

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

    Db::makeRootsPortable();
    {
        const QSignalBlocker blocker(m_sortCombo);
        const int saved = m_sortCombo->findData(Db::setting(SortOrderKey).toInt());
        m_sortCombo->setCurrentIndex(qMax(0, saved));
    }
    reloadGalleries(Db::setting(CurrentGalleryKey).toLongLong());
    statusBar()->showMessage((Db::isPortable() ? tr("Портабл-режим. ") : QString())
                                 + tr("База даних: %1").arg(QDir::toNativeSeparators(Db::databasePath())),
                             10000);
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::buildUi()
{
    // Menu. All texts are set in retranslateUi(), so the language can change on the fly.
    m_fileMenu = menuBar()->addMenu(QString());
    m_createGalleryAction = m_fileMenu->addAction(QString(), QKeySequence::New, this, &MainWindow::createGallery);
    m_editGalleryAction = m_fileMenu->addAction(QString(), this, &MainWindow::editGallery);
    m_rescanAction = m_fileMenu->addAction(QString(), QKeySequence(Qt::Key_F5), this, &MainWindow::startRescan);
    m_fileMenu->addSeparator();
    m_addFilesAction = m_fileMenu->addAction(QString(), this, &MainWindow::addFilesToGallery);
    m_addFolderAction = m_fileMenu->addAction(QString(), this, &MainWindow::addFolderToGallery);
    m_fileMenu->addSeparator();
    m_duplicatesAction = m_fileMenu->addAction(QString(), this, &MainWindow::findDuplicates);
    m_junkAction = m_fileMenu->addAction(QString(), this, &MainWindow::deleteJunk);
    m_fileMenu->addSeparator();
    m_quitAction = m_fileMenu->addAction(QString(), QKeySequence::Quit, this, &QWidget::close);

    // "Language" stays in English in every language; each item is in its own language.
    QMenu *languageMenu = menuBar()->addMenu(QStringLiteral("&Language"));
    auto *languageGroup = new QActionGroup(this);
    for (const I18n::Language &language : I18n::languages()) {
        QAction *action = languageMenu->addAction(QIcon(language.flag), language.nativeName);
        action->setCheckable(true);
        action->setChecked(language.code == I18n::current());
        action->setData(language.code);
        languageGroup->addAction(action);
        connect(action, &QAction::triggered, this, [code = language.code] {
            Db::setSetting(I18n::SettingKey, code);
            I18n::apply(code); // every window gets QEvent::LanguageChange
        });
    }

    auto *central = ui->centralwidget;
    auto *mainLayout = new QVBoxLayout(central);

    // Gallery row
    auto *rootRow = new QHBoxLayout;
    m_galleryLabel = new QLabel;
    rootRow->addWidget(m_galleryLabel);
    m_galleryCombo = new QComboBox;
    m_galleryCombo->setMinimumContentsLength(20);
    m_galleryCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    connect(m_galleryCombo, &QComboBox::currentIndexChanged, this, &MainWindow::onGalleryChanged);
    rootRow->addWidget(m_galleryCombo);
    m_createGalleryButton = new QPushButton;
    connect(m_createGalleryButton, &QPushButton::clicked, this, &MainWindow::createGallery);
    rootRow->addWidget(m_createGalleryButton);
    m_editGalleryButton = new QPushButton;
    connect(m_editGalleryButton, &QPushButton::clicked, this, &MainWindow::editGallery);
    rootRow->addWidget(m_editGalleryButton);
    m_rootLabel = new QLabel;
    m_rootLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_rootLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_rootLabel->setIndent(8);
    rootRow->addWidget(m_rootLabel, 1);
    m_rescanButton = new QPushButton;
    connect(m_rescanButton, &QPushButton::clicked, this, &MainWindow::startRescan);
    rootRow->addWidget(m_rescanButton);
    mainLayout->addLayout(rootRow);

    // Search row
    auto *searchRow = new QHBoxLayout;
    m_includeLabel = new QLabel;
    searchRow->addWidget(m_includeLabel);
    m_includeEdit = new QLineEdit;
    m_includeEdit->setClearButtonEnabled(true);
    searchRow->addWidget(m_includeEdit, 1);
    m_exactCheck = new QCheckBox;
    connect(m_exactCheck, &QCheckBox::toggled, this, [this](bool on) { m_excludeEdit->setEnabled(!on); });
    searchRow->addWidget(m_exactCheck);
    m_excludeLabel = new QLabel;
    searchRow->addWidget(m_excludeLabel);
    m_excludeEdit = new QLineEdit;
    m_excludeEdit->setClearButtonEnabled(true);
    searchRow->addWidget(m_excludeEdit, 1);
    m_searchButton = new QPushButton;
    m_searchButton->setDefault(true);
    connect(m_searchButton, &QPushButton::clicked, this, &MainWindow::applySearch);
    connect(m_includeEdit, &QLineEdit::returnPressed, this, &MainWindow::applySearch);
    connect(m_excludeEdit, &QLineEdit::returnPressed, this, &MainWindow::applySearch);
    searchRow->addWidget(m_searchButton);
    m_clearSearchButton = new QPushButton;
    connect(m_clearSearchButton, &QPushButton::clicked, this, &MainWindow::clearSearch);
    searchRow->addWidget(m_clearSearchButton);
    searchRow->addSpacing(12);
    m_sortLabel = new QLabel;
    searchRow->addWidget(m_sortLabel);
    m_sortCombo = new QComboBox;
    for (Db::SortOrder order : {Db::SortOrder::Classic, Db::SortOrder::NewestFirst, Db::SortOrder::LargestFirst})
        m_sortCombo->addItem(QString(), int(order));
    connect(m_sortCombo, &QComboBox::currentIndexChanged, this, [this] {
        Db::setSetting(SortOrderKey, QString::number(m_sortCombo->currentData().toInt()));
        loadPage(0);
    });
    searchRow->addWidget(m_sortCombo);
    mainLayout->addLayout(searchRow);

    // Tags list | thumbnail grid
    auto *splitter = new QSplitter(Qt::Horizontal);

    m_tagsBox = new QGroupBox;
    auto *tagsLayout = new QVBoxLayout(m_tagsBox);
    m_hideForeignTagsCheck = new QCheckBox;
    connect(m_hideForeignTagsCheck, &QCheckBox::toggled, this, &MainWindow::reloadTags);
    tagsLayout->addWidget(m_hideForeignTagsCheck);
    m_tagList = new QListWidget;
    m_tagList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tagList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        addTagToEdit(m_includeEdit, item->text());
        applySearch();
    });
    connect(m_tagList, &QWidget::customContextMenuRequested, this, &MainWindow::showTagMenu);
    tagsLayout->addWidget(m_tagList);
    splitter->addWidget(m_tagsBox);

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

        // Selection for bulk tag editing; it is kept by image id across pages and searches.
        auto *check = new QCheckBox(button);
        check->setStyleSheet("QCheckBox { background-color: rgba(255, 255, 255, 200); border-radius: 3px;"
                             " padding: 2px; }");
        check->move(4, 4);
        check->hide();
        connect(check, &QCheckBox::toggled, this, [this, i](bool on) {
            if (i >= m_pageRecords.size())
                return;
            if (on)
                m_selectedIds.insert(m_pageRecords[i].id);
            else
                m_selectedIds.remove(m_pageRecords[i].id);
            updateSelectionUi();
        });
        m_thumbChecks << check;
    }
    splitter->addWidget(gridHost);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({200, 800});
    mainLayout->addWidget(splitter, 1);

    // Paging row
    auto *pageRow = new QHBoxLayout;
    pageRow->addStretch();
    m_prevButton = new QPushButton;
    connect(m_prevButton, &QPushButton::clicked, this, &MainWindow::showPreviousPage);
    pageRow->addWidget(m_prevButton);
    m_pageLabel = new QLabel;
    m_pageLabel->setMinimumWidth(160);
    m_pageLabel->setAlignment(Qt::AlignCenter);
    pageRow->addWidget(m_pageLabel);
    m_nextButton = new QPushButton;
    connect(m_nextButton, &QPushButton::clicked, this, &MainWindow::showNextPage);
    pageRow->addWidget(m_nextButton);
    pageRow->addSpacing(24);
    // Bulk selection: "select all" works on this page, "clear all" on every page.
    m_selectAllButton = new QPushButton;
    connect(m_selectAllButton, &QPushButton::clicked, this, [this] {
        for (QCheckBox *box : std::as_const(m_thumbChecks))
            if (!box->isHidden())
                box->setChecked(true);
    });
    pageRow->addWidget(m_selectAllButton);
    m_clearSelectionButton = new QPushButton;
    connect(m_clearSelectionButton, &QPushButton::clicked, this, &MainWindow::clearSelection);
    pageRow->addWidget(m_clearSelectionButton);
    m_selectionLabel = new QLabel;
    m_selectionLabel->setMinimumWidth(110);
    pageRow->addWidget(m_selectionLabel);
    pageRow->addStretch();
    mainLayout->addLayout(pageRow);

    retranslateUi();
    resize(1150, 900);
}

// Every fixed text of the main window. Called once from buildUi() and again on every
// language change; texts that depend on data are refreshed by the caller.
void MainWindow::retranslateUi()
{
    m_fileMenu->setTitle(tr("&Файл"));
    m_createGalleryAction->setText(tr("&Створити галерею…"));
    m_editGalleryAction->setText(tr("&Редагувати галерею…"));
    m_rescanAction->setText(tr("Рес&кан"));
    m_addFilesAction->setText(tr("Додати &файли в галерею…"));
    m_addFolderAction->setText(tr("Додати п&апку в галерею…"));
    m_duplicatesAction->setText(tr("Знайти &дублікати…"));
    m_junkAction->setText(tr("Видалити &сміття…"));
    m_quitAction->setText(tr("&Вийти"));

    m_galleryLabel->setText(tr("Галерея:"));
    m_createGalleryButton->setText(tr("Створити"));
    m_editGalleryButton->setText(tr("Редагувати"));
    m_rescanButton->setText(tr("Рескан"));
    m_rescanButton->setToolTip(tr("Перечитати папку: додати нові файли, прибрати зниклі (F5)"));

    m_includeLabel->setText(tr("Містить теги:"));
    m_includeEdit->setPlaceholderText(tr("тег1, тег2 (усі одночасно)"));
    m_exactCheck->setText(tr("Жорстко"));
    m_exactCheck->setToolTip(tr("Лише файли, що мають рівно ці теги й жодних інших.\n"
                                "Поле «Не містить» тоді не враховується."));
    m_excludeLabel->setText(tr("Не містить:"));
    m_excludeEdit->setPlaceholderText(tr("тег3, тег4 (жодного з них)"));
    m_searchButton->setText(tr("Шукати"));
    m_clearSearchButton->setText(tr("Скинути"));
    m_sortLabel->setText(tr("Сортування:"));
    m_sortCombo->setItemText(0, tr("Класичне"));
    m_sortCombo->setItemText(1, tr("Спочатку новіші"));
    m_sortCombo->setItemText(2, tr("Спершу більші"));
    m_sortCombo->setToolTip(tr("Класичне: за віртуальним шляхом (усі теги файлу за абеткою через «/»),\nпотім за оцінкою (більші спочатку), "
                               "потім за іменем файлу.\n"
                               "Спочатку новіші: за датою додавання в галерею, потім як класичне.\n"
                               "Спершу більші: за розміром файлу (найбільші спочатку), потім як класичне."));

    m_tagsBox->setTitle(tr("Теги"));
    m_hideForeignTagsCheck->setText(tr("Сховати чужі теги"));
    m_hideForeignTagsCheck->setToolTip(tr("Показувати лише теги файлів, знайдених останнім пошуком"));
    m_tagList->setToolTip(tr("Подвійний клік — додати до «Містить».\n"
                             "Правий клік — вибрати, куди додати."));
    for (QCheckBox *check : std::as_const(m_thumbChecks))
        check->setToolTip(tr("Обрати для групового редагування тегів (правий клік на будь-якому прев'ю)"));

    m_prevButton->setText(tr("◀ Попередня"));
    m_nextButton->setText(tr("Наступна ▶"));
    m_selectAllButton->setText(tr("Обрати всі"));
    m_selectAllButton->setToolTip(tr("Відмітити всі файли на цій сторінці"));
    m_clearSelectionButton->setText(tr("Зняти всі"));
    m_clearSelectionButton->setToolTip(tr("Зняти позначки з усіх файлів на всіх сторінках"));
}

void MainWindow::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange && m_fileMenu) {
        retranslateUi();
        // Texts built from data: gallery root line, tag list ("<без тегів>"), page label, tooltips.
        if (const Gallery *gallery = currentGallery())
            m_rootLabel->setText(QDir::toNativeSeparators(gallery->rootFolder));
        else
            m_rootLabel->setText(tr("Галерей ще немає — натисни «Створити»."));
        m_includeEdit->setText(displayTags(m_includeTags).join(", "));
        m_excludeEdit->setText(displayTags(m_excludeTags).join(", "));
        reloadTags();
        refreshPageTexts();
        updateSelectionUi();
    }
    QMainWindow::changeEvent(event);
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
    m_selectedIds.clear(); // the selection belongs to the previous gallery
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
        QString message = tr("Додано: %1, переміщено: %2, оновлено: %3, видалено: %4, без змін: %5")
                              .arg(stats.added).arg(stats.moved).arg(stats.updated).arg(stats.removed)
                              .arg(stats.unchanged);
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

// The service tag is stored/searched as Db::NoTagsPseudoTag but shown in the UI language.
QString MainWindow::noTagsDisplayName()
{
    return tr("<без тегів>");
}

QStringList MainWindow::displayTags(const QStringList &tags)
{
    QStringList shown = tags;
    shown.replaceInStrings(QRegularExpression("^" + QRegularExpression::escape(Db::NoTagsPseudoTag) + "$"),
                           noTagsDisplayName());
    return shown;
}

QStringList MainWindow::parseTags(const QString &text)
{
    const QString shownNoTags = Db::normalizeTag(noTagsDisplayName());
    QStringList tags;
    for (const QString &part : text.split(',', Qt::SkipEmptyParts)) {
        QString tag = Db::normalizeTag(part);
        if (tag == shownNoTags)
            tag = Db::NoTagsPseudoTag;
        if (!tag.isEmpty() && !tags.contains(tag))
            tags << tag;
    }
    return tags;
}

void MainWindow::applySearch()
{
    m_includeTags = parseTags(m_includeEdit->text());
    m_excludeTags = parseTags(m_excludeEdit->text());
    m_exactTags = m_exactCheck->isChecked();
    // Show what is actually searched for (normalized spelling).
    m_includeEdit->setText(displayTags(m_includeTags).join(", "));
    m_excludeEdit->setText(displayTags(m_excludeTags).join(", "));
    loadPage(0);
    if (m_hideForeignTagsCheck->isChecked())
        reloadTags(); // the list follows the latest search
}

void MainWindow::clearSearch()
{
    m_includeEdit->clear();
    m_excludeEdit->clear();
    m_exactCheck->setChecked(false);
    applySearch();
}

void MainWindow::addTagToEdit(QLineEdit *edit, const QString &tag)
{
    QStringList tags = parseTags(edit->text());
    const QString internal = parseTags(tag).value(0);
    if (!internal.isEmpty() && !tags.contains(internal))
        tags << internal;
    edit->setText(displayTags(tags).join(", "));
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
    if (m_tagList->row(item) != 0) { // row 0 is the service tag, which has a fixed name
        menu.addSeparator();
        menu.addAction(tr("Перейменувати…"), this, [this, tag] { renameTag(tag); });
    }
    menu.exec(m_tagList->viewport()->mapToGlobal(pos));
}

void MainWindow::renameTag(const QString &oldName)
{
    const Gallery *gallery = currentGallery();
    if (isBusy() || !gallery)
        return;
    const qint64 galleryId = gallery->id;

    QString text = oldName;
    for (;;) {
        bool ok = false;
        text = QInputDialog::getText(this, tr("Перейменувати тег"), tr("Нова назва тегу «%1»:").arg(oldName),
                                     QLineEdit::Normal, text, &ok);
        if (!ok)
            return;
        const QString newName = Db::normalizeTag(text);
        if (newName.isEmpty() || newName == oldName)
            return; // nothing to change
        if (newName.contains(',') || newName.contains('<') || newName.contains('>')
            || newName.contains('/') || newName.contains('\\')) {
            QMessageBox::warning(this, tr("Перейменувати тег"), tr("Назва тегу не може містити символи «,», «<», «>», «/» і «\\»."));
            continue;
        }
        if (Db::allTags(galleryId).contains(newName)) {
            QMessageBox::warning(this, tr("Перейменувати тег"),
                                 tr("Тег «%1» вже існує. Перейменування скасовано.").arg(newName));
            return;
        }
        if (!Db::renameTag(galleryId, oldName, newName)) {
            QMessageBox::warning(this, tr("Помилка БД"), tr("Не вдалося перейменувати тег:\n%1").arg(Db::lastError()));
            return;
        }

        // Keep the search fields pointing at the same tag.
        for (QLineEdit *edit : {m_includeEdit, m_excludeEdit}) {
            QStringList tags = parseTags(edit->text());
            const qsizetype i = tags.indexOf(oldName);
            if (i >= 0) {
                tags[i] = newName;
                edit->setText(displayTags(tags).join(", "));
            }
        }
        m_includeTags.replaceInStrings(QRegularExpression("^" + QRegularExpression::escape(oldName) + "$"), newName);
        m_excludeTags.replaceInStrings(QRegularExpression("^" + QRegularExpression::escape(oldName) + "$"), newName);

        statusBar()->showMessage(tr("Тег «%1» перейменовано на «%2»").arg(oldName, newName), 5000);
        reloadTags();
        loadPage(m_page);
        return;
    }
}

void MainWindow::reloadTags()
{
    m_tagList->clear();
    const Gallery *gallery = currentGallery();
    if (!gallery)
        return;

    // Service tag: always on top, always present (also with "hide foreign tags"), not stored in the DB.
    auto *noTags = new QListWidgetItem(noTagsDisplayName(), m_tagList);
    QFont font = noTags->font();
    font.setItalic(true);
    noTags->setFont(font);
    noTags->setForeground(palette().color(QPalette::PlaceholderText));
    noTags->setToolTip(tr("Зображення, у яких немає жодного тегу"));

    if (m_hideForeignTagsCheck->isChecked())
        m_tagList->addItems(Db::tagsOfSearch(gallery->id, m_includeTags, m_excludeTags, m_exactTags));
    else
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
                                     &m_totalCount, sortOrder(), m_exactTags);
    if (m_pageRecords.isEmpty() && page > 0 && m_totalCount > 0) {
        // The page shrank (e.g. after editing tags): show the last one that exists.
        loadPage((m_totalCount - 1) / PageSize);
        return;
    }

    const int pageCount = qMax(1, (m_totalCount + PageSize - 1) / PageSize);
    m_prevButton->setEnabled(m_page > 0);
    m_nextButton->setEnabled(m_page + 1 < pageCount);

    for (int i = 0; i < m_thumbButtons.size(); ++i) {
        QToolButton *button = m_thumbButtons[i];
        button->setIcon(QIcon());
        button->setEnabled(i < m_pageRecords.size());
        QCheckBox *check = m_thumbChecks[i];
        {
            const QSignalBlocker blocker(check); // just showing the stored selection
            check->setChecked(i < m_pageRecords.size() && m_selectedIds.contains(m_pageRecords[i].id));
        }
        check->setVisible(i < m_pageRecords.size()
                          && MediaFiles::kindOf(m_pageRecords[i].relativePath) != MediaFiles::Kind::Other);
    }
    refreshPageTexts();
    updateSelectionUi();

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
        return MediaFiles::makeThumbnail(path, ThumbnailSize);
    }));
}

// Page label and the name/tooltip of every thumbnail (no reloading of the pictures).
void MainWindow::refreshPageTexts()
{
    const int pageCount = qMax(1, (m_totalCount + PageSize - 1) / PageSize);
    m_pageLabel->setText(tr("Сторінка %1 з %2 (%3 зобр.)").arg(m_page + 1).arg(pageCount).arg(m_totalCount));
    for (int i = 0; i < m_thumbButtons.size(); ++i) {
        QToolButton *button = m_thumbButtons[i];
        if (i < m_pageRecords.size()) {
            const ImageRecord &rec = m_pageRecords[i];
            const QString name = QFileInfo(rec.relativePath).fileName();
            button->setText(button->fontMetrics().elidedText(name, Qt::ElideMiddle, ThumbnailSize));
            QString hint;
            switch (MediaFiles::kindOf(rec.relativePath)) {
            case MediaFiles::Kind::Image:
                hint = tr("Клік — відкрити, правий клік — редагувати");
                break;
            case MediaFiles::Kind::Video:
                hint = tr("Клік — відкрити в програмі для відео, правий клік — редагувати");
                break;
            case MediaFiles::Kind::Other:
                break;
            }
            button->setToolTip(QDir::toNativeSeparators(rec.relativePath) + "\n"
                               + tr("Оцінка: ") + QString::number(rec.rating) + "\n"
                               + tr("Розмір: ") + QLocale().formattedDataSize(rec.size) + "\n"
                               + tr("Теги: ") + Db::tagsForImage(rec.id).join(", ")
                               + (hint.isEmpty() ? QString() : "\n\n" + hint));
        } else {
            button->setText(QString());
            button->setToolTip(QString());
        }
    }
}

void MainWindow::openThumbnail(int index)
{
    const Gallery *gallery = currentGallery();
    if (!gallery || index < 0 || index >= m_pageRecords.size())
        return;
    const QString path = absolutePath(m_pageRecords[index].relativePath);
    const MediaFiles::Kind kind = MediaFiles::kindOf(path);
    if (kind == MediaFiles::Kind::Other)
        return; // nothing to open for now
    if (!QFileInfo::exists(path)) {
        QMessageBox::warning(this, tr("Файл не знайдено"),
                             tr("Файлу більше немає:\n%1\n\nЗроби рескан.").arg(QDir::toNativeSeparators(path)));
        return;
    }

    if (kind == MediaFiles::Kind::Video) {
        if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path)))
            QMessageBox::warning(this, tr("Відео"),
                                 tr("Не вдалося відкрити файл:\n%1\n\nМожливо, у Windows немає програми для цього типу файлів.")
                                     .arg(QDir::toNativeSeparators(path)));
        return;
    }

    // The arrows in the viewer walk through the images of the whole search result
    // (not just this page); videos and other files are skipped.
    const QList<ImageRecord> all = Db::searchImages(gallery->id, m_includeTags, m_excludeTags, 0, -1, nullptr,
                                                    sortOrder(), m_exactTags);
    QStringList paths;
    paths.reserve(all.size());
    int current = 0;
    for (const ImageRecord &rec : all) {
        if (MediaFiles::kindOf(rec.relativePath) != MediaFiles::Kind::Image)
            continue;
        if (rec.id == m_pageRecords[index].id)
            current = int(paths.size());
        paths << absolutePath(rec.relativePath);
    }
    if (paths.isEmpty())
        paths << path;

    auto *viewer = new ImageViewer(paths, current, gallery->allowUpscale, this);
    // A rotated file was saved: refresh its record (size/hash) and the thumbnail.
    const qint64 galleryId = gallery->id;
    const QString root = gallery->rootFolder;
    connect(viewer, &ImageViewer::fileChanged, this, [this, galleryId, root](const QString &file) {
        fileRewritten(galleryId, root, file);
    });
    viewer->show();
}

void MainWindow::fileRewritten(qint64 galleryId, const QString &root, const QString &file)
{
    const QFileInfo fi(file);
    Db::updateImageFile(galleryId, QDir(root).relativeFilePath(file), fi.size(),
                        fi.lastModified(QTimeZone::UTC).toMSecsSinceEpoch(), Scanner::fileSha256(file));
    const Gallery *current = currentGallery();
    if (current && current->id == galleryId && !isBusy())
        loadPage(m_page);
}

// Connects what a metadata dialog needs from the main window.
void MainWindow::wireMetaDialog(ImageMetaDialog &dialog)
{
    const Gallery *gallery = currentGallery();
    dialog.setAllowUpscale(gallery->allowUpscale);
    connect(&dialog, &ImageMetaDialog::tagActivated, this, [this](const QString &tag) {
        addTagToEdit(m_includeEdit, tag); // the search itself waits for "Шукати"
    });
    const qint64 galleryId = gallery->id;
    const QString root = gallery->rootFolder;
    connect(&dialog, &ImageMetaDialog::fileChanged, this, [this, galleryId, root](const QString &file) {
        fileRewritten(galleryId, root, file);
    });
}

void MainWindow::editImageMeta(int index)
{
    const Gallery *gallery = currentGallery();
    if (isBusy() || !gallery || index < 0 || index >= m_pageRecords.size())
        return;
    const qint64 galleryId = gallery->id;

    if (m_selectedIds.size() > 1) {
        editTagsOfSelection();
        return;
    }
    ImageRecord record = m_pageRecords[index];
    if (m_selectedIds.size() == 1) {
        // One file chosen (maybe on another page): edit that one, wherever the click was.
        const QList<ImageRecord> chosen = Db::imagesByIds(galleryId, m_selectedIds.values());
        if (chosen.isEmpty()) {
            clearSelection(); // it no longer exists
        } else {
            record = chosen.first();
        }
    }
    if (MediaFiles::kindOf(record.relativePath) == MediaFiles::Kind::Other)
        return; // nothing to do for other files for now

    const QString path = absolutePath(record.relativePath);
    ImageMetaDialog dialog({path}, Db::tagsForImage(record.id), record.rating, Db::allTags(galleryId), this);
    wireMetaDialog(dialog);
    const int result = dialog.exec();

    if (result == ImageMetaDialog::DeleteRequested) {
        // A file already gone from disk only needs to leave the DB.
        if (QFileInfo::exists(path) && !QFile::moveToTrash(path)) {
            QMessageBox::warning(this, tr("Видалити файл"),
                                 tr("Не вдалося перемістити файл у Кошик, нічого не видалено:\n%1")
                                     .arg(QDir::toNativeSeparators(path)));
            return;
        }
        if (!Db::deleteImages(galleryId, {record.id}))
            QMessageBox::warning(this, tr("Помилка БД"),
                                 tr("Файл у Кошику, але прибрати запис з бази не вдалося:\n%1\n\nЗроби рескан.")
                                     .arg(Db::lastError()));
        m_selectedIds.remove(record.id);
        statusBar()->showMessage(tr("У Кошику: %1").arg(QFileInfo(record.relativePath).fileName()), 5000);
        reloadTags();
        loadPage(m_page);
        return;
    }
    if (result != QDialog::Accepted)
        return;

    if (!Db::saveImageMeta(galleryId, record.id, dialog.rating(), dialog.tags())) {
        QMessageBox::warning(this, tr("Помилка БД"), tr("Не вдалося зберегти зміни:\n%1").arg(Db::lastError()));
        return;
    }
    statusBar()->showMessage(tr("Збережено: %1").arg(QFileInfo(record.relativePath).fileName()), 5000);
    reloadTags();
    loadPage(m_page); // rating may have moved the image
}

void MainWindow::clearSelection()
{
    m_selectedIds.clear();
    for (QCheckBox *box : std::as_const(m_thumbChecks)) {
        const QSignalBlocker blocker(box);
        box->setChecked(false);
    }
    updateSelectionUi();
}

void MainWindow::updateSelectionUi()
{
    bool anyOnPage = false;
    bool allOnPage = true;
    for (QCheckBox *box : std::as_const(m_thumbChecks)) {
        if (box->isHidden())
            continue;
        anyOnPage = true;
        allOnPage = allOnPage && box->isChecked();
    }
    m_selectAllButton->setEnabled(anyOnPage && !allOnPage);
    m_clearSelectionButton->setEnabled(!m_selectedIds.isEmpty());
    m_selectionLabel->setText(m_selectedIds.isEmpty() ? QString()
                                                      : tr("Обрано: %1").arg(m_selectedIds.size()));
}

void MainWindow::editTagsOfSelection()
{
    const Gallery *gallery = currentGallery();
    if (!gallery)
        return;
    const qint64 galleryId = gallery->id;

    // The selection may span pages and searches; files deleted meanwhile drop out.
    const QList<ImageRecord> records = Db::imagesByIds(galleryId, m_selectedIds.values());
    m_selectedIds.clear();
    for (const ImageRecord &rec : records)
        m_selectedIds.insert(rec.id);
    updateSelectionUi();
    if (records.size() < 2) {
        loadPage(m_page);
        return;
    }

    QStringList paths;
    QList<qint64> ids;
    QStringList common;
    bool first = true;
    for (const ImageRecord &rec : records) {
        paths << absolutePath(rec.relativePath);
        ids << rec.id;
        const QStringList tags = Db::tagsForImage(rec.id);
        if (first) {
            common = tags;
            first = false;
        } else {
            common.erase(std::remove_if(common.begin(), common.end(),
                                        [&tags](const QString &t) { return !tags.contains(t); }),
                         common.end());
        }
    }

    ImageMetaDialog dialog(paths, common, 50, Db::allTags(galleryId), this);
    wireMetaDialog(dialog);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const QStringList added = dialog.addedTags();
    const QStringList removed = dialog.removedTags();
    if (added.isEmpty() && removed.isEmpty())
        return;

    if (!Db::applyTagChanges(galleryId, ids, added, removed)) {
        QMessageBox::warning(this, tr("Помилка БД"), tr("Не вдалося зберегти зміни:\n%1").arg(Db::lastError()));
        return;
    }
    statusBar()->showMessage(tr("Теги оновлено у файлів: %1").arg(ids.size()), 5000);
    reloadTags();
    loadPage(m_page);
}

Db::SortOrder MainWindow::sortOrder() const
{
    return Db::SortOrder(m_sortCombo->currentData().toInt());
}

void MainWindow::findDuplicates()
{
    const Gallery *gallery = currentGallery();
    if (isBusy())
        return;
    if (!gallery) {
        QMessageBox::information(this, tr("Дублікати"), tr("Спершу створи галерею."));
        return;
    }

    DuplicatesDialog dialog(*gallery, this);
    dialog.exec();
    if (dialog.changed()) {
        reloadTags();
        loadPage(m_page);
    }
}

void MainWindow::deleteJunk()
{
    const Gallery *gallery = currentGallery();
    if (isBusy())
        return;
    if (!gallery) {
        QMessageBox::information(this, tr("Сміття"), tr("Спершу створи галерею."));
        return;
    }
    if (!QFileInfo(gallery->rootFolder).isDir()) {
        QMessageBox::warning(this, tr("Сміття"),
                             tr("Папка недоступна:\n%1").arg(QDir::toNativeSeparators(gallery->rootFolder)));
        return;
    }

    auto *watcher = new QFutureWatcher<Scanner::JunkScan>(this);
    runBusy(watcher, tr("Шукаю сміття…"));
    const QString root = gallery->rootFolder;
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, root] {
        const Scanner::JunkScan junk = watcher->result();
        if (junk.junkFiles.isEmpty() && junk.emptyFolders.isEmpty()) {
            QMessageBox::information(this, tr("Сміття"), tr("Сміття не знайдено."));
            return;
        }

        auto sortedRelative = [&root](const QStringList &paths) {
            QStringList list;
            for (const QString &path : paths)
                list << QDir::toNativeSeparators(QDir(root).relativeFilePath(path));
            std::sort(list.begin(), list.end(), [](const QString &a, const QString &b) {
                return QString::compare(a, b, Qt::CaseInsensitive) < 0;
            });
            return list;
        };
        QString details;
        if (!junk.junkFiles.isEmpty())
            details += tr("Файли:\n") + sortedRelative(junk.junkFiles).join('\n') + "\n\n";
        if (!junk.emptyFolders.isEmpty())
            details += tr("Папки:\n") + sortedRelative(junk.emptyFolders).join('\n');

        QMessageBox box(QMessageBox::Warning, tr("Сміття"),
                        tr("Знайдено файлів Thumbs.db / desktop.ini: %1, пустих папок: %2.\n"
                           "Видалити їх остаточно (без Кошика)?")
                            .arg(junk.junkFiles.size())
                            .arg(junk.emptyFolders.size()),
                        QMessageBox::Yes | QMessageBox::No, this);
        box.setInformativeText(tr("Повний список — у подробицях (кнопка нижче)."));
        box.setDetailedText(details.trimmed());
        box.setDefaultButton(QMessageBox::No);
        if (box.exec() != QMessageBox::Yes)
            return;

        QApplication::setOverrideCursor(Qt::WaitCursor);
        // Junk files first; failures are silently skipped (they are often locked by Explorer).
        int removedFiles = 0;
        QStringList keptJunk;
        for (const QString &file : junk.junkFiles) {
            QFile::setPermissions(file, QFile::permissions(file) | QFile::WriteOwner | QFile::WriteUser);
            if (QFile::remove(file))
                ++removedFiles;
            else
                keptJunk << file;
        }

        // Then folders, deepest first, so a parent is already empty when its turn comes.
        int removedFolders = 0;
        QStringList failed;
        for (const QString &folder : junk.emptyFolders) {
            if (QDir().rmdir(folder)) {
                ++removedFolders;
                continue;
            }
            // A junk file we couldn't delete keeps its folders: that's expected, not an error.
            const bool explained = std::any_of(keptJunk.cbegin(), keptJunk.cend(), [&folder](const QString &f) {
                return f.startsWith(folder + '/', Qt::CaseInsensitive);
            });
            if (!explained)
                failed << QDir::toNativeSeparators(folder);
        }
        QApplication::restoreOverrideCursor();

        statusBar()->showMessage(tr("Видалено файлів: %1, папок: %2").arg(removedFiles).arg(removedFolders), 10000);
        if (!failed.isEmpty())
            QMessageBox::warning(this, tr("Сміття"),
                                 tr("Ці папки видалити не вдалося (можливо, зайняті або немає прав):\n\n%1")
                                     .arg(failed.join('\n')));
    });
    watcher->setFuture(QtConcurrent::run([root] { return Scanner::findJunk(root); }));
}

QString MainWindow::chooseRootTag(const QString &title, const QString &suggestion)
{
    const Gallery *gallery = currentGallery();
    if (!gallery)
        return {};
    QStringList folders = QDir(gallery->rootFolder)
                              .entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden,
                                         QDir::Name | QDir::IgnoreCase);
    int current = 0;
    if (!suggestion.isEmpty()) {
        current = int(folders.indexOf(suggestion));
        if (current < 0) {
            folders.prepend(suggestion);
            current = 0;
        }
    }

    for (;;) {
        bool ok = false;
        const QString name = QInputDialog::getItem(this, title,
                                                   tr("Папка галереї (вона ж перший тег).\n"
                                                      "Можна вписати нову назву — папку буде створено:"),
                                                   folders, current, true, &ok)
                                 .trimmed();
        if (!ok)
            return {};
        // Windows-forbidden characters only; a comma is fine (the tag gets a space instead).
        static const QRegularExpression forbidden(QStringLiteral(R"([\\/:*?"<>|])"));
        if (name.isEmpty() || name == "." || name == ".." || name.contains(forbidden) || name.endsWith('.')) {
            QMessageBox::warning(this, title,
                                 tr("Недопустима назва папки. Не можна використовувати символи \\ / : * ? \" < > |"));
            continue;
        }
        return name;
    }
}

void MainWindow::addFilesToGallery()
{
    const Gallery *gallery = currentGallery();
    if (isBusy())
        return;
    if (!gallery) {
        QMessageBox::information(this, tr("Додати файли"), tr("Спершу створи галерею."));
        return;
    }
    const QStringList files = QFileDialog::getOpenFileNames(this, tr("Обери файли для додавання в галерею"));
    if (files.isEmpty())
        return;
    const QString tag = chooseRootTag(tr("Додати файли"), QString());
    if (tag.isEmpty())
        return;

    runImport([files, tag] {
        QList<Scanner::ImportItem> items;
        for (const QString &file : files)
            items.append({file, tag});
        return items;
    });
}

void MainWindow::addFolderToGallery()
{
    const Gallery *gallery = currentGallery();
    if (isBusy())
        return;
    if (!gallery) {
        QMessageBox::information(this, tr("Додати папку"), tr("Спершу створи галерею."));
        return;
    }
    const QString source = QFileDialog::getExistingDirectory(this, tr("Обери папку для додавання в галерею"));
    if (source.isEmpty())
        return;

    const QDir sourceDir(source);
    const QDir::Filters fileFilter = QDir::Files | QDir::Readable | QDir::Hidden | QDir::System;
    bool hasOwnFiles = false;
    for (const QString &name : sourceDir.entryList(fileFilter))
        if (!Scanner::isIgnoredFile(name)) {
            hasOwnFiles = true;
            break;
        }

    // Every file under `from`, going to `tag` + its subfolder path below `from`.
    auto collectInto = [fileFilter](const QString &from, const QString &tag, QList<Scanner::ImportItem> &items) {
        const QDir base(from);
        QDirIterator it(from, fileFilter, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString file = it.next();
            if (Scanner::isIgnoredFile(file))
                continue;
            const QString sub = base.relativeFilePath(QFileInfo(file).absolutePath());
            items.append({file, QDir::cleanPath(tag + '/' + sub)});
        }
    };

    if (hasOwnFiles) {
        // Files right in the chosen folder: everything goes under one root tag.
        const QString tag = chooseRootTag(tr("Додати папку"), sourceDir.dirName());
        if (tag.isEmpty())
            return;
        runImport([source, tag, collectInto] {
            QList<Scanner::ImportItem> items;
            collectInto(source, tag, items);
            return items;
        });
        return;
    }

    // Only subfolders: each first-level subfolder is a root tag of its own.
    const QStringList subfolders = sourceDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden);
    if (subfolders.isEmpty()) {
        QMessageBox::information(this, tr("Додати папку"), tr("У цій папці немає файлів для додавання."));
        return;
    }
    runImport([sourceDir, subfolders, collectInto] {
        QList<Scanner::ImportItem> items;
        for (const QString &sub : subfolders)
            collectInto(sourceDir.filePath(sub), sub, items);
        return items;
    });
}

void MainWindow::runImport(std::function<QList<Scanner::ImportItem>()> collect)
{
    const Gallery *gallery = currentGallery();
    if (!gallery)
        return;
    const qint64 galleryId = gallery->id;
    const QString root = gallery->rootFolder;

    // Step 1: walk the source folders off the UI thread.
    auto *collector = new QFutureWatcher<QList<Scanner::ImportItem>>(this);
    runBusy(collector, tr("Шукаю файли для додавання…"));
    connect(collector, &QFutureWatcherBase::finished, this, [this, collector, galleryId, root] {
        const QList<Scanner::ImportItem> items = collector->result();
        if (items.isEmpty()) {
            QMessageBox::information(this, tr("Додавання в галерею"), tr("Немає файлів для додавання."));
            return;
        }

        // Step 2: copy + register.
        auto *importer = new QFutureWatcher<Scanner::ImportStats>(this);
        runBusy(importer, tr("Додаю файли в галерею…"));
        connect(importer, &QFutureWatcherBase::finished, this, [this, importer] {
            const Scanner::ImportStats stats = importer->future().resultCount() > 0 ? importer->result()
                                                                                     : Scanner::ImportStats{};
            reloadTags();
            loadPage(0);
            QString message = tr("Скопійовано — %1, дублікатів — %2.").arg(stats.copied).arg(stats.duplicates);
            if (stats.ignored > 0)
                message += "\n" + tr("Пропущено службових файлів (Thumbs.db, desktop.ini тощо) — %1.").arg(stats.ignored);
            if (!stats.failed.isEmpty())
                message += "\n\n" + tr("Не вдалося скопіювати:\n%1").arg(stats.failed.join('\n'));
            if (!stats.error.isEmpty())
                message += "\n\n" + stats.error;
            if (stats.error.isEmpty() && stats.failed.isEmpty())
                QMessageBox::information(this, tr("Додавання в галерею"), message);
            else
                QMessageBox::warning(this, tr("Додавання в галерею"), message);
        });
        importer->setFuture(QtConcurrent::run([galleryId, root, items](QPromise<Scanner::ImportStats> &promise) {
            Scanner::importFiles(promise, galleryId, root, items);
        }));
    });
    collector->setFuture(QtConcurrent::run(std::move(collect)));
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
