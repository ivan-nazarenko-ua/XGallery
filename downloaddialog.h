#ifndef DOWNLOADDIALOG_H
#define DOWNLOADDIALOG_H

#include <QDialog>
#include <QList>
#include <QPointer>
#include <QStringList>
#include <QTimer>
#include <QUrl>

#include <memory>

#include "database.h"
#include "downloadprofile.h"
#include "httpfetcher.h"
#include "scanner.h"

class QComboBox;
class QFile;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QProgressBar;
class QPushButton;
class QRadioButton;
class QTableWidget;
class QTemporaryDir;
class TagPickerDialog;

// "Завантажити з інтернету": downloads a page, or the files a profile finds on it, into a new
// numbered folder ("<root folder>/006-collection") of the gallery. GET requests only, one at a
// time, with a random pause of 0.1–3 s before each file. Files are imported (copied from a
// temporary folder) with the tags of this window only; duplicates are kept. Login, password,
// token and tags live only as long as the window. While it works, only "Зупинити" is usable;
// stopping keeps (imports) what has been downloaded.
class DownloadDialog : public QDialog
{
    Q_OBJECT

public:
    explicit DownloadDialog(const Gallery &gallery, QWidget *parent = nullptr);
    ~DownloadDialog() override;

signals:
    void filesAdded(); // the gallery got new files: the main window refreshes

public slots:
    void done(int result) override;

private:
    // Profiles
    DownloadProfile profileFromFields() const;
    void showProfile(const DownloadProfile &profile);
    void reloadProfiles(const QString &select);
    QListWidgetItem *selectedProfileItem() const;
    void loadSelectedProfile();
    void saveProfile();
    void deleteProfile();
    void exportProfiles();
    void importProfiles();
    void updateModeUi();
    void addAttributeRow(const QString &name, const QString &value);

    // Tags
    void chooseTags();
    void removeSelectedTags();
    void copySelectedTags();
    void pasteTags();
    void refreshTags();

    // Downloading
    void startOrStop();
    void start();
    void onPageFetched(const HttpFetcher::Result &result);
    void scheduleNextFile();
    void downloadNextFile();
    void onFileFetched(const HttpFetcher::Result &result);
    void importDownloads();
    void finishRun(const QString &summary, bool problems);
    void setRunning(bool running);
    QString downloadSummary() const;
    QString shownUrl(const QUrl &url) const; // shortened in the middle to the window's width

    Gallery m_gallery;

    QListWidget *m_profileList;
    QLineEdit *m_profileNameEdit;
    QLineEdit *m_userAgentEdit;
    QRadioButton *m_pageModeRadio;
    QRadioButton *m_filesModeRadio;
    QWidget *m_filesOptions; // locked in page mode
    QComboBox *m_tagCombo;
    QLineEdit *m_sourceEdit;
    QLineEdit *m_classesEdit;
    QTableWidget *m_attributesTable;
    QLineEdit *m_urlEdit;
    QLineEdit *m_userEdit;
    QLineEdit *m_passwordEdit;
    QLineEdit *m_tokenEdit;
    QListWidget *m_tagList;
    QPushButton *m_removeTagButton;
    QComboBox *m_folderCombo;
    QWidget *m_profileTab;    // tab "Профіль": profiles and the profile fields
    QWidget *m_form;          // tab "Завантаження" without the progress and the start/stop button
    QProgressBar *m_progress;
    QLabel *m_statusLabel;
    QPushButton *m_startButton;

    QStringList m_tags;
    QPointer<TagPickerDialog> m_picker;

    // The run
    HttpFetcher *m_fetcher;
    QTimer m_pauseTimer;
    bool m_running = false;
    bool m_importing = false;
    bool m_stopped = false;
    DownloadProfile m_runProfile;
    QStringList m_runTags;
    QString m_targetDir; // relative to the gallery root: "other/006-collection"
    std::unique_ptr<QTemporaryDir> m_tempDir;
    std::unique_ptr<QFile> m_file; // the one being downloaded
    QList<QUrl> m_links;
    int m_next = 0;
    QStringList m_failed; // "link — reason"
    QList<Scanner::ImportItem> m_items;
};

#endif // DOWNLOADDIALOG_H
