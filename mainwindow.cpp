#include "mainwindow.h"
#include "./ui_mainwindow.h"

#include "busydialog.h"
#include "collectiondialog.h"
#include "downloaddialog.h"
#include "duplicatesdialog.h"
#include "foldersync.h"
#include "gallerydialog.h"
#include "i18n.h"
#include "imagemetadialog.h"
#include "imageviewer.h"
#include "mediafiles.h"
#include "scanner.h"
#include "thumbcache.h"
#include "trashprompt.h"
#include "wrappedtext.h"

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
#include <QInputDialog>
#include <QRegularExpression>
#include <QFutureWatcher>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImageReader>
#include <QInputMethodEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollBar>
#include <QResizeEvent>
#include <QSplitter>
#include <QStyledItemDelegate>
#include <QTextLayout>
#include <QPainter>
#include <algorithm>
#include <cmath>
#include <QTextCharFormat>
#include <QToolButton>
#include <QVBoxLayout>
#include <QTimeZone>
#include <QUrl>
#include <QtConcurrent>

namespace {

const char *CurrentGalleryKey = "current_gallery";
const char *SortOrderKey = "sort_order";
const char *KeepExtensionKey = "keep_extension_on_rename"; // "0" = off; not set = on
const char *FollowFolderLinksKey = "follow_folder_links";    // "1" = on; not set = off
const char *ImportRootFolderKey = "import_root_folder";      // per gallery: the folder last chosen for an import

// Tag list entries as wide as the list, long names wrapped onto more lines instead of a
// horizontal scroll bar. Background, selection and colours (grey group headers) come from the style.
class WrappingItemDelegate : public QStyledItemDelegate
{
public:
    explicit WrappingItemDelegate(QAbstractItemView *view)
        : QStyledItemDelegate(view)
        , m_view(view)
    {
        view->viewport()->installEventFilter(this);
    }

    // The heights depend on the width of the visible part. It also changes when the vertical scroll
    // bar appears or goes, which the view's own Adjust mode misses: entries kept their old heights
    // and wrapped text ran into the next entry.
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == m_view->viewport() && event->type() == QEvent::Resize) {
            const int width = static_cast<QResizeEvent *>(event)->size().width();
            if (width != m_width) {
                m_width = width;
                // Queued: not inside the resize, which a new layout may cause again (scroll bar).
                QMetaObject::invokeMethod(m_view, [view = m_view] { view->doItemsLayout(); }, Qt::QueuedConnection);
            }
        }
        return QStyledItemDelegate::eventFilter(watched, event);
    }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);
        const auto *view = qobject_cast<const QAbstractItemView *>(opt.widget);
        const int width = view ? view->viewport()->width() : opt.rect.width();
        const int oneLine = QStyledItemDelegate::sizeHint(option, index).height();
        QTextLayout layout(opt.text, opt.font);
        const qreal textHeight = WrappedText::layout(layout, qMax(1, width - 2 * textMargin(opt)));
        // Same padding around the text as a one-line entry has.
        const int padding = oneLine - QFontMetrics(opt.font).height();
        return QSize(width, qMax(oneLine, int(std::ceil(textHeight)) + padding));
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);
        const QString text = opt.text;
        opt.text.clear(); // the style draws the panel only, the text is ours
        QStyle *style = opt.widget ? opt.widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);

        const int margin = textMargin(opt);
        const QRect rect = opt.rect.adjusted(margin, 0, -margin, 0);
        QTextLayout layout(text, opt.font);
        const qreal height = WrappedText::layout(layout, qMax(1, rect.width()));
        const QPalette::ColorGroup group = !(opt.state & QStyle::State_Enabled) ? QPalette::Disabled
                                         : (opt.state & QStyle::State_Active)   ? QPalette::Active
                                                                                : QPalette::Inactive;
        const QPalette::ColorRole role = (opt.state & QStyle::State_Selected) ? QPalette::HighlightedText
                                                                               : QPalette::Text;
        painter->save();
        painter->setPen(opt.palette.color(group, role));
        painter->setClipRect(opt.rect);
        layout.draw(painter, QPointF(rect.left(), rect.top() + (rect.height() - height) / 2));
        painter->restore();
    }

private:
    static int textMargin(const QStyleOptionViewItem &opt)
    {
        // As QCommonStyle places item text.
        const QStyle *style = opt.widget ? opt.widget->style() : QApplication::style();
        return style->pixelMetric(QStyle::PM_FocusFrameHMargin, nullptr, opt.widget) + 1;
    }

    QAbstractItemView *m_view;
    int m_width = -1; // of the viewport at the last layout request
};

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
    // An older version may have changed files in this DB without knowing collections.
    if (!Db::repairCollections())
        QMessageBox::warning(this, tr("Помилка БД"),
                             tr("Не вдалося привести колекції до ладу:\n%1\n\nЗроби рескан.").arg(Db::lastError()));
    ThumbCache::init();
    m_keepExtensionAction->setChecked(Db::setting(KeepExtensionKey) != "0");
    m_followLinksAction->setChecked(Db::setting(FollowFolderLinksKey) == "1");
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
    m_downloadAction = m_fileMenu->addAction(QString(), this, &MainWindow::downloadFromInternet);
    m_syncFolderAction = m_fileMenu->addAction(QString(), this, &MainWindow::syncWithFolder);
    m_fileMenu->addSeparator();
    m_duplicatesAction = m_fileMenu->addAction(QString(), this, &MainWindow::findDuplicates);
    m_junkAction = m_fileMenu->addAction(QString(), this, &MainWindow::deleteJunk);
    m_renameCollectionsAction = m_fileMenu->addAction(QString(), this, &MainWindow::renameAllCollections);
    m_fileMenu->addSeparator();
    m_quitAction = m_fileMenu->addAction(QString(), QKeySequence::Quit, this, &QWidget::close);

    m_settingsMenu = menuBar()->addMenu(QString());
    m_keepExtensionAction = m_settingsMenu->addAction(QString());
    m_keepExtensionAction->setCheckable(true);
    m_keepExtensionAction->setChecked(true);
    connect(m_keepExtensionAction, &QAction::triggered, this, [](bool checked) {
        Db::setSetting(KeepExtensionKey, checked ? "1" : "0");
    });
    m_followLinksAction = m_settingsMenu->addAction(QString());
    m_followLinksAction->setCheckable(true);
    connect(m_followLinksAction, &QAction::triggered, this, &MainWindow::setFollowFolderLinks);
    m_settingsMenu->setToolTipsVisible(true);

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
    for (QLineEdit *edit : {m_includeEdit, m_excludeEdit})
        connect(edit, &QLineEdit::textChanged, this, [this, edit] { highlightNoTags(edit); });
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

    // Collection mode: which collection is open, and the way back.
    m_collectionBar = new QWidget;
    auto *collectionRow = new QHBoxLayout(m_collectionBar);
    collectionRow->setContentsMargins(0, 0, 0, 0);
    m_closeCollectionButton = new QPushButton;
    m_closeCollectionButton->setShortcut(QKeySequence(Qt::Key_Escape)); // works only while shown
    connect(m_closeCollectionButton, &QPushButton::clicked, this, &MainWindow::closeCollection);
    collectionRow->addWidget(m_closeCollectionButton);
    m_collectionLabel = new QLabel;
    QFont collectionFont = m_collectionLabel->font();
    collectionFont.setBold(true);
    m_collectionLabel->setFont(collectionFont);
    m_collectionLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_collectionLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_collectionLabel->setIndent(8);
    collectionRow->addWidget(m_collectionLabel, 1);
    m_collectionBar->hide();
    mainLayout->addWidget(m_collectionBar);

    // Tags list | thumbnail grid
    auto *splitter = new QSplitter(Qt::Horizontal);

    m_tagsBox = new QGroupBox;
    auto *tagsLayout = new QVBoxLayout(m_tagsBox);
    m_hideForeignTagsCheck = new QCheckBox;
    connect(m_hideForeignTagsCheck, &QCheckBox::toggled, this, &MainWindow::reloadTags);
    tagsLayout->addWidget(m_hideForeignTagsCheck);
    m_tagFilterEdit = new QLineEdit;
    m_tagFilterEdit->setClearButtonEnabled(true);
    connect(m_tagFilterEdit, &QLineEdit::textChanged, this, &MainWindow::applyTagFilter);
    tagsLayout->addWidget(m_tagFilterEdit);
    m_tagList = new QListWidget;
    // Long names wrap; entries are laid out again whenever the list's width changes.
    m_tagList->setItemDelegate(new WrappingItemDelegate(m_tagList));
    m_tagList->setResizeMode(QListView::Adjust);
    m_tagList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_tagList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tagList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        if (item->data(GroupHeaderRole).toBool())
            return;
        addTagToEdit(m_includeEdit, item->text());
        applySearch();
    });
    connect(m_tagList, &QWidget::customContextMenuRequested, this, &MainWindow::showTagMenu);
    tagsLayout->addWidget(m_tagList);
    splitter->addWidget(m_tagsBox);

    m_gridHost = new QWidget;
    m_gridLayout = new QGridLayout(m_gridHost);
    m_gridLayout->setSpacing(GridSpacing);
    // The grid follows the window (fitGridToSize): its thumbnails never hold the window at their size.
    m_gridLayout->setSizeConstraint(QLayout::SetNoConstraint);
    for (int i = 0; i < pageSize(); ++i)
        addThumbButton();
    placeThumbButtons();
    {
        const QMargins margins = m_gridLayout->contentsMargins();
        const QSize cell = m_thumbButtons.first()->minimumSize();
        m_gridHost->setMinimumSize(cell.width() + margins.left() + margins.right(),
                                   cell.height() + margins.top() + margins.bottom());
    }
    m_gridHost->installEventFilter(this);
    m_gridReloadTimer.setSingleShot(true);
    m_gridReloadTimer.setInterval(200); // dragging the window edge reloads once, at the end
    connect(&m_gridReloadTimer, &QTimer::timeout, this, [this] { loadPage(m_offset); });
    splitter->addWidget(m_gridHost);
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
    // Bulk selection: "select all" takes the whole open collection, "select page" this page,
    // "clear all" clears every page.
    m_selectAllButton = new QPushButton;
    connect(m_selectAllButton, &QPushButton::clicked, this, &MainWindow::selectWholeCollection);
    pageRow->addWidget(m_selectAllButton);
    m_selectPageButton = new QPushButton;
    connect(m_selectPageButton, &QPushButton::clicked, this, [this] {
        for (QCheckBox *box : std::as_const(m_thumbChecks))
            if (!box->isHidden())
                box->setChecked(true);
    });
    pageRow->addWidget(m_selectPageButton);
    m_clearSelectionButton = new QPushButton;
    connect(m_clearSelectionButton, &QPushButton::clicked, this, &MainWindow::clearSelection);
    pageRow->addWidget(m_clearSelectionButton);
    m_selectionLabel = new QLabel;
    m_selectionLabel->setMinimumWidth(110);
    pageRow->addWidget(m_selectionLabel);
    pageRow->addStretch();
    mainLayout->addLayout(pageRow);

    retranslateUi();
    // Start size and the smallest the window can be made (owner's decision).
    setMinimumSize(800, 400);
    resize(800, 600);
}

// Every fixed text of the main window. Called once from buildUi() and again on every
// language change; texts that depend on data are refreshed by the caller.
void MainWindow::retranslateUi()
{
    m_fileMenu->setTitle(tr("&Файл"));
    m_settingsMenu->setTitle(tr("&Налаштування"));
    m_keepExtensionAction->setText(tr("Зберігати розширення при перейменуванні"));
    m_followLinksAction->setText(tr("Заходити в junction-папки"));
    m_followLinksAction->setToolTip(
        tr("Рескан і «Додати папку» заходять також у junction-папки й символьні посилання на папки.\n"
           "Кожна папка обходиться лише раз, тож зациклення не буде.\n"
           "Якщо зняти галочку, наступний рескан прибере з бази файли, знайдені через такі папки (разом з їхніми тегами).\n"
           "«Видалити сміття» в такі папки не заходить ніколи."));
    m_createGalleryAction->setText(tr("&Створити галерею…"));
    m_editGalleryAction->setText(tr("&Редагувати галерею…"));
    m_rescanAction->setText(tr("Рес&кан"));
    m_addFilesAction->setText(tr("Додати &файли в галерею…"));
    m_addFolderAction->setText(tr("Додати п&апку в галерею…"));
    m_downloadAction->setText(tr("Завантажити з &інтернету…"));
    m_syncFolderAction->setText(tr("Син&хронізувати з папкою…"));
    m_duplicatesAction->setText(tr("Знайти &дублікати…"));
    m_junkAction->setText(tr("Видалити &сміття…"));
    m_renameCollectionsAction->setText(tr("&Перейменувати всі колекції…"));
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
    m_sortCombo->setToolTip(tr("Порядок знайдених файлів. Колекція стоїть там, де її перший файл.\n"
                               "Класичне: за оцінкою колекції (більші спочатку), потім за ім'ям колекції, потім за її\n"
                               "тегами (за абеткою через «/»); останніми завжди ім'я файлу.\n"
                               "Спочатку новіші: за датою додавання в галерею, потім як класичне.\n"
                               "Спершу більші: за розміром файлу (найбільші спочатку), потім як класичне.\n"
                               "Усередині відкритої колекції файли завжди йдуть за ім'ям."));

    m_tagsBox->setTitle(tr("Теги"));
    m_hideForeignTagsCheck->setText(tr("Сховати чужі теги"));
    m_hideForeignTagsCheck->setToolTip(tr("Показувати лише теги файлів, знайдених останнім пошуком"));
    m_tagFilterEdit->setPlaceholderText(tr("Фільтр тегів…"));
    m_tagFilterEdit->setToolTip(tr("Показувати у списку лише теги, що містять цей текст (регістр не враховується)"));
    m_tagList->setToolTip(tr("Подвійний клік — додати до «Містить».\n"
                             "Правий клік — вибрати, куди додати."));
    for (QCheckBox *check : std::as_const(m_thumbChecks))
        check->setToolTip(tr("Обрати для групового редагування тегів (правий клік на будь-якому прев'ю)"));
    m_closeCollectionButton->setText(tr("◀ Закрити колекцію"));
    m_closeCollectionButton->setToolTip(tr("Повернутися до списку колекцій (Esc)"));

    m_prevButton->setText(tr("◀ Попередня"));
    m_nextButton->setText(tr("Наступна ▶"));
    m_selectAllButton->setText(tr("Обрати всі"));
    m_selectAllButton->setToolTip(tr("Відмітити всі файли відкритої колекції, на всіх сторінках"));
    m_selectPageButton->setText(tr("Обрати сторінку"));
    m_selectPageButton->setToolTip(tr("Відмітити всі файли на цій сторінці"));
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
        m_includeEdit->setText(tagsText(m_query.include, m_query.onlyUntagged));
        m_excludeEdit->setText(tagsText(m_query.exclude, m_query.skipUntagged));
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
    // Open viewers may hold an unsaved rotation: let them ask first; "Cancel" keeps the program open.
    for (ImageViewer *viewer : findChildren<ImageViewer *>(Qt::FindDirectChildrenOnly)) {
        if (!viewer->close()) {
            event->ignore();
            return;
        }
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

    m_openCollectionId = 0; // collections belong to their gallery
    updateCollectionModeUi();
    m_includeEdit->clear();
    m_excludeEdit->clear();
    m_exactCheck->setChecked(false);
    m_query = {};
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
    GalleryDialog dialog(Gallery{}, galleryNamesExcept(0), {}, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const qint64 id = Db::createGallery({0, dialog.name(), dialog.rootFolder(), dialog.allowUpscale()});
    if (id == 0) {
        QMessageBox::warning(this, tr("Помилка БД"), tr("Не вдалося створити галерею:\n%1").arg(Db::lastError()));
        return;
    }
    if (!Db::saveTagGroups(id, dialog.tagGroups()))
        QMessageBox::warning(this, tr("Помилка БД"), tr("Не вдалося зберегти групи тегів:\n%1").arg(Db::lastError()));
    reloadGalleries(id);
    askForRescan(tr("Галерею «%1» створено. Запустити рескан зараз?").arg(dialog.name()));
}

void MainWindow::editGallery()
{
    const Gallery *current = currentGallery();
    if (isBusy() || !current)
        return;
    const Gallery original = *current;

    GalleryDialog dialog(original, galleryNamesExcept(original.id), Db::tagGroups(original.id), this);
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
    if (!Db::saveTagGroups(updated.id, dialog.tagGroups()))
        QMessageBox::warning(this, tr("Помилка БД"), tr("Не вдалося зберегти групи тегів:\n%1").arg(Db::lastError()));
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

    runRescan(*gallery, [this](const ScanStats &stats) {
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
}

void MainWindow::runRescan(const Gallery &gallery, std::function<void(const ScanStats &)> done)
{
    auto *watcher = new QFutureWatcher<ScanStats>(this);
    runBusy(watcher, tr("Рескан…"));
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, done] {
        const ScanStats stats = watcher->future().resultCount() > 0 ? watcher->result() : ScanStats{};
        reloadTags();
        loadPage(0);
        done(stats);
    });

    const qint64 galleryId = gallery.id;
    const QString root = gallery.rootFolder;
    const bool followLinks = m_followLinksAction->isChecked();
    watcher->setFuture(QtConcurrent::run([galleryId, root, followLinks](QPromise<ScanStats> &promise) {
        Scanner::rescan(promise, galleryId, root, followLinks);
    }));
}

// Service entry of the search fields and the tag list: "images without tags". Never a real tag
// ('<' and '>' are not allowed in tag names), shown in the UI language.
QString MainWindow::noTagsDisplayName()
{
    return tr("<без тегів>");
}

QString MainWindow::tagsText(const QStringList &tags, bool noTags)
{
    return (noTags ? QStringList{noTagsDisplayName()} + tags : tags).join(", ");
}

// A translucent selection colour: stays readable with any system theme.
QColor MainWindow::noTagsBackground() const
{
    QColor color = palette().color(QPalette::Highlight);
    color.setAlpha(70);
    return color;
}

// QLineEdit has no API for formatting a part of its text; an input method event without preedit
// text is the way to pass it text formats. They are given relative to the cursor and must be
// sent again after every text change.
void MainWindow::highlightNoTags(QLineEdit *edit)
{
    QTextCharFormat format;
    format.setFontWeight(QFont::Bold);
    format.setBackground(noTagsBackground());

    const QString shownNoTags = Db::normalizeTag(noTagsDisplayName());
    const QString text = edit->text();
    QList<QInputMethodEvent::Attribute> attributes;
    for (qsizetype start = 0; start < text.size();) {
        qsizetype end = text.indexOf(u',', start);
        if (end < 0)
            end = text.size();
        if (Db::normalizeTag(text.mid(start, end - start)) == shownNoTags) { // the same test as parseTags
            qsizetype first = start, last = end;
            while (text[first].isSpace())
                ++first;
            while (text[last - 1].isSpace())
                --last;
            attributes << QInputMethodEvent::Attribute(QInputMethodEvent::TextFormat,
                                                       int(first) - edit->cursorPosition(), int(last - first), format);
        }
        start = end + 1;
    }
    QInputMethodEvent event(QString(), attributes);
    QCoreApplication::sendEvent(edit, &event);
}

QStringList MainWindow::parseTags(const QString &text, bool *noTags)
{
    const QString shownNoTags = Db::normalizeTag(noTagsDisplayName());
    if (noTags)
        *noTags = false;
    QStringList tags;
    for (const QString &part : text.split(',', Qt::SkipEmptyParts)) {
        const QString tag = Db::normalizeTag(part);
        if (tag == shownNoTags) {
            if (noTags)
                *noTags = true;
        } else if (!tag.isEmpty() && !tags.contains(tag)) {
            tags << tag;
        }
    }
    return tags;
}

void MainWindow::applySearch()
{
    m_query.include = parseTags(m_includeEdit->text(), &m_query.onlyUntagged);
    m_query.exclude = parseTags(m_excludeEdit->text(), &m_query.skipUntagged);
    m_query.exact = m_exactCheck->isChecked();
    // Show what is actually searched for (normalized spelling).
    m_includeEdit->setText(tagsText(m_query.include, m_query.onlyUntagged));
    m_excludeEdit->setText(tagsText(m_query.exclude, m_query.skipUntagged));
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
    bool noTags = false, addNoTags = false;
    QStringList tags = parseTags(edit->text(), &noTags);
    const QString internal = parseTags(tag, &addNoTags).value(0);
    if (!internal.isEmpty() && !tags.contains(internal))
        tags << internal;
    edit->setText(tagsText(tags, noTags || addNoTags));
}

void MainWindow::showTagMenu(const QPoint &pos)
{
    QListWidgetItem *item = m_tagList->itemAt(pos);
    const Gallery *gallery = currentGallery();
    if (!item || !gallery || item->data(GroupHeaderRole).toBool())
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
    if (!item->data(Qt::UserRole).toBool()) { // the service entry has a fixed name
        menu.addSeparator();
        menu.addAction(tr("Перейменувати…"), this, [this, tag] { renameTag(tag); });

        // One group per tag: checkable items of one exclusive group, the current one ticked.
        QMenu *groupMenu = menu.addMenu(tr("Група"));
        auto *choices = new QActionGroup(groupMenu);
        const qint64 current = item->data(TagGroupRole).toLongLong();
        const auto addChoice = [&](const QString &text, qint64 groupId) {
            QAction *action = groupMenu->addAction(text);
            action->setCheckable(true);
            action->setChecked(groupId == current);
            choices->addAction(action);
            connect(action, &QAction::triggered, this, [this, tag, groupId] { setTagGroup(tag, groupId); });
        };
        addChoice(tr("Без групи"), 0);
        const QList<TagGroup> groups = Db::tagGroups(gallery->id);
        if (!groups.isEmpty())
            groupMenu->addSeparator();
        for (const TagGroup &group : groups)
            addChoice(QString(group.name).replace('&', "&&"), group.id); // '&' would be a mnemonic
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
        if (!Db::isValidTagName(newName)) {
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
            bool noTags = false;
            QStringList tags = parseTags(edit->text(), &noTags);
            const qsizetype i = tags.indexOf(oldName);
            if (i >= 0) {
                tags[i] = newName;
                edit->setText(tagsText(tags, noTags));
            }
        }
        for (QStringList *tags : {&m_query.include, &m_query.exclude}) {
            const qsizetype i = tags->indexOf(oldName);
            if (i >= 0)
                (*tags)[i] = newName;
        }

        statusBar()->showMessage(tr("Тег «%1» перейменовано на «%2»").arg(oldName, newName), 5000);
        reloadTags();
        loadPage(m_offset);
        return;
    }
}

void MainWindow::reloadTags()
{
    m_tagList->clear();
    const Gallery *gallery = currentGallery();
    if (!gallery)
        return;

    // Service tag: always on top, always present (also with "hide foreign tags" or the tag filter), not stored in the DB.
    auto *noTags = new QListWidgetItem(noTagsDisplayName(), m_tagList);
    QFont font = noTags->font();
    font.setBold(true);
    noTags->setFont(font);
    noTags->setBackground(noTagsBackground());
    noTags->setToolTip(tr("Зображення, у яких немає жодного тегу"));
    noTags->setData(Qt::UserRole, true); // marks the service entry

    const QStringList tags = m_hideForeignTagsCheck->isChecked() ? Db::tagsOfSearch(gallery->id, m_query)
                                                                 : Db::allTags(gallery->id);
    // Tags without a group first, then the groups in the order set in the gallery dialog,
    // each after a header that can't be selected and doesn't look like a tag.
    const QList<TagSection> sections = Db::tagSections(gallery->id, tags);
    for (const TagSection &section : sections) {
        if (section.groupId != 0) {
            auto *header = new QListWidgetItem(QStringLiteral("— %1 —").arg(section.groupName), m_tagList);
            header->setFlags(Qt::NoItemFlags); // grey, not selectable, no double click
            // Left, like the tags: centring uses the width of the widest entry, which may be wider
            // than the visible part of the list, and the name was cut off at its right edge.
            header->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
            QFont headerFont = header->font();
            headerFont.setBold(true);
            headerFont.setItalic(true);
            header->setFont(headerFont);
            header->setToolTip(tr("Група тегів «%1»").arg(section.groupName));
            header->setData(GroupHeaderRole, true);
        }
        for (const QString &tag : section.tags) {
            auto *item = new QListWidgetItem(tag, m_tagList);
            item->setData(TagGroupRole, section.groupId);
        }
    }
    applyTagFilter();
}

void MainWindow::applyTagFilter()
{
    const QString text = m_tagFilterEdit->text();
    QListWidgetItem *header = nullptr; // of the group being walked
    bool groupShown = false;
    for (int i = 0; i < m_tagList->count(); ++i) {
        QListWidgetItem *item = m_tagList->item(i);
        if (item->data(GroupHeaderRole).toBool()) {
            if (header)
                header->setHidden(!groupShown);
            header = item;
            groupShown = false;
            continue;
        }
        const bool isServiceEntry = item->data(Qt::UserRole).toBool(); // never filtered out
        const bool shown = isServiceEntry || item->text().contains(text, Qt::CaseInsensitive);
        item->setHidden(!shown);
        groupShown = groupShown || shown;
    }
    if (header)
        header->setHidden(!groupShown); // a header stays only while some tag of its group is shown
}

void MainWindow::setTagGroup(const QString &tag, qint64 groupId)
{
    const Gallery *gallery = currentGallery();
    if (isBusy() || !gallery)
        return;
    if (!Db::setTagGroup(gallery->id, tag, groupId)) {
        QMessageBox::warning(this, tr("Помилка БД"), tr("Не вдалося змінити групу тегу:\n%1").arg(Db::lastError()));
        return;
    }
    // The list keeps its scroll position: sorting many tags into groups one by one must not make
    // the user search for the place they were at. (No current item: that would scroll to it.)
    const int scroll = m_tagList->verticalScrollBar()->value();
    reloadTags();
    m_tagList->doItemsLayout(); // the layout is delayed otherwise, and the scroll range with it
    m_tagList->verticalScrollBar()->setValue(scroll);
}

QString MainWindow::absolutePath(const QString &relativePath) const
{
    const Gallery *gallery = currentGallery();
    return gallery ? QDir(gallery->rootFolder).filePath(relativePath) : QString();
}

void MainWindow::showPreviousPage()
{
    if (m_offset > 0)
        loadPage(qMax(0, m_offset - pageSize()));
}

void MainWindow::showNextPage()
{
    if (m_offset + pageSize() < m_totalCount)
        loadPage(m_offset + pageSize());
}

void MainWindow::addThumbButton()
{
    const int i = int(m_thumbButtons.size());
    auto *button = new QToolButton(m_gridHost);
    button->setIconSize(QSize(ThumbnailSize, ThumbnailSize));
    button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    button->setAutoRaise(true);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    button->setMinimumSize(ThumbnailSize + 16, ThumbnailSize + 32);
    button->setEnabled(false);
    connect(button, &QToolButton::clicked, this, [this, i] { openThumbnail(i); });
    button->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(button, &QWidget::customContextMenuRequested, this, [this, i] { editImageMeta(i); });
    m_thumbButtons << button;

    // Selection for bulk tag editing; it is kept by image id across pages and searches.
    auto *check = new QCheckBox(button);
    check->setStyleSheet("QCheckBox { background-color: rgba(255, 255, 255, 200); border-radius: 3px;"
                         " padding: 2px; }");
    check->move(4, 4);
    check->hide();
    connect(check, &QCheckBox::toggled, this, [this, i](bool on) {
        if (!inCollection() || i >= m_pageRecords.size())
            return;
        if (on)
            m_selectedIds.insert(m_pageRecords[i].id);
        else
            m_selectedIds.remove(m_pageRecords[i].id);
        updateSelectionUi();
    });
    m_thumbChecks << check;
}

// Puts the first pageSize() thumbnails into the grid; the rest (made for a bigger window) wait hidden.
void MainWindow::placeThumbButtons()
{
    for (QToolButton *button : std::as_const(m_thumbButtons))
        m_gridLayout->removeWidget(button);
    for (int i = 0; i < m_thumbButtons.size(); ++i) {
        QToolButton *button = m_thumbButtons[i];
        if (i < pageSize()) {
            m_gridLayout->addWidget(button, i / m_gridColumns, i % m_gridColumns);
            button->show();
        } else {
            // Empty, so that it shows nothing stale when the grid grows again before the reload.
            button->hide();
            button->setEnabled(false);
            button->setIcon(QIcon());
            button->setText(QString());
            button->setToolTip(QString());
            m_thumbChecks[i]->hide();
        }
    }
}

void MainWindow::fitGridToSize()
{
    const QMargins margins = m_gridLayout->contentsMargins();
    const QSize cell = m_thumbButtons.first()->minimumSize();
    const int width = m_gridHost->width() - margins.left() - margins.right();
    const int height = m_gridHost->height() - margins.top() - margins.bottom();
    const int columns = qMax(1, (width + GridSpacing) / (cell.width() + GridSpacing));
    const int rows = qMax(1, (height + GridSpacing) / (cell.height() + GridSpacing));
    if (columns == m_gridColumns && rows == m_gridRows)
        return;
    m_gridColumns = columns;
    m_gridRows = rows;
    while (m_thumbButtons.size() < pageSize())
        addThumbButton();
    placeThumbButtons();
    // The top-left item stays where it is: the page now starts at it, whatever its number.
    m_gridReloadTimer.start();
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_gridHost && event->type() == QEvent::Resize)
        fitGridToSize();
    return QMainWindow::eventFilter(watched, event);
}

Db::TagQuery MainWindow::collectionQuery() const
{
    Db::TagQuery query;
    query.collectionId = m_openCollectionId;
    return query;
}

QString MainWindow::collectionDisplayName(const CollectionRecord &collection)
{
    if (!collection.name.isEmpty())
        return collection.name;
    return collection.virtualPath.isEmpty() ? noTagsDisplayName() : collection.virtualPath;
}

void MainWindow::updateCollectionModeUi()
{
    const bool open = inCollection();
    for (QWidget *widget : std::initializer_list<QWidget *>{m_includeEdit, m_exactCheck, m_searchButton,
                                                            m_clearSearchButton, m_sortCombo, m_tagsBox})
        widget->setEnabled(!open);
    m_excludeEdit->setEnabled(!open && !m_exactCheck->isChecked());
    m_collectionBar->setVisible(open);
}

void MainWindow::openCollection(int index)
{
    if (isBusy() || index < 0 || index >= m_pageCollections.size())
        return;
    m_collectionsOffset = m_offset;
    m_openCollectionId = m_pageCollections[index].id;
    updateCollectionModeUi();
    loadPage(0);
}

void MainWindow::closeCollection()
{
    if (isBusy() || !inCollection())
        return;
    m_openCollectionId = 0;
    updateCollectionModeUi();
    loadPage(m_collectionsOffset);
}

void MainWindow::editCollection(int index)
{
    const Gallery *gallery = currentGallery();
    if (isBusy() || !gallery || index < 0 || index >= m_pageCollections.size())
        return;
    const qint64 galleryId = gallery->id;
    const CollectionRecord collection = Db::collection(galleryId, m_pageCollections[index].id);
    if (collection.id == 0) {
        loadPage(m_offset); // gone meanwhile
        return;
    }

    CollectionDialog dialog(collection, m_thumbButtons[index]->icon().pixmap(ThumbnailSize),
                            Db::defaultCollectionName(galleryId, collection.virtualPath), this);
    const int result = dialog.exec();
    if (result == CollectionDialog::DeleteRequested) {
        deleteCollectionFiles(galleryId, collection);
        return;
    }
    if (result != QDialog::Accepted)
        return;
    if (!Db::updateCollection(galleryId, collection.id, dialog.name(), dialog.nameManual(), dialog.rating())) {
        QMessageBox::warning(this, tr("Помилка БД"), tr("Не вдалося зберегти колекцію:\n%1").arg(Db::lastError()));
        return;
    }
    CollectionRecord saved = collection;
    saved.name = dialog.name();
    statusBar()->showMessage(tr("Збережено колекцію: %1").arg(collectionDisplayName(saved)), 5000);
    loadPage(m_offset);
}

void MainWindow::deleteCollectionFiles(qint64 galleryId, const CollectionRecord &collection)
{
    if (isBusy())
        return;
    Db::TagQuery query;
    query.collectionId = collection.id;
    const QList<ImageRecord> records = Db::searchImages(galleryId, query, 0, -1, nullptr, Db::SortOrder::Classic);
    const QString root = currentGallery() ? currentGallery()->rootFolder : QString();
    QStringList paths;
    QHash<QString, qint64> idOf;
    for (const ImageRecord &record : records) {
        const QString path = QDir(root).filePath(record.relativePath);
        paths << path;
        idOf.insert(path, record.id);
    }
    if (paths.isEmpty()) {
        loadPage(m_offset);
        return;
    }

    auto *watcher = new QFutureWatcher<Scanner::TrashResult>(this);
    runBusy(watcher, tr("Переміщую файли колекції в Кошик…"));
    auto stop = std::make_shared<std::atomic_bool>(false);
    m_busyDialog->setStopHandler([stop] { *stop = true; });
    const QString name = collectionDisplayName(collection);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, galleryId, idOf, name] {
        const Scanner::TrashResult result = watcher->future().resultCount() > 0 ? watcher->result()
                                                                                : Scanner::TrashResult{};
        QList<qint64> ids;
        for (const QString &path : result.trashed + result.deleted)
            ids << idOf.value(path);
        bool dbOk = true;
        if (!ids.isEmpty()) {
            dbOk = Db::deleteImages(galleryId, ids);
            for (qint64 id : std::as_const(ids))
                m_selectedIds.remove(id);
        }
        reloadTags();
        loadPage(m_offset);

        QString message;
        if (result.stopped)
            message = tr("Видалення перервано. У Кошику: %1 з %2 файлів, решта залишилась у колекції.")
                          .arg(result.trashed.size()).arg(idOf.size());
        if (!result.deleted.isEmpty())
            message += (message.isEmpty() ? QString() : "\n\n")
                       + tr("Видалено остаточно, без Кошика (з вашої згоди): %1.").arg(result.deleted.size());
        if (!result.failed.isEmpty()) {
            QStringList shown = result.failed.mid(0, 20);
            for (QString &path : shown)
                path = QDir::toNativeSeparators(path);
            if (result.failed.size() > shown.size())
                shown << tr("… і ще %1").arg(result.failed.size() - shown.size());
            message += (message.isEmpty() ? QString() : "\n\n")
                       + tr("Ці файли не вдалося перемістити в Кошик, вони залишились на місці й у базі:\n\n%1")
                             .arg(shown.join('\n'));
        }
        if (!dbOk)
            message += (message.isEmpty() ? QString() : "\n\n")
                       + tr("Файли в Кошику, але прибрати записи з бази не вдалося:\n%1\n\nЗроби рескан.")
                             .arg(Db::lastError());
        if (!message.isEmpty())
            QMessageBox::warning(this, tr("Видалити колекцію"), message);
        else
            statusBar()->showMessage(tr("Колекцію «%1» видалено, у Кошику файлів: %2").arg(name).arg(ids.size()), 8000);
    });
    // Asked from the worker; it waits for the answer. Nothing is deleted permanently without a "yes".
    const Scanner::TrashQuestion ask = [this](const QString &path, Scanner::TrashProblem problem,
                                              const QString &error) {
        Scanner::TrashChoice choice = Scanner::TrashChoice::Stop;
        QMetaObject::invokeMethod(this, [&] { choice = askAboutTrashProblem(path, problem, error); },
                                  Qt::BlockingQueuedConnection);
        return choice;
    };
    watcher->setFuture(QtConcurrent::run([paths, stop, ask](QPromise<Scanner::TrashResult> &promise) {
        Scanner::moveFilesToTrash(promise, paths, stop.get(), ask);
    }));
}

Scanner::TrashChoice MainWindow::askAboutTrashProblem(const QString &path, Scanner::TrashProblem problem,
                                                      const QString &error)
{
    // Over the progress window when it is shown (it is modal).
    QWidget *parent = m_busyDialog->isVisible() ? static_cast<QWidget *>(m_busyDialog) : this;
    return TrashPrompt::ask(parent, tr("Видалити колекцію"), path, problem, error, true);
}
void MainWindow::loadPage(int offset)
{
    if (isBusy()) {
        // E.g. a viewer saved a rotation while the metadata dialog was closing, and the thumbnails
        // it asked for are still loading: the page is loaded again once they are there.
        m_reloadPending = true;
        return;
    }
    m_reloadPending = false;

    const Gallery *gallery = currentGallery();
    const qint64 galleryId = gallery ? gallery->id : 0;
    if (inCollection()) {
        // Tag edits may have moved the collection to other tags (it follows them) or emptied it.
        m_openCollection = Db::collection(galleryId, m_openCollectionId);
        if (m_openCollection.id == 0) {
            statusBar()->showMessage(tr("Колекції більше немає: її файли перейшли в інші колекції або видалені."),
                                     8000);
            m_openCollectionId = 0;
            updateCollectionModeUi();
            offset = m_collectionsOffset;
        }
    }

    m_offset = qMax(0, offset);
    QList<QPair<QString, QString>> paths; // what the thumbnails show: absolute path, sha256
    if (inCollection()) {
        m_pageCollections.clear();
        // Inside a collection files always go by name: Classic, whose collection keys are equal here.
        m_pageRecords = Db::searchImages(galleryId, collectionQuery(), m_offset, pageSize(), &m_totalCount,
                                         Db::SortOrder::Classic);
        m_totalFiles = m_totalCount;
        for (const ImageRecord &rec : std::as_const(m_pageRecords))
            paths.append({absolutePath(rec.relativePath), rec.sha256});
        // For "select all": the files of every page that get a checkbox.
        m_selectableIds.clear();
        const QList<ImageRecord> all = Db::searchImages(galleryId, collectionQuery(), 0, -1, nullptr,
                                                        Db::SortOrder::Classic);
        for (const ImageRecord &rec : all)
            if (MediaFiles::kindOf(rec.relativePath) != MediaFiles::Kind::Other)
                m_selectableIds << rec.id;
    } else {
        m_pageRecords.clear();
        m_selectableIds.clear();
        m_pageCollections = Db::searchCollections(galleryId, m_query, m_offset, pageSize(), &m_totalCount,
                                                  &m_totalFiles, sortOrder());
        for (const CollectionRecord &collection : std::as_const(m_pageCollections))
            paths.append({absolutePath(collection.coverPath), collection.coverSha256});
    }
    if (paths.isEmpty() && m_offset > 0 && m_totalCount > 0) {
        // The list got shorter (e.g. after editing tags): show its last page.
        loadPage((m_totalCount - 1) / pageSize() * pageSize());
        return;
    }

    m_prevButton->setEnabled(m_offset > 0);
    m_nextButton->setEnabled(m_offset + pageSize() < m_totalCount);

    for (int i = 0; i < m_thumbButtons.size(); ++i) {
        QToolButton *button = m_thumbButtons[i];
        button->setIcon(QIcon());
        button->setEnabled(i < paths.size());
        // Selection checkboxes: files only (not "other" files), not collections.
        QCheckBox *check = m_thumbChecks[i];
        const bool file = i < m_pageRecords.size();
        {
            const QSignalBlocker blocker(check); // just showing the stored selection
            check->setChecked(file && m_selectedIds.contains(m_pageRecords[i].id));
        }
        check->setVisible(file && MediaFiles::kindOf(m_pageRecords[i].relativePath) != MediaFiles::Kind::Other);
    }
    refreshPageTexts();
    updateSelectionUi();

    if (paths.isEmpty())
        return;

    auto *watcher = new QFutureWatcher<QImage>(this);
    runBusy(watcher, tr("Завантажую прев'юшки…"));
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher] {
        const QList<QImage> images = watcher->future().results();
        for (int i = 0; i < images.size() && i < m_thumbButtons.size(); ++i) {
            if (!images[i].isNull())
                m_thumbButtons[i]->setIcon(QIcon(QPixmap::fromImage(images[i])));
            else if (inCollection()) // a collection keeps its name under an empty cover
                m_thumbButtons[i]->setText(tr("(не читається)"));
        }
        if (m_reloadPending) // asked for while these were loading: the page may be out of date
            loadPage(m_offset);
    });
    watcher->setFuture(QtConcurrent::mapped(std::move(paths), [](const QPair<QString, QString> &file) {
        return ThumbCache::thumbnail(file.first, file.second, ThumbnailSize);
    }));
}

void MainWindow::updatePageLabel()
{
    const bool compact = width() < CompactWidth;
    m_pageLabel->setText(compact ? m_pageTextShort : m_pageTextFull);
    m_pageLabel->setToolTip(compact ? m_pageCounts : QString());
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    updatePageLabel();
}

// Page label and the name/tooltip of every thumbnail (no reloading of the pictures).
void MainWindow::refreshPageTexts()
{
    // Pages run both ways from the top-left item, so the first one may be shorter.
    const int size = pageSize();
    const int pagesBefore = (m_offset + size - 1) / size;
    const int pageCount = qMax(pagesBefore + 1, pagesBefore + (m_totalCount - m_offset + size - 1) / size);
    const int pageNumber = pagesBefore + 1;
    m_pageTextShort = tr("Сторінка %1 з %2").arg(pageNumber).arg(pageCount);
    if (inCollection()) {
        m_pageTextFull = tr("Сторінка %1 з %2 (%3 зобр.)").arg(pageNumber).arg(pageCount).arg(m_totalCount);
        m_pageCounts = tr("%1 зобр.").arg(m_totalCount);
        m_collectionLabel->setText(tr("Колекція «%1» · оцінка %2 · файлів: %3")
                                       .arg(collectionDisplayName(m_openCollection),
                                            QString::number(m_openCollection.rating),
                                            QString::number(m_openCollection.fileCount)));
        m_collectionLabel->setToolTip(m_openCollection.virtualPath);
    } else {
        m_pageTextFull = tr("Сторінка %1 з %2 (колекцій: %3, файлів: %4)")
                             .arg(pageNumber).arg(pageCount).arg(m_totalCount).arg(m_totalFiles);
        m_pageCounts = tr("колекцій: %1, файлів: %2").arg(m_totalCount).arg(m_totalFiles);
    }
    updatePageLabel();
    for (int i = 0; i < m_thumbButtons.size(); ++i) {
        QToolButton *button = m_thumbButtons[i];
        if (i < m_pageCollections.size()) {
            const CollectionRecord &collection = m_pageCollections[i];
            const QString name = collectionDisplayName(collection);
            button->setText(button->fontMetrics().elidedText(name, Qt::ElideMiddle, ThumbnailSize));
            button->setToolTip(tr("Колекція «%1»").arg(name) + "\n"
                               + tr("Теги: ") + collection.virtualPath + "\n"
                               + tr("Оцінка: ") + QString::number(collection.rating) + "\n"
                               + tr("Файлів: ") + QString::number(collection.fileCount) + "\n\n"
                               + tr("Клік — відкрити колекцію, правий клік — властивості колекції"));
        } else if (i < m_pageRecords.size()) {
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
    if (!inCollection()) {
        openCollection(index);
        return;
    }
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

    // The arrows in the viewer walk through the images of the whole collection (not just this
    // page), by file name like the grid; videos and other files are skipped.
    const QList<ImageRecord> all = Db::searchImages(gallery->id, collectionQuery(), 0, -1, nullptr,
                                                    Db::SortOrder::Classic);
    QStringList paths;
    QHash<QString, qint64> idOf; // path -> record, for deleting from the viewer
    paths.reserve(all.size());
    int current = 0;
    for (const ImageRecord &rec : all) {
        if (MediaFiles::kindOf(rec.relativePath) != MediaFiles::Kind::Image)
            continue;
        if (rec.id == m_pageRecords[index].id)
            current = int(paths.size());
        paths << absolutePath(rec.relativePath);
        idOf.insert(paths.last(), rec.id);
    }
    if (paths.isEmpty()) {
        paths << path;
        idOf.insert(path, m_pageRecords[index].id);
    }

    auto *viewer = new ImageViewer(paths, current, gallery->allowUpscale, this);
    // A rotated file was saved: refresh its record (size/hash) and the thumbnail.
    const qint64 galleryId = gallery->id;
    const QString root = gallery->rootFolder;
    connect(viewer, &ImageViewer::fileChanged, this, [this, galleryId, root](const QString &file) {
        fileRewritten(galleryId, root, file);
    });
    viewer->setDeleteHandler([this, viewer, galleryId, idOf](const QString &file) {
        return trashImageFile(viewer, galleryId, idOf.value(file), file);
    });
    viewer->show();
}

bool MainWindow::trashImageFile(QWidget *messageParent, qint64 galleryId, qint64 imageId, const QString &path)
{
    if (isBusy()) {
        // A background job (rescan, import…) works with the same records right now.
        QMessageBox::warning(messageParent, tr("Видалити файл"),
                             tr("Зачекайте, доки завершиться поточна операція, і спробуйте ще раз."));
        return false;
    }
    // A file already gone from disk only needs to leave the DB. Never deleted permanently without a "yes".
    bool permanently = false;
    if (QFileInfo::exists(path)) {
        QString error;
        const Scanner::RecycleOutcome outcome = Scanner::recycleFile(path, &error);
        if (outcome == Scanner::RecycleOutcome::NotRecyclable) {
            if (TrashPrompt::ask(messageParent, tr("Видалити файл"), path, Scanner::TrashProblem::NotRecyclable,
                                 QString(), false)
                != Scanner::TrashChoice::DeletePermanently)
                return false;
            QFile file(path);
            if (!file.remove()) {
                QMessageBox::warning(messageParent, tr("Видалити файл"),
                                     tr("Не вдалося видалити файл:\n%1\n\n%2")
                                         .arg(QDir::toNativeSeparators(path), file.errorString()));
                return false;
            }
            permanently = true;
        } else if (outcome == Scanner::RecycleOutcome::Failed) {
            TrashPrompt::ask(messageParent, tr("Видалити файл"), path, Scanner::TrashProblem::Failed, error, false);
            return false;
        }
    }
    if (imageId != 0) {
        if (!Db::deleteImages(galleryId, {imageId}))
            QMessageBox::warning(messageParent, tr("Помилка БД"),
                                 tr("Файл у Кошику, але прибрати запис з бази не вдалося:\n%1\n\nЗроби рескан.")
                                     .arg(Db::lastError()));
        m_selectedIds.remove(imageId);
    }
    statusBar()->showMessage((permanently ? tr("Видалено остаточно: %1") : tr("У Кошику: %1"))
                                 .arg(QFileInfo(path).fileName()),
                             5000);
    // A viewer may outlive a gallery switch: the window shows another gallery then, nothing to refresh.
    const Gallery *current = currentGallery();
    if (current && current->id == galleryId) {
        reloadTags();
        loadPage(m_offset);
    }
    return true;
}

void MainWindow::fileRewritten(qint64 galleryId, const QString &root, const QString &file)
{
    const QFileInfo fi(file);
    // Can't be read right now: the record stays as it is (an empty hash would stick, since rescan
    // trusts size + date, and would pair the file with others as "duplicates"). Rescan sees the new
    // size/date and hashes the file then.
    const QString sha256 = Scanner::fileSha256(file);
    if (!sha256.isEmpty())
        Db::updateImageFile(galleryId, QDir(root).relativeFilePath(file), fi.size(),
                            fi.lastModified(QTimeZone::UTC).toMSecsSinceEpoch(), sha256);
    const Gallery *current = currentGallery();
    if (current && current->id == galleryId && !isBusy())
        loadPage(m_offset);
}

// Connects what a metadata dialog needs from the main window.
void MainWindow::wireMetaDialog(ImageMetaDialog &dialog)
{
    const Gallery *gallery = currentGallery();
    dialog.setAllowUpscale(gallery->allowUpscale);
    dialog.setKeepExtension(m_keepExtensionAction->isChecked());
    dialog.setTagCollectionCounts(Db::tagCollectionCounts(gallery->id));
    dialog.setTagGroups(Db::tagGroups(gallery->id));
    connect(&dialog, &ImageMetaDialog::galleryTagsEdited, this,
            [this, &dialog, galleryId = gallery->id](const QHash<QString, qint64> &groupChanges,
                                                     const QStringList &deleted) {
                applyGalleryTagEdits(galleryId, groupChanges, deleted);
                // The next tag picker of this dialog shows the gallery as it is now.
                dialog.setGalleryTags(Db::tagSections(galleryId, Db::allTags(galleryId)));
                dialog.setTagCollectionCounts(Db::tagCollectionCounts(galleryId));
            });
    connect(&dialog, &ImageMetaDialog::tagActivated, this, [this](const QString &tag) {
        addTagToEdit(m_includeEdit, tag); // the search itself waits for "Шукати"
    });
    const qint64 galleryId = gallery->id;
    const QString root = gallery->rootFolder;
    connect(&dialog, &ImageMetaDialog::fileChanged, this, [this, galleryId, root](const QString &file) {
        fileRewritten(galleryId, root, file);
    });
    connect(&dialog, &ImageMetaDialog::exportRequested, this,
            [this, &dialog, galleryId, root](const QString &destination) {
                exportFile(dialog, galleryId, root, destination);
            });
}

// Copies the dialog's file to where the user chose. Inside the gallery, the copy is a new
// gallery file (after a question); a place the gallery ignores is just a place on disk.
void MainWindow::exportFile(ImageMetaDialog &dialog, qint64 galleryId, const QString &root,
                            const QString &destination)
{
    const QString title = tr("Експорт файлу");
    const QString source = dialog.filePath();
    const QFileInfo target(destination);
    const std::optional<QString> galleryFolder =
        Scanner::isIgnoredFile(destination) ? std::nullopt : Scanner::folderInGallery(root, target.absolutePath());

    if (!galleryFolder) {
        dialog.setExporting(true);
        auto *watcher = new QFutureWatcher<bool>(this);
        runBusy(watcher, tr("Експортую файл…"));
        connect(watcher, &QFutureWatcherBase::finished, &dialog, [this, watcher, &dialog, title, destination] {
            dialog.setExporting(false);
            const QString shown = QDir::toNativeSeparators(destination);
            if (watcher->result())
                QMessageBox::information(&dialog, title, tr("Файл збережено:\n%1").arg(shown));
            else
                QMessageBox::warning(&dialog, title, tr("Не вдалося скопіювати файл:\n%1").arg(shown));
        });
        watcher->setFuture(QtConcurrent::run([source, destination] {
            return Scanner::exportCopy(source, destination);
        }));
        return;
    }

    QString question = tr("Це місце всередині поточної галереї. Справді створити дублікат файлу?\n\n%1")
                           .arg(QDir::toNativeSeparators(destination));
    if (target.exists())
        question += "\n\n" + tr("Файл із такою назвою вже є. Його не буде замінено: копія отримає номер у назві.");
    QMessageBox box(QMessageBox::Question, title, question, QMessageBox::NoButton, &dialog);
    QPushButton *yes = box.addButton(tr("Створити дублікат"), QMessageBox::AcceptRole);
    QPushButton *no = box.addButton(tr("Скасувати"), QMessageBox::RejectRole);
    box.setDefaultButton(no);
    box.exec();
    if (box.clickedButton() != yes)
        return;

    const Scanner::ImportItem item{source, *galleryFolder, target.fileName(), true};
    dialog.setExporting(true);
    auto *watcher = new QFutureWatcher<Scanner::ImportStats>(this);
    runBusy(watcher, tr("Експортую файл…"));
    connect(watcher, &QFutureWatcherBase::finished, &dialog, [this, watcher, &dialog, title] {
        dialog.setExporting(false);
        const Scanner::ImportStats stats = watcher->result();
        reloadTags();
        if (!isBusy())
            loadPage(m_offset);
        if (stats.copied > 0 && stats.error.isEmpty())
            QMessageBox::information(&dialog, title, tr("Дублікат створено й додано в галерею."));
        else
            QMessageBox::warning(&dialog, title, stats.error.isEmpty()
                                                     ? tr("Не вдалося скопіювати файл:\n%1").arg(stats.failed.join('\n'))
                                                     : stats.error);
    });
    watcher->setFuture(QtConcurrent::run([galleryId, root, item](QPromise<Scanner::ImportStats> &promise) {
        Scanner::importFiles(promise, galleryId, root, {item});
    }));
}

// Renames the file on disk and in its record. On any failure the old name stays and the user is told why.
bool MainWindow::renameImageFile(qint64 galleryId, const ImageRecord &record, const QString &newName)
{
    const QString path = absolutePath(record.relativePath);
    const QFileInfo current(path);
    const QString newPath = current.dir().filePath(newName);

    QString problem;
    QFile file(path);
    if (!Scanner::isValidFileName(newName)) {
        problem = tr("Назва порожня або недопустима. Не можна використовувати символи \\ / : * ? \" < > |, "
                     "назви пристроїв Windows (CON, NUL тощо), а також крапку чи пробіл у кінці.");
    } else if (Scanner::isIgnoredFile(newPath)) {
        problem = tr("Файли з такою назвою галерея не зберігає (.db, .ini та службові файли).");
    } else if (QFileInfo::exists(newPath) && newName.compare(current.fileName(), Qt::CaseInsensitive) != 0) {
        problem = tr("Файл із такою назвою вже існує."); // a change of letter case only is fine
    } else if (!file.rename(newPath)) {
        problem = file.errorString();
    } else {
        const qsizetype slash = record.relativePath.lastIndexOf('/');
        if (Db::renameImage(galleryId, record.id, record.relativePath.left(slash + 1) + newName, newName))
            return true;
        problem = Db::lastError();
        QFile::rename(newPath, path); // the record can't follow, so the file goes back
    }
    QMessageBox::warning(this, tr("Перейменувати файл"),
                         tr("Не вдалося перейменувати файл, лишилася стара назва:\n%1\n\n%2")
                             .arg(current.fileName(), problem));
    return false;
}

void MainWindow::editImageMeta(int index)
{
    if (!inCollection()) {
        editCollection(index);
        return;
    }
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
    ImageMetaDialog dialog({path}, Db::tagsForImage(record.id),
                           Db::tagSections(galleryId, Db::allTags(galleryId)), this);
    wireMetaDialog(dialog);
    dialog.setIsCover(Db::isCollectionCover(galleryId, record.id));
    const int result = dialog.exec();

    if (result == ImageMetaDialog::DeleteRequested) {
        trashImageFile(this, galleryId, record.id, path);
        return;
    }
    if (result != QDialog::Accepted)
        return;

    prepareTagGroups(galleryId, dialog.groupChanges());
    if (!Db::saveImageMeta(galleryId, record.id, dialog.tags())) {
        QMessageBox::warning(this, tr("Помилка БД"), tr("Не вдалося зберегти зміни:\n%1").arg(Db::lastError()));
        return;
    }
    // After the tags: the file is the cover of the collection it is in now.
    if (dialog.coverRequested() && !Db::setCollectionCover(galleryId, record.id))
        QMessageBox::warning(this, tr("Помилка БД"), tr("Не вдалося встановити обкладинку:\n%1").arg(Db::lastError()));
    QString fileName = QFileInfo(record.relativePath).fileName();
    // The raw text first: an existing name with a leading space must count as unchanged.
    if (dialog.fileName() != fileName) {
        const QString newName = dialog.fileName().trimmed();
        if (newName != fileName && renameImageFile(galleryId, record, newName))
            fileName = newName;
    }
    statusBar()->showMessage(tr("Збережено: %1").arg(fileName), 5000);
    reloadTags();
    loadPage(m_offset); // new tags may have moved the file to another collection
}

void MainWindow::applyGalleryTagEdits(qint64 galleryId, const QHash<QString, qint64> &groupChanges,
                                      const QStringList &deleted)
{
    setTagGroups(galleryId, groupChanges);
    deleteTagsFromGallery(galleryId, deleted);
    reloadTags();
    loadPage(m_offset); // a deleted tag may have moved files to other collections
}

void MainWindow::prepareTagGroups(qint64 galleryId, const QHash<QString, qint64> &groups)
{
    // Not fatal: the tags are still saved, only their collection is named without the group.
    if (!Db::prepareTagGroups(galleryId, groups))
        QMessageBox::warning(this, tr("Помилка БД"), tr("Не вдалося змінити групу тегу:\n%1").arg(Db::lastError()));
}

void MainWindow::setTagGroups(qint64 galleryId, const QHash<QString, qint64> &changes)
{
    // After the tags were saved: a new tag exists only now (one no file kept is simply not found).
    for (auto it = changes.cbegin(); it != changes.cend(); ++it) {
        if (!Db::setTagGroup(galleryId, it.key(), it.value())) {
            QMessageBox::warning(this, tr("Помилка БД"), tr("Не вдалося змінити групу тегу:\n%1").arg(Db::lastError()));
            return;
        }
    }
}

void MainWindow::deleteTagsFromGallery(qint64 galleryId, const QStringList &tags)
{
    if (tags.isEmpty())
        return;
    if (!Db::deleteTags(galleryId, tags)) {
        QMessageBox::warning(this, tr("Помилка БД"), tr("Не вдалося видалити теги:\n%1").arg(Db::lastError()));
        return;
    }

    // The search must not keep asking for tags that are gone.
    for (QLineEdit *edit : {m_includeEdit, m_excludeEdit}) {
        bool noTags = false;
        QStringList current = parseTags(edit->text(), &noTags);
        const qsizetype before = current.size();
        for (const QString &tag : tags)
            current.removeAll(tag);
        if (current.size() != before)
            edit->setText(tagsText(current, noTags));
    }
    for (QStringList *query : {&m_query.include, &m_query.exclude})
        for (const QString &tag : tags)
            query->removeAll(tag);
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

void MainWindow::selectWholeCollection()
{
    for (qint64 id : std::as_const(m_selectableIds))
        m_selectedIds.insert(id);
    for (int i = 0; i < m_thumbChecks.size() && i < m_pageRecords.size(); ++i) {
        QCheckBox *box = m_thumbChecks[i];
        const QSignalBlocker blocker(box); // the ids are in the set already
        box->setChecked(!box->isHidden() && m_selectedIds.contains(m_pageRecords[i].id));
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
    m_selectPageButton->setEnabled(anyOnPage && !allOnPage);
    m_selectAllButton->setEnabled(std::any_of(m_selectableIds.cbegin(), m_selectableIds.cend(),
                                              [this](qint64 id) { return !m_selectedIds.contains(id); }));
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
        loadPage(m_offset);
        return;
    }

    QStringList paths;
    QList<qint64> ids;
    QStringList common;
    QStringList any; // tags of at least one file
    bool first = true;
    for (const ImageRecord &rec : records) {
        paths << absolutePath(rec.relativePath);
        ids << rec.id;
        const QStringList tags = Db::tagsForImage(rec.id);
        for (const QString &tag : tags)
            if (!any.contains(tag))
                any << tag;
        if (first) {
            common = tags;
            first = false;
        } else {
            common.erase(std::remove_if(common.begin(), common.end(),
                                        [&tags](const QString &t) { return !tags.contains(t); }),
                         common.end());
        }
    }

    ImageMetaDialog dialog(paths, common, Db::tagSections(galleryId, Db::allTags(galleryId)), this);
    QStringList partial; // only some of the files have them: the tag picker shows them locked
    for (const QString &tag : std::as_const(any))
        if (!common.contains(tag))
            partial << tag;
    dialog.setPartialTags(partial);
    wireMetaDialog(dialog);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const QStringList added = dialog.addedTags();
    const QStringList removed = dialog.removedTags();
    if (added.isEmpty() && removed.isEmpty())
        return;

    prepareTagGroups(galleryId, dialog.groupChanges());
    if (!Db::applyTagChanges(galleryId, ids, added, removed)) {
        QMessageBox::warning(this, tr("Помилка БД"), tr("Не вдалося зберегти зміни:\n%1").arg(Db::lastError()));
        return;
    }
    statusBar()->showMessage(tr("Теги оновлено у файлів: %1").arg(ids.size()), 5000);
    reloadTags();
    loadPage(m_offset);
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
        loadPage(m_offset);
    }
}

void MainWindow::renameAllCollections()
{
    const Gallery *gallery = currentGallery();
    if (isBusy())
        return;
    const QString title = tr("Перейменувати всі колекції");
    if (!gallery) {
        QMessageBox::information(this, title, tr("Спершу створи галерею."));
        return;
    }

    QMessageBox box(QMessageBox::Question, title,
                    tr("Дати колекціям галереї «%1» імена за замовчуванням (тег з останньої групи та найменше число)?\n\n"
                       "Імена, задані вручну, можна зберегти або теж замінити. Повернути старі імена не вийде.")
                        .arg(gallery->name),
                    QMessageBox::NoButton, this);
    QPushButton *automatic = box.addButton(tr("Перейменувати лише автоматичні імена"), QMessageBox::AcceptRole);
    QPushButton *all = box.addButton(tr("Перейменувати загалом всі колекції"), QMessageBox::DestructiveRole);
    QPushButton *cancel = box.addButton(tr("Скасувати"), QMessageBox::RejectRole);
    box.setDefaultButton(cancel);
    box.exec();
    if (box.clickedButton() != automatic && box.clickedButton() != all)
        return;

    const int renamed = Db::renameCollections(gallery->id, box.clickedButton() == automatic);
    if (renamed < 0) {
        QMessageBox::warning(this, tr("Помилка БД"), tr("Не вдалося перейменувати колекції:\n%1").arg(Db::lastError()));
        return;
    }
    statusBar()->showMessage(tr("Перейменовано колекцій: %1").arg(renamed), 5000);
    loadPage(m_offset); // the names sort the collections
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

        auto *removal = new QFutureWatcher<Scanner::JunkRemoval>(this);
        runBusy(removal, tr("Видаляю сміття…"));
        connect(removal, &QFutureWatcherBase::finished, this, [this, removal] {
            const Scanner::JunkRemoval done = removal->result();
            statusBar()->showMessage(tr("Видалено файлів: %1, папок: %2").arg(done.removedFiles).arg(done.removedFolders),
                                     10000);
            if (!done.failedFolders.isEmpty())
                QMessageBox::warning(this, tr("Сміття"),
                                     tr("Ці папки видалити не вдалося (можливо, зайняті або немає прав):\n\n%1")
                                         .arg(done.failedFolders.join('\n')));
        });
        removal->setFuture(QtConcurrent::run([junk] { return Scanner::removeJunk(junk); }));
    });
    watcher->setFuture(QtConcurrent::run([root] { return Scanner::findJunk(root); }));
}

void MainWindow::setFollowFolderLinks(bool follow)
{
    QMessageBox box(QMessageBox::Warning, tr("Заходити в junction-папки"),
                    follow ? tr("Увімкнути перехід у junction-папки?") : tr("Вимкнути перехід у junction-папки?"),
                    QMessageBox::NoButton, this);
    box.setInformativeText(
        follow ? tr("Рескан і «Додати папку» заходитимуть також у junction-папки й символьні посилання на папки.\n\n"
                    "• Файли, на які вони ведуть, можуть лежати поза кореневою папкою галереї: на іншому диску, "
                    "у мережі, в іншій галереї. Після наступного рескану вони з'являться в галереї.\n"
                    "• Усе, що програма робить із такими файлами (поворот, перейменування, видалення в Кошик), "
                    "змінює оригінали, а не посилання.\n"
                    "• Кожна папка обходиться лише раз, тож посилання, що веде вгору, не зациклить програму.\n"
                    "• «Видалити сміття» в такі папки не заходить ніколи.")
               : tr("Рескан і «Додати папку» більше не заходитимуть у junction-папки й символьні посилання на папки.\n\n"
                    "• Наступний рескан прибере з бази всі файли, знайдені через такі папки, разом з їхніми тегами. "
                    "Колекції, що лишаться без файлів, зникнуть разом із назвами, рейтингами й обкладинками.\n"
                    "• Самі файли на диску не зміняться.\n"
                    "• Якщо потім знову увімкнути, ці файли повернуться як нові, без ручних тегів."));
    QPushButton *yes = box.addButton(follow ? tr("Увімкнути") : tr("Вимкнути"), QMessageBox::AcceptRole);
    QPushButton *no = box.addButton(tr("Скасувати"), QMessageBox::RejectRole);
    box.setDefaultButton(no);
    box.exec();
    if (box.clickedButton() != yes) {
        m_followLinksAction->setChecked(!follow); // setChecked doesn't emit triggered
        return;
    }
    Db::setSetting(FollowFolderLinksKey, follow ? "1" : "0");
    statusBar()->showMessage(tr("Зміниться під час наступного рескану."), 5000);
}

QString MainWindow::chooseRootTag(const QString &title, const QString &suggestion)
{
    const Gallery *gallery = currentGallery();
    if (!gallery)
        return {};
    // Real folders only, whatever the setting: a link leads outside the gallery, and the import
    // refuses a target folder outside it.
    QStringList folders = Scanner::realSubfolders(gallery->rootFolder);
    int current = 0;
    if (!suggestion.isEmpty()) {
        current = int(folders.indexOf(suggestion));
        if (current < 0) {
            folders.prepend(suggestion);
            current = 0;
        }
    }
    // The folder chosen last time for this gallery wins, while it still exists (the suggestion stays in the list).
    const QString remembered = Db::gallerySetting(gallery->id, ImportRootFolderKey);
    if (!remembered.isEmpty()) {
        for (int i = 0; i < folders.size(); ++i) {
            if (folders[i].compare(remembered, Qt::CaseInsensitive) == 0
                && QFileInfo(QDir(gallery->rootFolder).filePath(folders[i])).isDir()) {
                current = i;
                break;
            }
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
        if (!Scanner::isValidFolderName(name)) {
            QMessageBox::warning(this, title,
                                 tr("Недопустима назва папки. Не можна використовувати символи \\ / : * ? \" < > |"));
            continue;
        }
        Db::setGallerySetting(gallery->id, ImportRootFolderKey, name);
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
        if (!Scanner::isIgnoredFile(sourceDir.filePath(name))) {
            hasOwnFiles = true;
            break;
        }

    // Every file under `from`, going to `tag` + its subfolder path below `from`.
    // Folder links as in rescan: only with the setting, never in circles.
    const bool followLinks = m_followLinksAction->isChecked();
    auto collectInto = [followLinks](const QString &from, const QString &tag, QList<Scanner::ImportItem> &items) {
        const QDir base(from);
        Scanner::forEachFile(from, followLinks, [&](const QString &file) {
            if (!Scanner::isIgnoredFile(file)) {
                const QString sub = base.relativeFilePath(QFileInfo(file).absolutePath());
                items.append({file, QDir::cleanPath(tag + '/' + sub)});
            }
            return true;
        });
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
    QStringList subfolders;
    const QFileInfoList entries = sourceDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden);
    for (const QFileInfo &entry : entries)
        if (followLinks || Scanner::isRealFolder(entry))
            subfolders << entry.fileName();
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

void MainWindow::downloadFromInternet()
{
    const Gallery *gallery = currentGallery();
    if (isBusy())
        return;
    if (!gallery) {
        QMessageBox::information(this, tr("Завантажити з інтернету"), tr("Спершу створи галерею."));
        return;
    }
    DownloadDialog dialog(*gallery, this);
    connect(&dialog, &DownloadDialog::filesAdded, this, [this] {
        reloadTags();
        loadPage(0);
    });
    const qint64 galleryId = gallery->id;
    connect(&dialog, &DownloadDialog::galleryTagsEdited, this,
            [this, galleryId](const QHash<QString, qint64> &groupChanges, const QStringList &deleted) {
                applyGalleryTagEdits(galleryId, groupChanges, deleted);
            });
    dialog.exec();
}

namespace {

// The folder with links resolved, '/'-separated.
QString resolvedFolder(const QString &folder)
{
    const QFileInfo info(folder);
    const QString canonical = info.canonicalFilePath();
    return QDir::cleanPath(canonical.isEmpty() ? info.absoluteFilePath() : canonical);
}

// The same folder, or one inside it (case ignored, as on Windows).
bool isSameOrInside(const QString &folder, const QString &parent)
{
    const QString prefix = parent.endsWith('/') ? parent : parent + '/';
    return folder.compare(parent, Qt::CaseInsensitive) == 0 || folder.startsWith(prefix, Qt::CaseInsensitive);
}

// One comparison category for the details text: native separators, at most a thousand paths.
QString pathList(const QString &title, const QStringList &paths)
{
    if (paths.isEmpty())
        return {};
    constexpr int Shown = 1000;
    QStringList lines;
    for (const QString &path : paths.mid(0, Shown))
        lines << "  " + QDir::toNativeSeparators(path);
    if (paths.size() > Shown)
        lines << "  " + QCoreApplication::translate("MainWindow", "… і ще %1").arg(paths.size() - Shown);
    return title + '\n' + lines.join('\n') + "\n\n";
}

} // namespace

void MainWindow::syncWithFolder()
{
    const Gallery *gallery = currentGallery();
    if (isBusy())
        return;
    const QString title = tr("Синхронізація з папкою");
    if (!gallery) {
        QMessageBox::information(this, title, tr("Спершу створи галерею."));
        return;
    }
    if (!QFileInfo(gallery->rootFolder).isDir()) {
        QMessageBox::warning(this, title,
                             tr("Папка галереї недоступна:\n%1").arg(QDir::toNativeSeparators(gallery->rootFolder)));
        return;
    }
    const QString chosen = QFileDialog::getExistingDirectory(this, tr("Обери папку для синхронізації з галереєю"));
    if (chosen.isEmpty())
        return;
    const QString folder = resolvedFolder(chosen);
    const QString root = resolvedFolder(gallery->rootFolder);
    if (isSameOrInside(folder, root) || isSameOrInside(root, folder)) {
        QMessageBox::warning(this, title,
                             tr("Ця папка не може бути другою галереєю: вона є папкою галереї, лежить усередині неї "
                                "або містить її.\n\nГалерея: %1\nПапка: %2")
                                 .arg(QDir::toNativeSeparators(root), QDir::toNativeSeparators(folder)));
        return;
    }

    QMessageBox modeBox(QMessageBox::Question, title, tr("Як порівнювати папку з галереєю?"), QMessageBox::NoButton,
                        this);
    modeBox.setInformativeText(
        tr("Швидко: файл із тим самим шляхом відносно папки, тим самим ім'ям і розміром вважається тим самим, "
           "його вміст не читається.\n\n"
           "Надійно: однаковими вважаються лише файли з однаковою sha256, тож файли папки читаються повністю. "
           "Для великої мережевої папки це довго.\n\n"
           "В обох режимах файл папки читається, лише якщо в галереї є файл такого самого розміру: інший розмір — "
           "точно інший вміст. Вміст файлів галереї береться з бази, тож спершу буде рескан галереї. "
           "Час зміни файлів не порівнюється: на іншому комп'ютері годинник може йти інакше."));
    QPushButton *quick = modeBox.addButton(tr("Швидко"), QMessageBox::AcceptRole);
    QPushButton *reliable = modeBox.addButton(tr("Надійно"), QMessageBox::AcceptRole);
    QPushButton *cancel = modeBox.addButton(QMessageBox::Cancel);
    modeBox.setDefaultButton(quick);
    modeBox.setEscapeButton(cancel);
    modeBox.exec();
    if (modeBox.clickedButton() != quick && modeBox.clickedButton() != reliable)
        return;
    const FolderSync::Mode mode = modeBox.clickedButton() == quick ? FolderSync::Mode::Quick
                                                                   : FolderSync::Mode::Reliable;

    const Gallery target = *gallery;
    runRescan(target, [this, target, folder, mode, title](const ScanStats &stats) {
        if (!stats.error.isEmpty()) {
            QMessageBox::warning(this, title, tr("Рескан не вдався:\n%1").arg(stats.error));
            return;
        }
        compareWithFolder(target, folder, mode);
    });
}

void MainWindow::offerSyncStop(const std::shared_ptr<FolderSync::Control> &control, const QString &question)
{
    m_busyDialog->setSpinning(true);
    m_busyDialog->setStopHandler([control] { control->stop = true; });
    // The job waits while the user thinks: a click by mistake changes nothing.
    m_busyDialog->setStopConfirmation([this, control, question] {
        control->pause = true;
        const bool stop = QMessageBox::question(m_busyDialog, tr("Синхронізація з папкою"), question,
                                                QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
                          == QMessageBox::Yes;
        if (stop)
            control->stop = true;
        control->pause = false;
        return stop;
    });
}

void MainWindow::compareWithFolder(const Gallery &gallery, const QString &folder, FolderSync::Mode mode)
{
    auto control = std::make_shared<FolderSync::Control>();
    auto *watcher = new QFutureWatcher<FolderSync::Comparison>(this);
    runBusy(watcher, tr("Порівнюю папку з галереєю…"));
    offerSyncStop(control, tr("Перервати порівняння?"));
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, gallery, folder] {
        const FolderSync::Comparison comparison = watcher->future().resultCount() > 0 ? watcher->result()
                                                                                      : FolderSync::Comparison{};
        if (comparison.stopped) {
            statusBar()->showMessage(tr("Порівняння з папкою перервано."), 8000);
            return;
        }
        if (!comparison.error.isEmpty()) {
            QMessageBox::warning(this, tr("Синхронізація з папкою"),
                                 tr("Порівняти не вдалося:\n%1").arg(comparison.error));
            return;
        }
        chooseSyncAction(gallery, folder, comparison);
    });
    const qint64 galleryId = gallery.id;
    watcher->setFuture(QtConcurrent::run([galleryId, folder, mode, control](QPromise<FolderSync::Comparison> &promise) {
        FolderSync::compare(promise, galleryId, folder, mode, control.get());
    }));
}

void MainWindow::chooseSyncAction(const Gallery &gallery, const QString &folder,
                                  const FolderSync::Comparison &comparison)
{
    const QString title = tr("Синхронізація з папкою");
    const FolderSync::Comparison &c = comparison;
    QString text = tr("Галерея: %1\nПапка: %2")
                       .arg(QDir::toNativeSeparators(gallery.rootFolder), QDir::toNativeSeparators(folder));
    text += "\n\n"
            + tr("Синхронізовано (на своїх місцях): %1\n"
                 "Переміщено (лежать в інших папках): %2\n"
                 "Змінено (той самий шлях, інший вміст): %3\n"
                 "Є лише в галереї: %4\n"
                 "Є лише в папці: %5")
                  .arg(c.synced).arg(c.moved.size()).arg(c.changed.size()).arg(c.onlyInGallery.size())
                  .arg(c.onlyInFolder.size());
    if (!c.unreadable.isEmpty())
        text += '\n' + tr("Не вдалося прочитати (їх не буде змінено): %1").arg(c.unreadable.size());

    QStringList moved, changed, onlyGallery, onlyFolder;
    for (const FolderSync::Pair &pair : c.moved)
        moved << tr("%1 (у галереї: %2)").arg(QDir::toNativeSeparators(pair.folder.path),
                                              QDir::toNativeSeparators(pair.gallery.path));
    for (const FolderSync::Pair &pair : c.changed)
        changed << pair.folder.path;
    for (const FolderSync::File &file : c.onlyInGallery)
        onlyGallery << file.path;
    for (const FolderSync::File &file : c.onlyInFolder)
        onlyFolder << file.path;
    const QString details = pathList(tr("Переміщено (шлях у папці, у дужках — у галереї):"), moved)
                            + pathList(tr("Змінено:"), changed) + pathList(tr("Є лише в галереї:"), onlyGallery)
                            + pathList(tr("Є лише в папці:"), onlyFolder)
                            + pathList(tr("Не вдалося прочитати:"), c.unreadable);

    if (!c.differs()) {
        QMessageBox box(QMessageBox::Information, title, tr("Відмінностей немає.") + "\n\n" + text,
                        QMessageBox::Ok, this);
        if (!details.isEmpty())
            box.setDetailedText(details.trimmed());
        box.exec();
        return;
    }

    QMessageBox box(QMessageBox::Question, title, text, QMessageBox::NoButton, this);
    box.setInformativeText(
        tr("Що зробити?\n\n"
           "«Папку — як галерею»: з папки буде видалено файлів: %1, переміщено: %2, скопійовано туди: %3, "
           "замінено: %4.\n\n"
           "«Галерею — як папку»: з галереї буде видалено файлів: %3, переміщено: %2, додано з папки: %1, "
           "замінено: %4. Переміщені файли зберігають свої теги.\n\n"
           "Видалене й замінене йде в Кошик; якщо Кошика там немає, програма спитає.")
            .arg(QString::number(c.onlyInFolder.size()), QString::number(c.moved.size()),
                 QString::number(c.onlyInGallery.size()), QString::number(c.changed.size()))
        + "\n\n"
        + tr("«Об'єднати»: у папку буде скопійовано файлів: %1, у галерею додано: %2 (з тегами з їхніх папок). "
             "Нічого не видаляється й не переміщується; якщо ім'я вже зайняте (змінені файли), копія отримує "
             "«ім'я (1)».")
              .arg(QString::number(c.onlyInGallery.size() + c.changed.size()),
                   QString::number(c.onlyInFolder.size() + c.changed.size())));
    box.setDetailedText(details.trimmed());
    QPushButton *nothing = box.addButton(tr("Нічого не робити"), QMessageBox::RejectRole);
    QPushButton *toFolder = box.addButton(tr("Папку — як галерею"), QMessageBox::AcceptRole);
    QPushButton *toGallery = box.addButton(tr("Галерею — як папку"), QMessageBox::AcceptRole);
    QPushButton *merge = box.addButton(tr("Об'єднати"), QMessageBox::AcceptRole);
    toFolder->setToolTip(tr("Галерея — джерело істини: папка стане такою самою, як галерея"));
    toGallery->setToolTip(tr("Папка — джерело істини: галерея стане такою самою, як папка"));
    merge->setToolTip(tr("Кожна сторона отримає файли, яких у неї немає; нічого не видаляється"));
    box.setDefaultButton(nothing);
    box.setEscapeButton(nothing);
    box.exec();
    FolderSync::Direction direction;
    if (box.clickedButton() == toFolder)
        direction = FolderSync::Direction::FolderLikeGallery;
    else if (box.clickedButton() == toGallery)
        direction = FolderSync::Direction::GalleryLikeFolder;
    else if (box.clickedButton() == merge)
        direction = FolderSync::Direction::Merge;
    else
        return;

    // Deleting user files: one more question, "no" by default. Merge deletes nothing.
    const bool galleryFollowsFolder = direction == FolderSync::Direction::GalleryLikeFolder;
    const qsizetype deletes = galleryFollowsFolder ? c.onlyInGallery.size() : c.onlyInFolder.size();
    if (direction != FolderSync::Direction::Merge && deletes + c.changed.size() > 0) {
        const QString question =
            galleryFollowsFolder
                ? tr("З галереї буде видалено файлів: %1, замінено: %2 (старі версії теж видаляються).")
                : tr("З папки буде видалено файлів: %1, замінено: %2 (старі версії теж видаляються).");
        if (QMessageBox::warning(this, title,
                                 question.arg(QString::number(deletes), QString::number(c.changed.size())) + "\n"
                                     + tr("Усе це піде в Кошик; якщо Кошика немає, програма спитає.\n\nПродовжити?"),
                                 QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
            != QMessageBox::Yes)
            return;
    }
    runFolderSync(gallery, folder, comparison, direction);
}

void MainWindow::runFolderSync(const Gallery &gallery, const QString &folder, const FolderSync::Comparison &comparison,
                               FolderSync::Direction direction)
{
    using FolderSync::Direction;
    const QString title = tr("Синхронізація з папкою");
    auto control = std::make_shared<FolderSync::Control>();
    auto *watcher = new QFutureWatcher<FolderSync::ApplyResult>(this);
    runBusy(watcher, direction == Direction::GalleryLikeFolder ? tr("Змінюю галерею за папкою…")
                     : direction == Direction::FolderLikeGallery ? tr("Змінюю папку за галереєю…")
                                                                 : tr("Об'єдную галерею з папкою…"));
    offerSyncStop(control, tr("Перервати синхронізацію?\n\nУже зроблене залишиться, а файл, що копіюється зараз, "
                              "буде прибрано."));
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, title, direction] {
        const FolderSync::ApplyResult result = watcher->future().resultCount() > 0 ? watcher->result()
                                                                                   : FolderSync::ApplyResult{};
        if (direction != Direction::FolderLikeGallery) {
            for (qint64 id : result.removedImageIds)
                m_selectedIds.remove(id);
            reloadTags();
            loadPage(0);
            updateSelectionUi();
        }
        QString message = result.stopped ? tr("Синхронізацію перервано. Уже зроблене залишилось.")
                                         : tr("Синхронізацію завершено.");
        if (direction == Direction::Merge) {
            message += "\n\n" + tr("Скопійовано в папку: %1\nДодано в галерею: %2\nЗ новим ім'ям («ім'я (1)»): %3")
                                    .arg(result.copied).arg(result.added).arg(result.renamed);
        } else {
            message += "\n\n"
                       + tr("Видалено в Кошик: %1\nПереміщено: %2\nСкопійовано: %3\nЗамінено: %4\n"
                            "Прибрано пустих папок: %5")
                             .arg(result.deleted).arg(result.moved).arg(result.copied + result.added)
                             .arg(result.replaced).arg(result.removedFolders);
        }
        if (result.deletedPermanently > 0)
            message += '\n' + tr("Видалено остаточно, без Кошика (з вашої згоди): %1").arg(result.deletedPermanently);
        if (!result.failed.isEmpty()) {
            QStringList shown = result.failed.mid(0, 20);
            if (result.failed.size() > shown.size())
                shown << tr("… і ще %1").arg(result.failed.size() - shown.size());
            message += "\n\n" + tr("Не вдалося:\n%1").arg(shown.join('\n'));
        }
        if (!result.error.isEmpty())
            message += "\n\n" + tr("Файли змінено, але записати зміни в базу не вдалося:\n%1\n\nЗроби рескан.")
                                    .arg(result.error);
        if (result.failed.isEmpty() && result.error.isEmpty())
            QMessageBox::information(this, title, message);
        else
            QMessageBox::warning(this, title, message);
    });
    // Asked from the worker; it waits for the answer. Nothing is deleted permanently without a "yes".
    const Scanner::TrashQuestion ask = [this, title](const QString &path, Scanner::TrashProblem problem,
                                                     const QString &error) {
        Scanner::TrashChoice choice = Scanner::TrashChoice::Stop;
        QMetaObject::invokeMethod(this, [&] {
            QWidget *parent = m_busyDialog->isVisible() ? static_cast<QWidget *>(m_busyDialog) : this;
            choice = TrashPrompt::ask(parent, title, path, problem, error, true);
        }, Qt::BlockingQueuedConnection);
        return choice;
    };
    const qint64 galleryId = gallery.id;
    const QString root = gallery.rootFolder;
    watcher->setFuture(QtConcurrent::run([galleryId, root, folder, comparison, direction, control,
                                          ask](QPromise<FolderSync::ApplyResult> &promise) {
        switch (direction) {
        case Direction::GalleryLikeFolder:
            FolderSync::applyToGallery(promise, galleryId, root, folder, comparison, control.get(), ask);
            break;
        case Direction::FolderLikeGallery:
            FolderSync::applyToFolder(promise, root, folder, comparison, control.get(), ask);
            break;
        case Direction::Merge:
            FolderSync::merge(promise, galleryId, root, folder, comparison, control.get());
            break;
        }
    }));
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

        // Step 2: copy + register. "Перервати" stops after the current file; what is copied stays.
        auto *importer = new QFutureWatcher<Scanner::ImportStats>(this);
        runBusy(importer, tr("Додаю файли в галерею…"));
        auto stop = std::make_shared<std::atomic_bool>(false);
        m_busyDialog->setStopHandler([stop] { *stop = true; });
        connect(importer, &QFutureWatcherBase::finished, this, [this, importer] {
            const Scanner::ImportStats stats = importer->future().resultCount() > 0 ? importer->result()
                                                                                     : Scanner::ImportStats{};
            reloadTags();
            loadPage(0);
            QString message = tr("Скопійовано — %1, дублікатів — %2.").arg(stats.copied).arg(stats.duplicates);
            if (stats.stopped)
                message.prepend(tr("Додавання перервано. Скопійовані файли залишились у галереї.") + "\n\n");
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
        importer->setFuture(QtConcurrent::run([galleryId, root, items, stop](QPromise<Scanner::ImportStats> &promise) {
            Scanner::importFiles(promise, galleryId, root, items, stop.get());
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
    m_busyDialog->setStopHandler({}); // a job that can stop says so after runBusy
    m_busyDialog->setSpinning(false);
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
