#ifndef DOWNLOADDIALOG_H
#define DOWNLOADDIALOG_H

#include <QDeadlineTimer>
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

class BusySpinner;
class QComboBox;
class QFile;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QProgressBar;
class QPushButton;
class QRadioButton;
class QSpinBox;
class QTableWidget;
class QTemporaryDir;
class TagPickerDialog;

// "Завантажити з інтернету": downloads a page, or the files a profile finds on it, into a new
// numbered folder ("<root folder>/006-collection") of the gallery. GET requests only, one at a
// time, with a random pause of 0.1–3 s before each file. Files that failed are tried again after
// the whole list, as many times as the profile says, after the pause the server asked for
// (Retry-After; else 10 s; over a minute only if the user agrees). Files are imported (copied from a
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
    // Saved in the tag picker: the main window writes them (groupChanges: tag → group, 0 = none;
    // deleted: tags to delete from the whole gallery) and refreshes.
    void galleryTagsEdited(const QHash<QString, qint64> &groupChanges, const QStringList &deleted);

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
    void keepPendingGroups(const QHash<QString, qint64> &changes); // the ones for tags not stored yet

    // Downloading
    void startOrStop();
    void start();
    void onPageFetched(const HttpFetcher::Result &result);
    void scheduleNextFile();
    void downloadNextFile();
    void onFileFetched(const HttpFetcher::Result &result);
    void fileFailed(const QString &reason, qint64 retryAfterMs = -1); // the current file
    void finishAttempt();     // the list is through: another attempt for the failed files, or the import
    void startNextAttempt();  // after the pause
    void showRetryCountdown();
    QString fileHeader() const; // "Файл 2 з 7", with the attempt from the second one on
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
    QSpinBox *m_attemptsSpin;
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
    BusySpinner *m_spinner; // runs during the pauses
    QLabel *m_statusLabel;
    QPushButton *m_startButton;

    QStringList m_tags;
    // Groups chosen for this window's tags the gallery doesn't have yet: applied once a download brings them in.
    QHash<QString, qint64> m_pendingGroups;
    QPointer<TagPickerDialog> m_picker;

    // The run
    HttpFetcher *m_fetcher;
    QTimer m_pauseTimer;     // before each file
    QTimer m_retryTimer;     // before another attempt
    QTimer m_countdownTimer; // updates the text during m_retryTimer
    QDeadlineTimer m_retryDeadline;
    bool m_running = false;
    bool m_importing = false;
    bool m_stopped = false;
    DownloadProfile m_runProfile;
    QStringList m_runTags;
    QString m_targetDir; // relative to the gallery root: "other/006-collection"
    std::unique_ptr<QTemporaryDir> m_tempDir;
    std::unique_ptr<QFile> m_file; // the one being downloaded
    QList<QUrl> m_links;
    QList<int> m_queue; // indexes into m_links this attempt downloads
    int m_next = 0;     // position in m_queue
    int m_attempt = 1;
    bool m_retriesDeclined = false; // the user didn't want to wait for another attempt
    struct Failure
    {
        int index; // into m_links
        QString reason;
        qint64 pauseMs; // before trying again
    };
    QList<Failure> m_failed;   // of the current attempt
    QList<Failure> m_retrying; // of the previous attempt: what m_queue tries again
    // Files not downloaded in the end: m_failed, and when stopped, the ones of m_retrying not tried again yet.
    QList<Failure> failures() const;
    QList<Scanner::ImportItem> m_items;
};

#endif // DOWNLOADDIALOG_H
