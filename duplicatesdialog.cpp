#include "duplicatesdialog.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

constexpr int ImageIdRole = Qt::UserRole;
constexpr int PathRole = Qt::UserRole + 1;

} // namespace

DuplicatesDialog::DuplicatesDialog(const Gallery &gallery, QWidget *parent)
    : QDialog(parent)
    , m_gallery(gallery)
    , m_tree(new QTreeWidget(this))
    , m_summary(new QLabel(this))
    , m_deleteButton(new QPushButton(tr("Видалити"), this))
{
    setWindowTitle(tr("Дублікати — %1").arg(gallery.name));
    resize(900, 600);

    m_tree->setColumnCount(1);
    m_tree->header()->hide();
    m_tree->setUniformRowHeights(true);
    m_tree->setSelectionMode(QAbstractItemView::NoSelection);
    connect(m_tree, &QTreeWidget::itemChanged, this, &DuplicatesDialog::updateButtons);

    auto *hint = new QLabel(tr("Відміть файли, які треба видалити. Вони підуть у Кошик і зникнуть з бази.\n"
                               "Якщо відмітити всі файли групи, перший з них залишиться."),
                            this);
    hint->setWordWrap(true);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    buttons->button(QDialogButtonBox::Close)->setText(tr("Закрити"));
    buttons->addButton(m_deleteButton, QDialogButtonBox::ActionRole);
    connect(m_deleteButton, &QPushButton::clicked, this, &DuplicatesDialog::deleteChecked);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(hint);
    layout->addWidget(m_tree, 1);
    layout->addWidget(m_summary);
    layout->addWidget(buttons);

    reload();
}

void DuplicatesDialog::reload()
{
    const QSignalBlocker blocker(m_tree);
    m_tree->clear();

    const QList<Db::DuplicateGroup> groups = Db::findDuplicates(m_gallery.id);
    const QDir root(m_gallery.rootFolder);
    QFont bold = m_tree->font();
    bold.setBold(true);

    int groupNo = 0;
    for (const Db::DuplicateGroup &group : groups) {
        QStringList paths;
        QHash<QString, qint64> idByPath;
        for (const ImageRecord &file : group.files) {
            const QString path = QDir::toNativeSeparators(root.filePath(file.relativePath));
            paths << path;
            idByPath.insert(path, file.id);
        }
        std::sort(paths.begin(), paths.end(), [](const QString &a, const QString &b) {
            return QString::compare(a, b, Qt::CaseInsensitive) < 0;
        });

        auto *groupItem = new QTreeWidgetItem(m_tree);
        groupItem->setText(0, tr("Група %1 · файлів: %2 · розмір кожного: %3 · sha256 %4…")
                                  .arg(++groupNo)
                                  .arg(paths.size())
                                  .arg(QLocale().formattedDataSize(group.size))
                                  .arg(group.sha256.left(12)));
        groupItem->setFont(0, bold);
        groupItem->setFlags(Qt::ItemIsEnabled);

        for (const QString &path : std::as_const(paths)) {
            auto *item = new QTreeWidgetItem(groupItem);
            const bool exists = QFileInfo::exists(path);
            item->setText(0, exists ? path : path + tr("   (файлу вже немає на диску)"));
            item->setData(0, ImageIdRole, idByPath.value(path));
            item->setData(0, PathRole, path);
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
            item->setCheckState(0, Qt::Unchecked);
        }
        groupItem->setExpanded(true);
    }

    if (groups.isEmpty()) {
        auto *item = new QTreeWidgetItem(m_tree);
        item->setText(0, tr("Дублікатів не знайдено."));
        item->setFlags(Qt::ItemIsEnabled);
    }
    updateButtons();
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
    m_summary->setText(tr("Груп: %1, файлів у них: %2, відмічено: %3").arg(groups).arg(files).arg(checked));
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
    QApplication::setOverrideCursor(Qt::WaitCursor);
    for (const Target &target : std::as_const(targets)) {
        // A file already gone from disk only needs to leave the DB.
        if (!QFileInfo::exists(target.path) || QFile::moveToTrash(target.path))
            removedIds << target.id;
        else
            failed << target.path;
    }
    const bool dbOk = removedIds.isEmpty() || Db::deleteImages(m_gallery.id, removedIds);
    QApplication::restoreOverrideCursor();

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
