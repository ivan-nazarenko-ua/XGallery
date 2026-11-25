#ifndef DOWNLOADPROFILE_H
#define DOWNLOADPROFILE_H

#include <QJsonObject>
#include <QList>
#include <QPair>
#include <QString>

// How the download manager finds what to download on a page. Stored in the DB (program-wide,
// not per gallery) and exported/imported as JSON.
struct DownloadProfile
{
    enum class Mode {
        Page, // save the page itself (page_N.html / .bin); the fields below are not used
        Files // download the links found on the page
    };
    enum class Tag {
        Any,
        Img,
        A
    };

    QString name;      // unique, trimmed, not empty
    QString userAgent; // empty: Qt's default User-Agent
    Mode mode = Mode::Files;
    Tag tag = Tag::Any;
    QString source;  // attribute holding the link; empty: the tag's body (text without inner tags)
    QString classes; // space-separated; all must be in the tag's class attribute (case-sensitive)
    // Attributes the tag must have: name (case-insensitive, as in HTML) -> exact value, case ignored.
    QList<QPair<QString, QString>> attributes;
    // Files mode: how many times a file is tried in all (the first try included). Failed files are
    // tried again after the whole list, in the same folder.
    int attempts = DefaultAttempts;

    static constexpr int DefaultAttempts = 3;
    static constexpr int MaxAttempts = 99;

    // Trims the text fields, drops attributes without a name (and repeats of one, the first wins),
    // keeps attempts within 1…MaxAttempts.
    void normalize();

    QJsonObject toJson() const;
    // false if the object is not a profile (no name, unknown mode/tag).
    static bool fromJson(const QJsonObject &object, DownloadProfile *profile);
};

namespace DownloadProfiles {

// The file "Експорт профілів" writes: {"format": ..., "version": 1, "profiles": [...]}.
QByteArray toFile(const QList<DownloadProfile> &profiles);
// Parses such a file. Profiles that can't be read are counted in *skipped. false (and *error)
// if the file itself is not a profile export.
bool fromFile(const QByteArray &data, QList<DownloadProfile> *profiles, int *skipped, QString *error);

} // namespace DownloadProfiles

#endif // DOWNLOADPROFILE_H
