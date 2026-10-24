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

private slots:
    void createGallery();
    void editGallery();
    void onGalleryChanged(int index);
    void startRescan();
    void applySearch();
    void clearSearch();
    void showPreviousPage();
    void showNextPage();
    void openThumbnail(int index);
    void editImageMeta(int index);
    void editTagsOfSelection(); // all selected files, on any page
    void clearSelection();
    void updateSelectionUi();
    void findDuplicates();
    void deleteJunk(); // Thumbs.db, desktop.ini and empty folders
    void addFilesToGallery();
    void addFolderToGallery();
    void showTagMenu(const QPoint &pos);
    void renameTag(const QString &oldName);

private:
    static constexpr int GridColumns = 4;
    static constexpr int GridRows = 4;
    static constexpr int PageSize = GridColumns * GridRows;
    static constexpr int ThumbnailSize = 160;

    void buildUi();
    void retranslateUi();
    void refreshPageTexts();
    void reloadGalleries(qint64 selectId);
    void askForRescan(const QString &question);
    const Gallery *currentGallery() const;
    QStringList galleryNamesExcept(qint64 galleryId) const;
    Db::SortOrder sortOrder() const;
    // Asks for a first-level folder of the gallery (existing, or a new name). Empty on cancel.
    QString chooseRootTag(const QString &title, const QString &suggestion);
    // Collects files (in a worker) with collect(), then copies them into the gallery.
    void runImport(std::function<QList<Scanner::ImportItem>()> collect);
    void reloadTags();
    void loadPage(int page);
    // A file was rewritten by the program (saved rotation): refresh its DB record and the grid.
    void fileRewritten(qint64 galleryId, const QString &root, const QString &file);
    void wireMetaDialog(ImageMetaDialog &dialog);
    void addTagToEdit(QLineEdit *edit, const QString &tag);
    QString absolutePath(const QString &relativePath) const;
    static QStringList parseTags(const QString &text); // display text -> normalized tags
    static QString noTagsDisplayName();
    static QStringList displayTags(const QStringList &tags); // tags -> what the UI shows

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
    QAction *m_duplicatesAction = nullptr;
    QAction *m_junkAction = nullptr;
    QAction *m_quitAction = nullptr;
    QLabel *m_galleryLabel = nullptr;
    QPushButton *m_createGalleryButton = nullptr;
    QLabel *m_includeLabel = nullptr;
    QLabel *m_excludeLabel = nullptr;
    QLabel *m_sortLabel = nullptr;
    QPushButton *m_searchButton = nullptr;
    QPushButton *m_clearSearchButton = nullptr;
    QGroupBox *m_tagsBox = nullptr;
    QCheckBox *m_hideForeignTagsCheck = nullptr; // tag list shows only tags of the last search result

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
    QPushButton *m_selectAllButton = nullptr;
    QPushButton *m_clearSelectionButton = nullptr;
    QLabel *m_selectionLabel = nullptr;
    QSet<qint64> m_selectedIds; // bulk-edit selection, kept across pages and searches
    QLabel *m_pageLabel = nullptr;

    QStringList m_includeTags;
    QStringList m_excludeTags;
    bool m_exactTags = false;
    QList<ImageRecord> m_pageRecords;
    int m_page = 0;
    int m_totalCount = 0;

    BusyDialog *m_busyDialog = nullptr;
    QTimer m_busyShowTimer;
    int m_busyDepth = 0;
};
#endif // MAINWINDOW_H
