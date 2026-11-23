#include "downloadprofile.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>

namespace {

const char FileFormat[] = "xgallery-download-profiles";
constexpr int FileVersion = 1;

QString modeName(DownloadProfile::Mode mode)
{
    return mode == DownloadProfile::Mode::Page ? QStringLiteral("page") : QStringLiteral("files");
}

QString tagName(DownloadProfile::Tag tag)
{
    switch (tag) {
    case DownloadProfile::Tag::Img:
        return QStringLiteral("img");
    case DownloadProfile::Tag::A:
        return QStringLiteral("a");
    case DownloadProfile::Tag::Any:
        break;
    }
    return QStringLiteral("any");
}

} // namespace

void DownloadProfile::normalize()
{
    name = name.trimmed();
    userAgent = userAgent.trimmed();
    source = source.trimmed();
    classes = classes.simplified();
    QList<QPair<QString, QString>> kept;
    QSet<QString> seen;
    for (const auto &attribute : std::as_const(attributes)) {
        const QString key = attribute.first.trimmed();
        if (key.isEmpty() || seen.contains(key.toLower()))
            continue;
        seen.insert(key.toLower());
        kept.append({key, attribute.second.trimmed()});
    }
    attributes = kept;
}

QJsonObject DownloadProfile::toJson() const
{
    QJsonArray attributeArray;
    for (const auto &attribute : attributes)
        attributeArray.append(QJsonObject{{"name", attribute.first}, {"value", attribute.second}});
    return QJsonObject{
        {"name", name},
        {"userAgent", userAgent},
        {"mode", modeName(mode)},
        {"tag", tagName(tag)},
        {"source", source},
        {"classes", classes},
        {"attributes", attributeArray},
    };
}

bool DownloadProfile::fromJson(const QJsonObject &object, DownloadProfile *profile)
{
    DownloadProfile result;
    result.name = object.value("name").toString();
    result.userAgent = object.value("userAgent").toString();

    const QString mode = object.value("mode").toString(modeName(Mode::Files));
    if (mode == modeName(Mode::Page))
        result.mode = Mode::Page;
    else if (mode == modeName(Mode::Files))
        result.mode = Mode::Files;
    else
        return false;

    const QString tag = object.value("tag").toString(tagName(Tag::Any));
    if (tag == tagName(Tag::Img))
        result.tag = Tag::Img;
    else if (tag == tagName(Tag::A))
        result.tag = Tag::A;
    else if (tag == tagName(Tag::Any))
        result.tag = Tag::Any;
    else
        return false;

    result.source = object.value("source").toString();
    result.classes = object.value("classes").toString();
    const QJsonArray attributes = object.value("attributes").toArray();
    for (const QJsonValue &value : attributes) {
        const QJsonObject attribute = value.toObject();
        result.attributes.append({attribute.value("name").toString(), attribute.value("value").toString()});
    }
    result.normalize();
    if (result.name.isEmpty())
        return false;
    *profile = result;
    return true;
}

namespace DownloadProfiles {

QByteArray toFile(const QList<DownloadProfile> &profiles)
{
    QJsonArray array;
    for (const DownloadProfile &profile : profiles)
        array.append(profile.toJson());
    const QJsonObject root{{"format", FileFormat}, {"version", FileVersion}, {"profiles", array}};
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

bool fromFile(const QByteArray &data, QList<DownloadProfile> *profiles, int *skipped, QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (document.isNull()) {
        *error = QCoreApplication::translate("DownloadProfiles", "Файл не є коректним JSON: %1")
                     .arg(parseError.errorString());
        return false;
    }
    const QJsonObject root = document.object();
    if (root.value("format").toString() != FileFormat || !root.value("profiles").isArray()) {
        *error = QCoreApplication::translate("DownloadProfiles", "Це не файл профілів завантаження XGallery.");
        return false;
    }
    if (root.value("version").toInt() > FileVersion) {
        *error = QCoreApplication::translate("DownloadProfiles",
                                             "Файл створено новішою версією програми, його не прочитати.");
        return false;
    }
    profiles->clear();
    *skipped = 0;
    const QJsonArray array = root.value("profiles").toArray();
    for (const QJsonValue &value : array) {
        DownloadProfile profile;
        if (DownloadProfile::fromJson(value.toObject(), &profile))
            profiles->append(profile);
        else
            ++*skipped;
    }
    return true;
}

} // namespace DownloadProfiles
