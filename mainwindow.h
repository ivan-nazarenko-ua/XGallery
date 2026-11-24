#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QSet>
#include <QTimer>

#include "database.h"
#include "scanner.h"

#include <functional>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
class QAction;
class QCheckBox;
class QGridLayout;
class QResizeEvent;
class QGroupBox;
class QMenu;
class QComboBox;
class QFutureWatcherBase;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QToolButton;
QT_END_NAMESPACE

class BusyDialog;
class ImageMetaDialog;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent *event) override; // language switch on the fly
    void resizeEvent(QResizeEvent *event) override; // short page label in a narrow window

private slots:
    void createGallery();
    void editGallery();
    void onGalleryChanged(int index);
    void startRescan();
    void applySearch();
    void clearSearch();
    void showPreviousPage();
    void showNextPage();
    void openThumbnail(int index);  // collection list: opens the collection; inside one: the file
    void editImageMeta(int index);  // collection list: collection properties; inside one: the file's
    void closeCollection();
    void editTagsOfSelection(); // all selected files, on any page
    void clearSelection();
    void updateSelectionUi();
    void selectWholeCollection();
    void findDuplicates();
    void deleteJunk(); // Thumbs.db, desktop.ini and empty folders
    void addFilesToGallery();
    void addFolderToGallery();
    void downloadFromInternet();
    void showTagMenu(const QPoint &pos);
    void renameTag(const QString &oldName);
    void setTagGroup(const QString &tag, qint64 groupId); // 0: no group
    void deleteTagsFromGallery(qint64 galleryId, const QStringList &tags);
    void setTagGroups(qint64 galleryId, const QHash<QString, qint64> &changes); // tag → group, 0 = none
    // Saved in a tag picker (metadata or download window): written at once, then the list and grid refresh.
    void applyGalleryTagEdits(qint64 galleryId, const QHash<QString, qint64> &groupChanges, const QStringList &deleted);

private:
    static constexpr int ThumbnailSize = 160;
    static constexpr int GridSpacing = 6;
    // Tag list item data. Qt::UserRole (bool) marks the "<без тегів>" service entry.
    static constexpr int GroupHeaderRole = Qt::UserRole + 1; // bool: a group's name, not a tag
    static constexpr int TagGroupRole = Qt::UserRole + 2;    // qint64: the tag's group, 0 = none

    void buildUi();
    void retranslateUi();
    void refreshPageTexts();
    // Below CompactWidth only "Сторінка N з M" is shown, the counts go to its tooltip.
    void updatePageLabel();
    static constexpr int CompactWidth = 900;
    void reloadGalleries(qint64 selectId);
    void askForRescan(const QString &question);
    const Gallery *currentGallery() const;
    QStringList galleryNamesExcept(qint64 galleryId) const;
    Db::SortOrder sortOrder() const;
    // Asks for a first-level folder of the gallery (existing, or a new name). Empty on cancel.
    QString chooseRootTag(const QString &title, const QString &suggestion);
    // Menu "Налаштування" → "Заходити в junction-папки": asks first, describing what changes;
    // on cancel the menu item goes back to its old state.
    void setFollowFolderLinks(bool follow);
    // Collects files (in a worker) with collect(), then copies them into the gallery.
    void runImport(std::function<QList<Scanner::ImportItem>()> collect);
    void reloadTags();
    void applyTagFilter();
    // A page of the collection list, or of the open collection's files, starting at the offset-th
    // item (top left). Pages follow each other from there: the offset need not be a multiple of the page size.
    void loadPage(int offset);
    // The grid shows as many thumbnails as fit (m_gridColumns × m_gridRows, at least 1 × 1).
    int pageSize() const { return m_gridColumns * m_gridRows; }
    void addThumbButton();
    void placeThumbButtons();
    void fitGridToSize(); // on a resize of the grid area; reloads the page (delayed) when the count changed
    bool eventFilter(QObject *watched, QEvent *event) override;
    // Collection mode: the grid shows the files of one collection; search, sorting and the tag
    // list are locked until "Закрити колекцію".
    bool inCollection() const { return m_openCollectionId != 0; }
    void openCollection(int index);
    void editCollection(int index);
    // Moves every file of the collection to the Recycle Bin (worker, with progress and "Перервати")
    // and drops the records of the ones that went. Already confirmed by the user.
    void deleteCollectionFiles(qint64 galleryId, const CollectionRecord &collection);
    // A file of that job can't go to the Recycle Bin: the user decides (GUI thread).
    Scanner::TrashChoice askAboutTrashProblem(const QString &path, Scanner::TrashProblem problem, const QString &error);
    void updateCollectionModeUi();
    // What the grid of the open collection shows: all its files, whatever the search.
    Db::TagQuery collectionQuery() const;
    // The stored name; without one, the tags (or "<без тегів>").
    static QString collectionDisplayName(const CollectionRecord &collection);
    // A file was rewritten by the program (saved rotation): refresh its DB record and the grid.
    void fileRewritten(qint64 galleryId, const QString &root, const QString &file);
    // Moves one gallery file to the Recycle Bin and drops its record (imageId 0: no record known);
    // messages go to messageParent. True = the file is gone from disk.
    bool trashImageFile(QWidget *messageParent, qint64 galleryId, qint64 imageId, const QString &path);
    void wireMetaDialog(ImageMetaDialog &dialog);
    bool renameImageFile(qint64 galleryId, const ImageRecord &record, const QString &newName);
    void exportFile(ImageMetaDialog &dialog, qint64 galleryId, const QString &root, const QString &destination);
    void addTagToEdit(QLineEdit *edit, const QString &tag);
    QString absolutePath(const QString &relativePath) const;
    // Search field text <-> normalized tags. "<без тегів>" (in the UI language) is not a tag:
    // parseTags reports it in *noTags, tagsText puts it first.
    static QStringList parseTags(const QString &text, bool *noTags = nullptr);
    static QString tagsText(const QStringList &tags, bool noTags);
    static QString noTagsDisplayName();
    // "<без тегів>" looks different from a real tag "Без тегів": bold on a tinted background.
    QColor noTagsBackground() const;
    void highlightNoTags(QLineEdit *edit);

    // Busy indicator + UI lock while a background job runs.
    void runBusy(QFutureWatcherBase *watcher, const QString &text);
    void beginBusy(const QString &text);
    void endBusy();
    bool isBusy() const { return m_busyDepth > 0; }

    Ui::MainWindow *ui;

    QMenu *m_fileMenu = nullptr;
    QAction *m_createGalleryAction = nullptr;
    QAction *m_editGalleryAction = nullptr;
    QAction *m_rescanAction = nullptr;
    QAction *m_addFilesAction = nullptr;
    QAction *m_addFolderAction = nullptr;
    QAction *m_downloadAction = nullptr;
    QAction *m_duplicatesAction = nullptr;
    QAction *m_junkAction = nullptr;
    QAction *m_quitAction = nullptr;
    QMenu *m_settingsMenu = nullptr;
    QAction *m_keepExtensionAction = nullptr;
    QAction *m_followLinksAction = nullptr; // rescan and "add folder" walk into junctions/symlinked folders
    QLabel *m_galleryLabel = nullptr;
    QPushButton *m_createGalleryButton = nullptr;
    QLabel *m_includeLabel = nullptr;
    QLabel *m_excludeLabel = nullptr;
    QLabel *m_sortLabel = nullptr;
    QPushButton *m_searchButton = nullptr;
    QPushButton *m_clearSearchButton = nullptr;
    QGroupBox *m_tagsBox = nullptr;
    QCheckBox *m_hideForeignTagsCheck = nullptr; // tag list shows only tags of the last search result
    QLineEdit *m_tagFilterEdit = nullptr; // tag list shows only entries containing this text; kept until the app closes
    QWidget *m_collectionBar = nullptr; // shown in collection mode
    QLabel *m_collectionLabel = nullptr;
    QPushButton *m_closeCollectionButton = nullptr;

    QList<Gallery> m_galleries;
    QComboBox *m_galleryCombo = nullptr;
    QComboBox *m_sortCombo = nullptr;
    QPushButton *m_editGalleryButton = nullptr;
    QLabel *m_rootLabel = nullptr;
    QPushButton *m_rescanButton = nullptr;
    QLineEdit *m_includeEdit = nullptr;
    QLineEdit *m_excludeEdit = nullptr;
    QCheckBox *m_exactCheck = nullptr; // "Жорстко": exactly the included tags
    QListWidget *m_tagList = nullptr;
    QList<QToolButton *> m_thumbButtons;
    QList<QCheckBox *> m_thumbChecks; // one per thumbnail, for bulk tag editing
    QPushButton *m_prevButton = nullptr;
    QPushButton *m_nextButton = nullptr;
    QPushButton *m_selectAllButton = nullptr;  // every file of the open collection
    QPushButton *m_selectPageButton = nullptr; // the files of this page
    QPushButton *m_clearSelectionButton = nullptr;
    QLabel *m_selectionLabel = nullptr;
    QSet<qint64> m_selectedIds; // bulk-edit selection, kept across pages and searches
    QList<qint64> m_selectableIds; // files of the open collection that can be ticked (not "other" files)
    QLabel *m_pageLabel = nullptr;
    QString m_pageTextFull;  // "Сторінка N з M (counts)"
    QString m_pageTextShort; // "Сторінка N з M"
    QString m_pageCounts;    // the counts alone, the short label's tooltip

    Db::TagQuery m_query; // the last applied search
    QList<ImageRecord> m_pageRecords;          // collection mode: the files on the page
    QList<CollectionRecord> m_pageCollections; // otherwise: the collections on the page
    qint64 m_openCollectionId = 0;             // 0 = the collection list is shown
    CollectionRecord m_openCollection;
    int m_collectionsOffset = 0; // top-left item of the collection list to return to
    int m_offset = 0;            // the top-left thumbnail's position in the whole list
    int m_gridColumns = 4;
    int m_gridRows = 4;
    QWidget *m_gridHost = nullptr;
    QGridLayout *m_gridLayout = nullptr;
    QTimer m_gridReloadTimer; // the grid size changed: reload once the resizing stops
    int m_totalCount = 0; // collections, or files of the open collection
    int m_totalFiles = 0; // files found by the search

    BusyDialog *m_busyDialog = nullptr;
    QTimer m_busyShowTimer;
    int m_busyDepth = 0;
    bool m_reloadPending = false; // loadPage() was called while busy; done after the thumbnails load
};
#endif // MAINWINDOW_H
