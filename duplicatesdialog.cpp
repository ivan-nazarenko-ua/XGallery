#include "duplicatesdialog.h"

#include "trashprompt.h"

#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace {

constexpr int ImageIdRole = Qt::UserRole;
constexpr int PathRole = Qt::UserRole + 1;
constexpr int GroupIndexRole = Qt::UserRole + 2; // group items: index in m_groups

bool hasUnignored(const Db::DuplicateGroup &group)
{
    return std::any_of(group.files.cbegin(), group.files.cend(), [](const Db::DuplicateFile &f) { return !f.ignored; });
}

bool hasIgnored(const Db::DuplicateGroup &group)
{
    return std::any_of(group.files.cbegin(), group.files.cend(), [](const Db::DuplicateFile &f) { return f.ignored; });
}

} // namespace

DuplicatesDialog::DuplicatesDialog(const Gallery &gallery, QWidget *parent)
    : QDialog(parent)
    , m_gallery(gallery)
    , m_showIgnored(new QCheckBox(tr("Показати пропущені дублікати"), this))
    , m_tree(new QTreeWidget(this))
    , m_summary(new QLabel(this))
    , m_deleteButton(new QPushButton(tr("Видалити"), this))
{
    setWindowTitle(tr("Дублікати — %1").arg(gallery.name));
    resize(800, 600); // as the main window starts

    m_tree->setColumnCount(1);
    m_tree->header()->hide();
    m_tree->setUniformRowHeights(true);
    m_tree->setSelectionMode(QAbstractItemView::NoSelection);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tree, &QTreeWidget::itemChanged, this, &DuplicatesDialog::updateButtons);
    connect(m_tree, &QWidget::customContextMenuRequested, this, &DuplicatesDialog::showContextMenu);
    connect(m_showIgnored, &QCheckBox::toggled, this, &DuplicatesDialog::populate);

    auto *hint = new QLabel(tr("Відміть файли, які треба видалити. Вони підуть у Кошик і зникнуть з бази.\n"
                               "Якщо відмітити всі файли групи, перший з них залишиться.\n"
                               "Правий клік на групі — «Ігнорувати»: група більше не показуватиметься, "
                               "поки в ній не з'явиться новий дублікат."),
                            this);
    hint->setWordWrap(true);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    buttons->button(QDialogButtonBox::Close)->setText(tr("Закрити"));
    buttons->addButton(m_deleteButton, QDialogButtonBox::ActionRole);
    connect(m_deleteButton, &QPushButton::clicked, this, &DuplicatesDialog::deleteChecked);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(hint);
    layout->addWidget(m_showIgnored);
    layout->addWidget(m_tree, 1);
    layout->addWidget(m_summary);
    layout->addWidget(buttons);

    // Entries of files that are gone, moved or changed would only pile up.
    Db::pruneIgnoredDuplicates(m_gallery.id);
    reload();
}

void DuplicatesDialog::reload()
{
    m_groups = Db::findDuplicates(m_gallery.id);
    for (Db::DuplicateGroup &group : m_groups)
        std::sort(group.files.begin(), group.files.end(), [](const Db::DuplicateFile &a, const Db::DuplicateFile &b) {
            return QString::compare(a.relativePath, b.relativePath, Qt::CaseInsensitive) < 0;
        });
    populate();
}

void DuplicatesDialog::populate()
{
    // Checks survive a rebuild (toggling the checkbox, ignoring a group) for files still shown.
    QSet<qint64> checkedIds;
    for (int g = 0; g < m_tree->topLevelItemCount(); ++g) {
        QTreeWidgetItem *group = m_tree->topLevelItem(g);
        for (int i = 0; i < group->childCount(); ++i)
            if (group->child(i)->checkState(0) == Qt::Checked)
                checkedIds.insert(group->child(i)->data(0, ImageIdRole).toLongLong());
    }

    const QSignalBlocker blocker(m_tree);
    m_tree->clear();

    const bool showIgnored = m_showIgnored->isChecked();
    const QDir root(m_gallery.rootFolder);
    QFont bold = m_tree->font();
    bold.setBold(true);

    // "<без тегів>" as the main window shows it (same translation).
    const QString noTags = QCoreApplication::translate("MainWindow", "<без тегів>");

    int groupNo = 0;
    int hidden = 0;
    for (int index = 0; index < m_groups.size(); ++index) {
        const Db::DuplicateGroup &group = m_groups.at(index);
        if (!showIgnored && !hasUnignored(group)) {
            ++hidden;
            continue;
        }
        const QList<Db::DuplicateFile> &files = group.files;

        auto *groupItem = new QTreeWidgetItem(m_tree);
        groupItem->setText(0, tr("Група %1 · файлів: %2 · розмір кожного: %3 · sha256 %4…")
                                  .arg(++groupNo)
                                  .arg(files.size())
                                  .arg(QLocale().formattedDataSize(group.size))
                                  .arg(group.sha256.left(12)));
        groupItem->setFont(0, bold);
        groupItem->setFlags(Qt::ItemIsEnabled);
        groupItem->setData(0, GroupIndexRole, index);

        for (const Db::DuplicateFile &file : std::as_const(files)) {
            const QString path = QDir::toNativeSeparators(root.filePath(file.relativePath));
            const QString virtualPath = file.virtualPath.isEmpty() ? noTags : file.virtualPath;
            // The collection, as the main window names it: its name, else its tags.
            const QString collection = file.collectionName.isEmpty() ? virtualPath : file.collectionName;
            QString text = path + " (" + collection + ")";
            if (file.ignored)
                text = tr("[пропущено]") + " " + text;
            if (!QFileInfo::exists(path))
                text += tr("   (файлу вже немає на диску)");
            auto *item = new QTreeWidgetItem(groupItem);
            item->setText(0, text);
            item->setToolTip(0, tr("Віртуальний шлях: %1").arg(virtualPath));
            item->setData(0, ImageIdRole, file.id);
            item->setData(0, PathRole, path);
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
            item->setCheckState(0, checkedIds.contains(file.id) ? Qt::Checked : Qt::Unchecked);
        }
        groupItem->setExpanded(true);
    }

    if (groupNo == 0) {
        auto *item = new QTreeWidgetItem(m_tree);
        item->setText(0, hidden > 0 ? tr("Нових дублікатів немає, лише пропущені (груп: %1).").arg(hidden)
                                    : tr("Дублікатів не знайдено."));
        item->setFlags(Qt::ItemIsEnabled);
    }
    updateButtons();
}

void DuplicatesDialog::showContextMenu(const QPoint &pos)
{
    QTreeWidgetItem *item = m_tree->itemAt(pos);
    if (item && item->parent())
        item = item->parent(); // a file: the menu is about its group
    if (!item || !item->data(0, GroupIndexRole).isValid())
        return;
    const int index = item->data(0, GroupIndexRole).toInt();
    if (index < 0 || index >= m_groups.size())
        return;

    QMenu menu(this);
    QAction *ignore = menu.addAction(tr("Ігнорувати"));
    ignore->setEnabled(hasUnignored(m_groups.at(index))); // otherwise it is ignored already
    QAction *unignore = menu.addAction(tr("Не ігнорувати"));
    unignore->setEnabled(hasIgnored(m_groups.at(index)));
    QAction *chosen = menu.exec(m_tree->viewport()->mapToGlobal(pos));
    if (!chosen || (chosen != ignore && chosen != unignore))
        return;
    const bool ignored = chosen == ignore; // both act on the whole group

    Db::DuplicateGroup &group = m_groups[index];
    QList<ImageRecord> files;
    for (const Db::DuplicateFile &file : std::as_const(group.files))
        files << file;
    if (!Db::setDuplicatesIgnored(m_gallery.id, files, ignored)) {
        QMessageBox::warning(this, windowTitle(),
                             (ignored ? tr("Не вдалося запам'ятати пропущені файли:\n%1")
                                      : tr("Не вдалося прибрати файли зі списку пропущених:\n%1"))
                                 .arg(Db::lastError()));
        return;
    }
    for (Db::DuplicateFile &file : group.files)
        file.ignored = ignored;
    populate();
}

void DuplicatesDialog::updateButtons()
{
    int groups = 0;
    int files = 0;
    int checked = 0;
    for (int g = 0; g < m_tree->topLevelItemCount(); ++g) {
        QTreeWidgetItem *group = m_tree->topLevelItem(g);
        if (group->childCount() == 0)
            continue;
        ++groups;
        files += group->childCount();
        for (int i = 0; i < group->childCount(); ++i)
            if (group->child(i)->checkState(0) == Qt::Checked)
                ++checked;
    }
    QString summary = tr("Груп: %1, файлів у них: %2, відмічено: %3").arg(groups).arg(files).arg(checked);
    if (!m_showIgnored->isChecked()) {
        const auto hidden = std::count_if(m_groups.cbegin(), m_groups.cend(),
                                          [](const Db::DuplicateGroup &g) { return !hasUnignored(g); });
        if (hidden > 0)
            summary += tr(", приховано пропущених груп: %1").arg(hidden);
    }
    m_summary->setText(summary);
    m_deleteButton->setEnabled(checked > 0);
}

void DuplicatesDialog::deleteChecked()
{
    struct Target
    {
        qint64 id;
        QString path;
    };
    QList<Target> targets;
    int keptFirst = 0;

    for (int g = 0; g < m_tree->topLevelItemCount(); ++g) {
        QTreeWidgetItem *group = m_tree->topLevelItem(g);
        QList<Target> checked;
        for (int i = 0; i < group->childCount(); ++i) {
            QTreeWidgetItem *item = group->child(i);
            if (item->checkState(0) == Qt::Checked)
                checked.append({item->data(0, ImageIdRole).toLongLong(), item->data(0, PathRole).toString()});
        }
        if (group->childCount() > 0 && checked.size() == group->childCount()) {
            checked.removeFirst(); // the whole group is checked: keep the first file
            ++keptFirst;
        }
        targets += checked;
    }
    if (targets.isEmpty())
        return;

    QString question = tr("Перемістити в Кошик і прибрати з бази файлів: %1?").arg(targets.size());
    if (keptFirst > 0)
        question += "\n\n" + tr("У групах, де відмічено всі файли (%1), перший файл залишиться.").arg(keptFirst);
    if (QMessageBox::question(this, windowTitle(), question) != QMessageBox::Yes)
        return;

    QList<qint64> removedIds;
    QStringList failed;
    int deletedPermanently = 0;
    bool deleteAllPermanently = false;
    bool stopped = false;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    // A question for the user, without the wait cursor over it.
    const auto ask = [this](const QString &path, Scanner::TrashProblem problem, const QString &error) {
        QApplication::restoreOverrideCursor();
        const Scanner::TrashChoice choice = TrashPrompt::ask(this, windowTitle(), path, problem, error, true);
        QApplication::setOverrideCursor(Qt::WaitCursor);
        return choice;
    };
    for (const Target &target : std::as_const(targets)) {
        // A file already gone from disk only needs to leave the DB.
        if (!QFileInfo::exists(target.path)) {
            removedIds << target.id;
            continue;
        }
        QString error;
        Scanner::RecycleOutcome outcome = Scanner::recycleFile(target.path, &error);
        if (outcome == Scanner::RecycleOutcome::NotRecyclable) {
            // Never deleted permanently without a "yes".
            const Scanner::TrashChoice choice = deleteAllPermanently
                ? Scanner::TrashChoice::DeleteAllPermanently
                : ask(target.path, Scanner::TrashProblem::NotRecyclable, QString());
            if (choice == Scanner::TrashChoice::Stop) {
                stopped = true;
                break;
            }
            deleteAllPermanently = choice == Scanner::TrashChoice::DeleteAllPermanently;
            QFile file(target.path);
            if (file.remove()) {
                removedIds << target.id;
                ++deletedPermanently;
                continue;
            }
            outcome = Scanner::RecycleOutcome::Failed;
            error = file.errorString();
        }
        if (outcome == Scanner::RecycleOutcome::Failed) {
            failed << target.path;
            if (ask(target.path, Scanner::TrashProblem::Failed, error) != Scanner::TrashChoice::Continue) {
                stopped = true;
                break;
            }
            continue;
        }
        removedIds << target.id;
    }
    const bool dbOk = removedIds.isEmpty() || Db::deleteImages(m_gallery.id, removedIds);
    QApplication::restoreOverrideCursor();
    if (stopped || deletedPermanently > 0) {
        QString text = stopped ? tr("Видалення перервано. Прибрано файлів: %1 з %2.").arg(removedIds.size()).arg(targets.size())
                               : QString();
        if (deletedPermanently > 0)
            text += (text.isEmpty() ? QString() : "\n") + tr("З них видалено остаточно, без Кошика: %1.").arg(deletedPermanently);
        QMessageBox::information(this, windowTitle(), text);
    }

    m_changed = m_changed || !removedIds.isEmpty();
    if (!dbOk)
        QMessageBox::warning(this, windowTitle(),
                             tr("Файли переміщено в Кошик, але базу оновити не вдалося:\n%1\n\n"
                                "Зроби рескан, щоб прибрати записи.")
                                 .arg(Db::lastError()));
    if (!failed.isEmpty())
        QMessageBox::warning(this, windowTitle(),
                             tr("Ці файли не вдалося перемістити в Кошик, вони залишились на місці й у базі:\n\n%1")
                                 .arg(failed.join('\n')));
    reload();
}
