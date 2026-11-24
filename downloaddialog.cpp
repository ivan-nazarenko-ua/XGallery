#include "downloaddialog.h"

#include "downloadnames.h"
#include "htmllinks.h"
#include "tagpickerdialog.h"

#include <QAction>
#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QShortcut>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <algorithm>
#include <functional>

namespace {

const char ProfilesFolderKey[] = "download_profiles_folder"; // last folder of profile export/import
const char RootFolderKey[] = "download_root_folder";         // per gallery: the root folder last chosen
constexpr qint64 MaxPageBytes = 64LL * 1024 * 1024;          // a page to search for links
constexpr int MinPauseMs = 100;
constexpr int MaxPauseMs = 3000;
constexpr int MaxListedFailures = 20;

bool lessCaseInsensitive(const QString &a, const QString &b)
{
    return QString::compare(a, b, Qt::CaseInsensitive) < 0;
}

} // namespace

DownloadDialog::DownloadDialog(const Gallery &gallery, QWidget *parent)
    : QDialog(parent)
    , m_gallery(gallery)
    , m_profileList(new QListWidget(this))
    , m_profileNameEdit(new QLineEdit(this))
    , m_userAgentEdit(new QLineEdit(this))
    , m_pageModeRadio(new QRadioButton(tr("Завантажити сторінку"), this))
    , m_filesModeRadio(new QRadioButton(tr("Завантажити файли зі сторінки"), this))
    , m_filesOptions(new QWidget(this))
    , m_tagCombo(new QComboBox(this))
    , m_sourceEdit(new QLineEdit(this))
    , m_classesEdit(new QLineEdit(this))
    , m_attributesTable(new QTableWidget(0, 2, this))
    , m_urlEdit(new QLineEdit(this))
    , m_userEdit(new QLineEdit(this))
    , m_passwordEdit(new QLineEdit(this))
    , m_tokenEdit(new QLineEdit(this))
    , m_tagList(new QListWidget(this))
    , m_removeTagButton(new QPushButton(tr("Видалити тег"), this))
    , m_folderCombo(new QComboBox(this))
    , m_profileTab(new QWidget(this))
    , m_form(new QWidget(this))
    , m_progress(new QProgressBar(this))
    , m_statusLabel(new QLabel(this))
    , m_startButton(new QPushButton(this))
    , m_fetcher(new HttpFetcher(this))
{
    setWindowTitle(tr("Завантаження з інтернету — %1").arg(gallery.name));
    setMinimumWidth(680);

    // --- Profiles (as PuTTY's saved sessions) ---
    auto *profilesBox = new QGroupBox(tr("Профілі"), m_profileTab);
    m_profileNameEdit->setPlaceholderText(tr("назва профілю"));
    m_profileList->setMaximumHeight(90);
    // Selection, not the current item: on focus-in the list makes its first row current without selecting it,
    // which must not look like a chosen profile.
    connect(m_profileList, &QListWidget::itemSelectionChanged, this, [this] {
        if (const QListWidgetItem *item = selectedProfileItem())
            m_profileNameEdit->setText(item->text());
    });
    connect(m_profileList, &QListWidget::itemDoubleClicked, this, &DownloadDialog::loadSelectedProfile);
    auto *loadButton = new QPushButton(tr("Відкрити"), profilesBox);
    loadButton->setToolTip(tr("Заповнити поля обраним профілем (або подвійний клік по ньому)"));
    auto *saveButton = new QPushButton(tr("Зберегти"), profilesBox);
    saveButton->setToolTip(tr("Зберегти поля профілю під назвою з поля вище"));
    auto *deleteButton = new QPushButton(tr("Видалити"), profilesBox);
    auto *exportButton = new QPushButton(tr("Експорт…"), profilesBox);
    exportButton->setToolTip(tr("Зберегти всі профілі в JSON-файл"));
    auto *importButton = new QPushButton(tr("Імпорт…"), profilesBox);
    importButton->setToolTip(tr("Додати профілі з JSON-файлу (профілі з тими самими назвами буде замінено)"));
    connect(loadButton, &QPushButton::clicked, this, &DownloadDialog::loadSelectedProfile);
    connect(saveButton, &QPushButton::clicked, this, &DownloadDialog::saveProfile);
    connect(deleteButton, &QPushButton::clicked, this, &DownloadDialog::deleteProfile);
    connect(exportButton, &QPushButton::clicked, this, &DownloadDialog::exportProfiles);
    connect(importButton, &QPushButton::clicked, this, &DownloadDialog::importProfiles);
    auto *profileColumn = new QVBoxLayout;
    profileColumn->addWidget(m_profileNameEdit);
    profileColumn->addWidget(m_profileList);
    auto *profileButtons = new QVBoxLayout;
    profileButtons->addWidget(loadButton);
    profileButtons->addWidget(saveButton);
    profileButtons->addWidget(deleteButton);
    profileButtons->addSpacing(8);
    profileButtons->addWidget(exportButton);
    profileButtons->addWidget(importButton);
    profileButtons->addStretch(1);
    auto *profilesLayout = new QHBoxLayout(profilesBox);
    profilesLayout->addLayout(profileColumn, 1);
    profilesLayout->addLayout(profileButtons);

    // --- Profile fields ---
    auto *settingsBox = new QGroupBox(tr("Налаштування профілю"), m_profileTab);
    m_userAgentEdit->setPlaceholderText(tr("порожньо — стандартний"));
    m_filesModeRadio->setChecked(true);
    connect(m_pageModeRadio, &QRadioButton::toggled, this, &DownloadDialog::updateModeUi);
    auto *modeRow = new QHBoxLayout;
    modeRow->addWidget(m_pageModeRadio);
    modeRow->addWidget(m_filesModeRadio);
    modeRow->addStretch(1);

    m_tagCombo->addItem(tr("будь-який"), int(DownloadProfile::Tag::Any));
    m_tagCombo->addItem(QStringLiteral("img"), int(DownloadProfile::Tag::Img));
    m_tagCombo->addItem(QStringLiteral("a"), int(DownloadProfile::Tag::A));
    m_sourceEdit->setPlaceholderText(tr("ім'я атрибута, напр. src чи href; порожньо — вміст тегу"));
    m_classesEdit->setPlaceholderText(tr("класи через пробіл; порожньо — будь-які"));
    m_attributesTable->setHorizontalHeaderLabels({tr("Атрибут"), tr("Значення")});
    m_attributesTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_attributesTable->verticalHeader()->hide();
    m_attributesTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_attributesTable->setMaximumHeight(100);
    m_attributesTable->setToolTip(tr("Тег має мати кожен з цих атрибутів саме з таким значенням (регістр не важить)"));
    auto *addAttributeButton = new QPushButton(tr("Додати атрибут"), m_filesOptions);
    auto *removeAttributeButton = new QPushButton(tr("Видалити атрибут"), m_filesOptions);
    connect(addAttributeButton, &QPushButton::clicked, this, [this] {
        addAttributeRow(QString(), QString());
        const int row = m_attributesTable->rowCount() - 1;
        m_attributesTable->setCurrentCell(row, 0);
        m_attributesTable->editItem(m_attributesTable->item(row, 0));
    });
    connect(removeAttributeButton, &QPushButton::clicked, this, [this] {
        QList<int> rows;
        const auto ranges = m_attributesTable->selectedRanges();
        for (const QTableWidgetSelectionRange &range : ranges)
            for (int row = range.topRow(); row <= range.bottomRow(); ++row)
                rows << row;
        if (rows.isEmpty() && m_attributesTable->currentRow() >= 0)
            rows << m_attributesTable->currentRow();
        std::sort(rows.begin(), rows.end(), std::greater<int>());
        rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
        for (const int row : std::as_const(rows))
            m_attributesTable->removeRow(row);
    });
    auto *attributeButtons = new QHBoxLayout;
    attributeButtons->addWidget(addAttributeButton);
    attributeButtons->addWidget(removeAttributeButton);
    attributeButtons->addStretch(1);
    auto *attributesColumn = new QVBoxLayout;
    attributesColumn->setContentsMargins(0, 0, 0, 0);
    attributesColumn->addWidget(m_attributesTable);
    attributesColumn->addLayout(attributeButtons);

    auto *filesForm = new QFormLayout(m_filesOptions);
    filesForm->setContentsMargins(0, 0, 0, 0);
    filesForm->addRow(tr("Тег:"), m_tagCombo);
    filesForm->addRow(tr("Джерело посилань:"), m_sourceEdit);
    filesForm->addRow(tr("Клас:"), m_classesEdit);
    filesForm->addRow(tr("Атрибути:"), attributesColumn);

    auto *settingsForm = new QFormLayout(settingsBox);
    settingsForm->addRow(tr("User-Agent:"), m_userAgentEdit);
    settingsForm->addRow(tr("Режим:"), modeRow);
    settingsForm->addRow(m_filesOptions);

    // --- Address and access (never saved) ---
    auto *accessBox = new QGroupBox(tr("Сторінка"), m_form);
    m_urlEdit->setPlaceholderText(QStringLiteral("https://…"));
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_tokenEdit->setEchoMode(QLineEdit::Password);
    auto *accessForm = new QFormLayout(accessBox);
    accessForm->addRow(tr("Адреса сторінки:"), m_urlEdit);
    accessForm->addRow(tr("Ім'я користувача:"), m_userEdit);
    accessForm->addRow(tr("Пароль:"), m_passwordEdit);
    accessForm->addRow(tr("Bearer-токен:"), m_tokenEdit);
    auto *accessNote = new QLabel(tr("Ім'я, пароль і токен не зберігаються й надсилаються лише на сайт зі вказаної адреси."),
                                  accessBox);
    accessNote->setWordWrap(true);
    accessForm->addRow(accessNote);

    // --- Tags for every downloaded file (as in the metadata dialog) ---
    auto *tagsBox = new QGroupBox(tr("Теги завантажених файлів"), m_form);
    m_tagList->setSelectionMode(QAbstractItemView::ExtendedSelection);

    auto *copyAction = new QAction(tr("Копіювати теги"), m_tagList);
    copyAction->setShortcut(QKeySequence::Copy);
    copyAction->setShortcutContext(Qt::WidgetShortcut);
    connect(copyAction, &QAction::triggered, this, &DownloadDialog::copySelectedTags);
    auto *pasteAction = new QAction(tr("Вставити теги"), m_tagList);
    pasteAction->setShortcut(QKeySequence::Paste);
    pasteAction->setShortcutContext(Qt::WidgetShortcut);
    connect(pasteAction, &QAction::triggered, this, &DownloadDialog::pasteTags);
    m_tagList->addAction(copyAction);
    m_tagList->addAction(pasteAction);
    m_tagList->setContextMenuPolicy(Qt::ActionsContextMenu);
    connect(m_tagList, &QListWidget::itemSelectionChanged, this, [this] {
        m_removeTagButton->setEnabled(!m_tagList->selectedItems().isEmpty());
    });
    auto *deleteShortcut = new QShortcut(QKeySequence::Delete, m_tagList);
    deleteShortcut->setContext(Qt::WidgetShortcut);
    connect(deleteShortcut, &QShortcut::activated, this, &DownloadDialog::removeSelectedTags);
    connect(m_removeTagButton, &QPushButton::clicked, this, &DownloadDialog::removeSelectedTags);
    auto *addTagsButton = new QPushButton(tr("Додати теги"), tagsBox);
    connect(addTagsButton, &QPushButton::clicked, this, &DownloadDialog::chooseTags);
    auto *tagButtons = new QHBoxLayout;
    tagButtons->addWidget(m_removeTagButton);
    tagButtons->addWidget(addTagsButton);
    tagButtons->addStretch(1);
    auto *tagsLayout = new QVBoxLayout(tagsBox);
    tagsLayout->addWidget(m_tagList);
    tagsLayout->addLayout(tagButtons);

    // --- Where to ---
    m_folderCombo->setEditable(true);
    m_folderCombo->setInsertPolicy(QComboBox::NoInsert);
    m_folderCombo->addItems(Scanner::realSubfolders(gallery.rootFolder));
    // The folder chosen last time for this gallery; gone meanwhile: the first one, as before.
    const int remembered = m_folderCombo->findText(Db::gallerySetting(gallery.id, RootFolderKey), Qt::MatchFixedString);
    if (remembered >= 0)
        m_folderCombo->setCurrentIndex(remembered);
    // Remembered as soon as it changes: picked from the list, or a typed name once the field is left.
    const auto rememberFolder = [this] {
        const QString folder = m_folderCombo->currentText().trimmed();
        if (!folder.isEmpty() && Scanner::isValidFolderName(folder))
            Db::setGallerySetting(m_gallery.id, RootFolderKey, folder);
    };
    connect(m_folderCombo, &QComboBox::currentIndexChanged, this, rememberFolder);
    connect(m_folderCombo->lineEdit(), &QLineEdit::editingFinished, this, rememberFolder);
    m_folderCombo->setToolTip(tr("Перша папка галереї. Файли ляжуть у нову папку в ній: «006-collection» тощо.\n"
                                 "Можна вписати нову назву — папку буде створено."));
    auto *folderForm = new QFormLayout;
    folderForm->setContentsMargins(0, 0, 0, 0);
    folderForm->addRow(tr("Коренева папка:"), m_folderCombo);

    auto *profileLayout = new QVBoxLayout(m_profileTab);
    profileLayout->addWidget(profilesBox);
    profileLayout->addWidget(settingsBox);
    profileLayout->addStretch(1);

    auto *formLayout = new QVBoxLayout(m_form);
    formLayout->setContentsMargins(0, 0, 0, 0);
    formLayout->addWidget(accessBox);
    formLayout->addWidget(tagsBox, 1); // the tag list takes the free height of the tab
    formLayout->addLayout(folderForm);

    // --- Progress + start/stop ---
    m_progress->setTextVisible(false);
    m_progress->hide();
    m_statusLabel->setTextFormat(Qt::PlainText);
    m_statusLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred); // long links don't widen the window
    m_statusLabel->hide();
    m_startButton->setText(tr("Почати завантаження"));
    connect(m_startButton, &QPushButton::clicked, this, &DownloadDialog::startOrStop);
    auto *startRow = new QHBoxLayout;
    startRow->addStretch(1);
    startRow->addWidget(m_startButton);
    startRow->addStretch(1);

    auto *downloadTab = new QWidget(this);
    auto *downloadLayout = new QVBoxLayout(downloadTab);
    downloadLayout->addWidget(m_form, 1);
    downloadLayout->addWidget(m_progress);
    downloadLayout->addWidget(m_statusLabel);
    downloadLayout->addLayout(startRow);

    auto *tabs = new QTabWidget(this);
    tabs->addTab(m_profileTab, tr("Профіль"));
    tabs->addTab(downloadTab, tr("Завантаження"));
    tabs->setCurrentWidget(m_profileTab); // a profile is chosen first, then the page and the tags

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(tabs);

    // Enter in a text field must not start a download.
    for (QPushButton *button : findChildren<QPushButton *>())
        button->setAutoDefault(false);

    m_pauseTimer.setSingleShot(true);
    connect(&m_pauseTimer, &QTimer::timeout, this, &DownloadDialog::downloadNextFile);

    reloadProfiles(QString());
    refreshTags();
    updateModeUi();
}

DownloadDialog::~DownloadDialog()
{
    // The fetcher's reply must not write into a file that is gone.
    m_fetcher->disconnect(this);
    m_fetcher->abort();
}

void DownloadDialog::done(int result)
{
    if (m_running)
        return; // stop first; the window can't close while it works
    if (m_picker)
        m_picker->reject();
    QDialog::done(result);
}

// ---- Profiles -----------------------------------------------------------------------------

DownloadProfile DownloadDialog::profileFromFields() const
{
    DownloadProfile profile;
    profile.name = m_profileNameEdit->text();
    profile.userAgent = m_userAgentEdit->text();
    profile.mode = m_pageModeRadio->isChecked() ? DownloadProfile::Mode::Page : DownloadProfile::Mode::Files;
    profile.tag = DownloadProfile::Tag(m_tagCombo->currentData().toInt());
    profile.source = m_sourceEdit->text();
    profile.classes = m_classesEdit->text();
    for (int row = 0; row < m_attributesTable->rowCount(); ++row) {
        const QTableWidgetItem *name = m_attributesTable->item(row, 0);
        const QTableWidgetItem *value = m_attributesTable->item(row, 1);
        profile.attributes.append({name ? name->text() : QString(), value ? value->text() : QString()});
    }
    profile.normalize();
    return profile;
}

void DownloadDialog::showProfile(const DownloadProfile &profile)
{
    m_profileNameEdit->setText(profile.name);
    m_userAgentEdit->setText(profile.userAgent);
    (profile.mode == DownloadProfile::Mode::Page ? m_pageModeRadio : m_filesModeRadio)->setChecked(true);
    m_tagCombo->setCurrentIndex(qMax(0, m_tagCombo->findData(int(profile.tag))));
    m_sourceEdit->setText(profile.source);
    m_classesEdit->setText(profile.classes);
    m_attributesTable->setRowCount(0);
    for (const auto &attribute : profile.attributes)
        addAttributeRow(attribute.first, attribute.second);
    updateModeUi();
}

void DownloadDialog::addAttributeRow(const QString &name, const QString &value)
{
    const int row = m_attributesTable->rowCount();
    m_attributesTable->insertRow(row);
    m_attributesTable->setItem(row, 0, new QTableWidgetItem(name));
    m_attributesTable->setItem(row, 1, new QTableWidgetItem(value));
}

void DownloadDialog::updateModeUi()
{
    m_filesOptions->setEnabled(!m_pageModeRadio->isChecked());
}

void DownloadDialog::reloadProfiles(const QString &select)
{
    const QString typed = m_profileNameEdit->text();
    m_profileList->clear();
    const QList<DownloadProfile> profiles = Db::downloadProfiles();
    for (const DownloadProfile &profile : profiles) {
        auto *item = new QListWidgetItem(profile.name, m_profileList);
        if (profile.name == select)
            m_profileList->setCurrentItem(item);
    }
    if (select.isEmpty())
        m_profileNameEdit->setText(typed); // clearing the list must not change the typed name
}

QListWidgetItem *DownloadDialog::selectedProfileItem() const
{
    const QList<QListWidgetItem *> selected = m_profileList->selectedItems();
    return selected.isEmpty() ? nullptr : selected.first();
}

void DownloadDialog::loadSelectedProfile()
{
    const QListWidgetItem *item = selectedProfileItem();
    if (!item) {
        QMessageBox::information(this, windowTitle(), tr("Обери профіль у списку."));
        return;
    }
    const QList<DownloadProfile> profiles = Db::downloadProfiles();
    for (const DownloadProfile &profile : profiles)
        if (profile.name == item->text()) {
            showProfile(profile);
            return;
        }
    reloadProfiles(QString()); // deleted meanwhile
}

void DownloadDialog::saveProfile()
{
    const DownloadProfile profile = profileFromFields();
    if (profile.name.isEmpty()) {
        QMessageBox::information(this, windowTitle(), tr("Впиши назву профілю."));
        m_profileNameEdit->setFocus();
        return;
    }
    const QList<DownloadProfile> profiles = Db::downloadProfiles();
    const bool exists = std::any_of(profiles.cbegin(), profiles.cend(),
                                    [&profile](const DownloadProfile &p) { return p.name == profile.name; });
    if (exists
        && QMessageBox::question(this, windowTitle(), tr("Профіль «%1» уже є. Замінити його?").arg(profile.name))
               != QMessageBox::Yes)
        return;
    if (!Db::saveDownloadProfile(profile)) {
        QMessageBox::warning(this, windowTitle(), tr("Не вдалося зберегти профіль:\n%1").arg(Db::lastError()));
        return;
    }
    showProfile(profile); // trimmed, attributes without a name gone
    reloadProfiles(profile.name);
}

void DownloadDialog::deleteProfile()
{
    const QListWidgetItem *item = selectedProfileItem();
    if (!item) {
        QMessageBox::information(this, windowTitle(), tr("Обери профіль у списку."));
        return;
    }
    const QString name = item->text();
    if (QMessageBox::question(this, windowTitle(), tr("Видалити профіль «%1»?").arg(name)) != QMessageBox::Yes)
        return;
    if (!Db::deleteDownloadProfile(name)) {
        QMessageBox::warning(this, windowTitle(), tr("Не вдалося видалити профіль:\n%1").arg(Db::lastError()));
        return;
    }
    reloadProfiles(QString());
}

void DownloadDialog::exportProfiles()
{
    const QList<DownloadProfile> profiles = Db::downloadProfiles();
    if (profiles.isEmpty()) {
        QMessageBox::information(this, windowTitle(), tr("Збережених профілів немає."));
        return;
    }
    const QString folder = Db::setting(ProfilesFolderKey,
                                       QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
    const QString path = QFileDialog::getSaveFileName(this, tr("Експорт профілів"),
                                                      QDir(folder).filePath(QStringLiteral("xgallery-profiles.json")),
                                                      tr("JSON-файли (*.json)"));
    if (path.isEmpty())
        return;
    Db::setSetting(ProfilesFolderKey, QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(DownloadProfiles::toFile(profiles)) < 0 || !file.commit()) {
        QMessageBox::warning(this, windowTitle(), tr("Не вдалося записати файл:\n%1").arg(file.errorString()));
        return;
    }
    QMessageBox::information(this, windowTitle(), tr("Експортовано профілів: %1.").arg(profiles.size()));
}

void DownloadDialog::importProfiles()
{
    const QString folder = Db::setting(ProfilesFolderKey,
                                       QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
    const QString path = QFileDialog::getOpenFileName(this, tr("Імпорт профілів"), folder, tr("JSON-файли (*.json)"));
    if (path.isEmpty())
        return;
    Db::setSetting(ProfilesFolderKey, QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, windowTitle(), tr("Не вдалося прочитати файл:\n%1").arg(file.errorString()));
        return;
    }
    QList<DownloadProfile> profiles;
    int skipped = 0;
    QString error;
    if (!DownloadProfiles::fromFile(file.readAll(), &profiles, &skipped, &error)) {
        QMessageBox::warning(this, windowTitle(), error);
        return;
    }
    int replaced = 0;
    if (!profiles.isEmpty() && !Db::saveDownloadProfiles(profiles, &replaced)) {
        QMessageBox::warning(this, windowTitle(), tr("Не вдалося зберегти профілі:\n%1").arg(Db::lastError()));
        return;
    }
    reloadProfiles(QString());
    QString message = tr("Імпортовано профілів: %1 (з них замінено наявних: %2).").arg(profiles.size()).arg(replaced);
    if (skipped > 0)
        message += "\n" + tr("Пропущено пошкоджених записів: %1.").arg(skipped);
    QMessageBox::information(this, windowTitle(), message);
}

// ---- Tags ---------------------------------------------------------------------------------

void DownloadDialog::chooseTags()
{
    if (m_picker) {
        m_picker->raise();
        m_picker->activateWindow();
        return;
    }
    // All gallery tags, plus the ones of this window the gallery doesn't have yet.
    QList<TagSection> sections = Db::tagSections(m_gallery.id, Db::allTags(m_gallery.id));
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

    m_picker = new TagPickerDialog(sections, m_tags, QStringList(), Db::tagCollectionCounts(m_gallery.id), this);
    m_picker->setTagGroups(Db::tagGroups(m_gallery.id), m_pendingGroups);
    m_picker->setAttribute(Qt::WA_DeleteOnClose);
    connect(m_picker, &QDialog::accepted, this, [this] {
        for (const QString &tag : m_picker->addedTags())
            if (!m_tags.contains(tag))
                m_tags << tag;
        for (const QString &tag : m_picker->removedTags())
            m_tags.removeAll(tag);
        const QStringList deleted = m_picker->deletedTags();
        for (const QString &tag : deleted)
            m_tags.removeAll(tag);
        const QHash<QString, qint64> groupChanges = m_picker->groupChanges();
        if (!groupChanges.isEmpty() || !deleted.isEmpty())
            emit galleryTagsEdited(groupChanges, deleted);
        keepPendingGroups(groupChanges);
        refreshTags();
    });
    m_picker->show();
}

void DownloadDialog::removeSelectedTags()
{
    const auto selected = m_tagList->selectedItems();
    for (const QListWidgetItem *item : selected)
        m_tags.removeAll(item->text());
    refreshTags();
}

void DownloadDialog::copySelectedTags()
{
    QStringList selected;
    const auto items = m_tagList->selectedItems();
    for (const QListWidgetItem *item : items)
        selected << item->text();
    if (selected.isEmpty())
        return;
    std::sort(selected.begin(), selected.end(), lessCaseInsensitive);
    QGuiApplication::clipboard()->setText(selected.join(", "));
}

void DownloadDialog::pasteTags()
{
    const QStringList pasted = Db::tagsFromText(QGuiApplication::clipboard()->text());
    for (const QString &tag : pasted)
        if (!m_tags.contains(tag))
            m_tags << tag;
    refreshTags();
}

void DownloadDialog::keepPendingGroups(const QHash<QString, qint64> &changes)
{
    // Only tags of this window the gallery doesn't know: the stored ones got their group already.
    const QStringList stored = Db::allTags(m_gallery.id);
    m_pendingGroups.clear();
    for (auto it = changes.cbegin(); it != changes.cend(); ++it)
        if (m_tags.contains(it.key()) && !stored.contains(it.key()))
            m_pendingGroups.insert(it.key(), it.value());
}

void DownloadDialog::refreshTags()
{
    std::sort(m_tags.begin(), m_tags.end(), lessCaseInsensitive);
    m_tagList->clear();
    m_tagList->addItems(m_tags);
    m_removeTagButton->setEnabled(false);
    if (m_picker)
        m_picker->setAssignedTags(m_tags);
}

// ---- Downloading --------------------------------------------------------------------------

void DownloadDialog::startOrStop()
{
    if (!m_running) {
        start();
        return;
    }
    if (m_importing)
        return;
    m_stopped = true;
    m_startButton->setEnabled(false); // until what's downloaded is in the gallery
    if (m_pauseTimer.isActive()) {
        m_pauseTimer.stop();
        importDownloads();
    } else {
        m_fetcher->abort(); // its finished() goes on from there
    }
}

void DownloadDialog::start()
{
    QUrl url = QUrl::fromUserInput(m_urlEdit->text().trimmed());
    const QString scheme = url.scheme().toLower();
    if (m_urlEdit->text().trimmed().isEmpty() || !url.isValid() || url.host().isEmpty()
        || (scheme != QLatin1String("http") && scheme != QLatin1String("https"))) {
        QMessageBox::information(this, windowTitle(), tr("Впиши адресу сторінки (http:// або https://)."));
        m_urlEdit->setFocus();
        return;
    }
    const QString folder = m_folderCombo->currentText().trimmed();
    if (folder.isEmpty()) {
        QMessageBox::information(this, windowTitle(), tr("Обери кореневу папку."));
        m_folderCombo->setFocus();
        return;
    }
    if (!Scanner::isValidFolderName(folder)) {
        QMessageBox::warning(this, windowTitle(),
                             tr("Недопустима назва папки. Не можна використовувати символи \\ / : * ? \" < > |"));
        m_folderCombo->setFocus();
        return;
    }
    const QDir root(m_gallery.rootFolder);
    if (!root.exists()) {
        QMessageBox::warning(this, windowTitle(), tr("Папка галереї не існує: %1")
                                                      .arg(QDir::toNativeSeparators(m_gallery.rootFolder)));
        return;
    }
    m_tempDir = std::make_unique<QTemporaryDir>();
    if (!m_tempDir->isValid()) {
        QMessageBox::warning(this, windowTitle(), tr("Не вдалося створити тимчасову папку:\n%1")
                                                      .arg(m_tempDir->errorString()));
        m_tempDir.reset();
        return;
    }

    m_runProfile = profileFromFields();
    m_runTags = m_tags;
    m_targetDir = folder + '/' + DownloadNames::nextFolderName(root.filePath(folder));
    m_links.clear();
    m_next = 0;
    m_failed.clear();
    m_items.clear();
    m_stopped = false;
    m_fetcher->setUserAgent(m_runProfile.userAgent);
    m_fetcher->setCredentials({m_userEdit->text(), m_passwordEdit->text(), m_tokenEdit->text(), url.host()});
    setRunning(true);

    m_progress->setRange(0, 0);
    m_statusLabel->setText(tr("Завантажую сторінку…\n%1").arg(shownUrl(url)));
    connect(m_fetcher, &HttpFetcher::finished, this, &DownloadDialog::onPageFetched, Qt::SingleShotConnection);
    if (m_runProfile.mode == DownloadProfile::Mode::Page) {
        m_file = std::make_unique<QFile>(m_tempDir->filePath(QStringLiteral("page.part")));
        if (!m_file->open(QIODevice::WriteOnly)) {
            disconnect(m_fetcher, &HttpFetcher::finished, this, nullptr);
            finishRun(tr("Не вдалося створити тимчасовий файл:\n%1").arg(m_file->errorString()), true);
            return;
        }
        m_fetcher->get(url, m_file.get());
    } else {
        m_fetcher->get(url, nullptr, MaxPageBytes);
    }
}

void DownloadDialog::onPageFetched(const HttpFetcher::Result &result)
{
    if (m_file)
        m_file->close();
    if (result.aborted) {
        finishRun(tr("Завантаження зупинено."), false);
        return;
    }
    if (!result.ok) {
        finishRun(tr("Не вдалося завантажити сторінку:\n%1").arg(result.error), true);
        return;
    }

    if (m_runProfile.mode == DownloadProfile::Mode::Page) {
        const QDir target(QDir(m_gallery.rootFolder).filePath(m_targetDir));
        const QString name = DownloadNames::pageFileName(target, result.contentType);
        const QString path = m_tempDir->filePath(name);
        if (!m_file->rename(path)) {
            finishRun(tr("Не вдалося зберегти сторінку:\n%1").arg(m_file->errorString()), true);
            return;
        }
        m_file.reset();
        m_items.append({path, m_targetDir, name, true, m_runTags, false});
        importDownloads();
        return;
    }

    const QString html = HtmlLinks::decodePage(result.body, result.contentType);
    m_links = HtmlLinks::extractLinks(html, m_runProfile, result.finalUrl);
    if (m_links.isEmpty()) {
        finishRun(tr("На сторінці не знайдено посилань, що підходять під профіль."), true);
        return;
    }
    m_progress->setRange(0, int(m_links.size()));
    m_progress->setValue(0);
    scheduleNextFile();
}

void DownloadDialog::scheduleNextFile()
{
    if (m_stopped || m_next >= m_links.size()) {
        importDownloads();
        return;
    }
    // A pause before every request: no load spikes on the server, no anti-bot alarm.
    const int pause = int(QRandomGenerator::global()->bounded(MinPauseMs, MaxPauseMs + 1));
    m_statusLabel->setText(tr("Файл %1 з %2: пауза %3 с\n%4")
                               .arg(m_next + 1)
                               .arg(m_links.size())
                               .arg(QLocale().toString(pause / 1000.0, 'f', 1))
                               .arg(shownUrl(m_links.at(m_next))));
    m_pauseTimer.start(pause);
}

void DownloadDialog::downloadNextFile()
{
    const QUrl url = m_links.at(m_next);
    // A folder per file: the name it gets later can't collide with another download's.
    const QString folder = QString::number(m_next);
    if (!QDir(m_tempDir->path()).mkdir(folder)) {
        m_failed << tr("%1 — не вдалося створити тимчасову папку").arg(url.toDisplayString());
        ++m_next;
        m_progress->setValue(m_next);
        scheduleNextFile();
        return;
    }
    m_file = std::make_unique<QFile>(QDir(m_tempDir->filePath(folder)).filePath(QStringLiteral("download.part")));
    if (!m_file->open(QIODevice::WriteOnly)) {
        m_failed << tr("%1 — %2").arg(url.toDisplayString(), m_file->errorString());
        m_file.reset();
        ++m_next;
        m_progress->setValue(m_next);
        scheduleNextFile();
        return;
    }
    const QString header = tr("Файл %1 з %2").arg(m_next + 1).arg(m_links.size());
    const QString shown = shownUrl(url);
    m_statusLabel->setText(header + "\n" + shown);
    connect(m_fetcher, &HttpFetcher::progress, this, [this, header, shown](qint64 received, qint64) {
        m_statusLabel->setText(tr("%1: %2 КБ\n%3").arg(header).arg(received / 1024).arg(shown));
    });
    connect(m_fetcher, &HttpFetcher::finished, this, &DownloadDialog::onFileFetched, Qt::SingleShotConnection);
    m_fetcher->get(url, m_file.get());
}

void DownloadDialog::onFileFetched(const HttpFetcher::Result &result)
{
    disconnect(m_fetcher, &HttpFetcher::progress, this, nullptr);
    const QUrl url = m_links.at(m_next);
    m_file->close();
    if (result.aborted) {
        m_file->remove();
        m_file.reset();
        importDownloads();
        return;
    }
    if (!result.ok) {
        m_file->remove();
        m_failed << tr("%1 — %2").arg(url.toDisplayString(), result.error);
    } else {
        QString name = DownloadNames::nameFromUrl(url);
        if (name.isEmpty())
            name = DownloadNames::numberedName(m_next + 1, int(m_links.size()),
                                               DownloadNames::extensionFor(result.contentType));
        const QString path = QFileInfo(m_file->fileName()).dir().filePath(name);
        if (m_file->rename(path)) {
            m_items.append({path, m_targetDir, name, true, m_runTags, false});
        } else {
            m_failed << tr("%1 — %2").arg(url.toDisplayString(), m_file->errorString());
            m_file->remove();
        }
    }
    m_file.reset();
    ++m_next;
    m_progress->setValue(m_next);
    scheduleNextFile();
}

void DownloadDialog::importDownloads()
{
    if (m_items.isEmpty()) {
        finishRun(downloadSummary(), !m_failed.isEmpty());
        return;
    }
    m_importing = true;
    m_startButton->setEnabled(false);
    m_progress->setRange(0, 0);
    m_statusLabel->setText(tr("Додаю файли в галерею…"));

    auto *watcher = new QFutureWatcher<Scanner::ImportStats>(this);
    connect(watcher, &QFutureWatcherBase::progressRangeChanged, m_progress, &QProgressBar::setRange);
    connect(watcher, &QFutureWatcherBase::progressValueChanged, m_progress, &QProgressBar::setValue);
    connect(watcher, &QFutureWatcherBase::progressTextChanged, m_statusLabel, &QLabel::setText);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher] {
        watcher->deleteLater();
        const Scanner::ImportStats stats = watcher->future().resultCount() > 0 ? watcher->result()
                                                                                : Scanner::ImportStats{};
        // The new tags exist now: they get the groups chosen for them.
        if (!m_pendingGroups.isEmpty()) {
            const QHash<QString, qint64> pending = m_pendingGroups; // keepPendingGroups clears the member
            emit galleryTagsEdited(pending, {});
            keepPendingGroups(pending); // what a stopped run didn't bring in stays pending
        }
        emit filesAdded();
        QString message = downloadSummary();
        message += "\n" + tr("Додано в галерею — %1 (папка «%2»).").arg(stats.copied).arg(m_targetDir);
        if (stats.ignored > 0)
            message += "\n" + tr("Пропущено службових файлів (Thumbs.db, desktop.ini тощо) — %1.").arg(stats.ignored);
        if (!stats.failed.isEmpty())
            message += "\n\n" + tr("Не вдалося скопіювати:\n%1").arg(stats.failed.join('\n'));
        if (!stats.error.isEmpty())
            message += "\n\n" + stats.error;
        finishRun(message, !m_failed.isEmpty() || !stats.failed.isEmpty() || !stats.error.isEmpty());
    });
    const qint64 galleryId = m_gallery.id;
    const QString root = m_gallery.rootFolder;
    const QList<Scanner::ImportItem> items = m_items;
    watcher->setFuture(QtConcurrent::run([galleryId, root, items](QPromise<Scanner::ImportStats> &promise) {
        Scanner::importFiles(promise, galleryId, root, items);
    }));
}

QString DownloadDialog::downloadSummary() const
{
    QString message;
    if (m_stopped)
        message = tr("Завантаження зупинено.") + "\n";
    if (m_runProfile.mode == DownloadProfile::Mode::Page)
        return message + tr("Сторінку завантажено.");
    message += tr("Знайдено посилань — %1, завантажено — %2, з помилкою — %3.")
                   .arg(m_links.size())
                   .arg(m_items.size())
                   .arg(m_failed.size());
    if (!m_failed.isEmpty()) {
        message += "\n\n" + tr("Не завантажено:") + "\n" + m_failed.mid(0, MaxListedFailures).join('\n');
        if (m_failed.size() > MaxListedFailures)
            message += "\n" + tr("…і ще %1").arg(m_failed.size() - MaxListedFailures);
    }
    return message;
}

void DownloadDialog::finishRun(const QString &summary, bool problems)
{
    m_file.reset();
    m_tempDir.reset(); // removes the downloaded copies: the gallery has its own now
    setRunning(false);
    if (problems)
        QMessageBox::warning(this, windowTitle(), summary);
    else
        QMessageBox::information(this, windowTitle(), summary);
}

QString DownloadDialog::shownUrl(const QUrl &url) const
{
    return m_statusLabel->fontMetrics().elidedText(url.toDisplayString(), Qt::ElideMiddle, qMax(200, m_form->width()));
}

void DownloadDialog::setRunning(bool running)
{
    m_running = running;
    m_importing = false;
    m_profileTab->setEnabled(!running);
    m_form->setEnabled(!running);
    m_progress->setVisible(running);
    m_statusLabel->setVisible(running);
    m_startButton->setEnabled(true);
    m_startButton->setText(running ? tr("Зупинити завантаження") : tr("Почати завантаження"));
    if (running && m_picker)
        m_picker->reject(); // its tags would be lost to this run anyway
}
