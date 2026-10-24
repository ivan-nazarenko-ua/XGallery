#include "i18n.h"

#include <QCoreApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>

namespace {

QString g_current = QStringLiteral("uk");

QTranslator &appTranslator()
{
    static QTranslator translator;
    return translator;
}

// Qt's own texts (standard buttons, file dialogs). Found next to the Qt installation;
// if a deployment has no translations folder they simply stay English.
QTranslator &qtTranslator()
{
    static QTranslator translator;
    return translator;
}

} // namespace

namespace I18n {

QList<Language> languages()
{
    return {
        {QStringLiteral("uk"), QStringLiteral("Українська"), QStringLiteral(":/flags/ua.png")},
        {QStringLiteral("en"), QStringLiteral("English"), QStringLiteral(":/flags/us.png")},
        {QStringLiteral("fr"), QStringLiteral("Français"), QStringLiteral(":/flags/fr.png")},
    };
}

bool isKnown(const QString &code)
{
    for (const Language &language : languages())
        if (language.code == code)
            return true;
    return false;
}

void apply(const QString &code)
{
    const QString lang = isKnown(code) ? code : QStringLiteral("uk");

    QCoreApplication::removeTranslator(&appTranslator());
    QCoreApplication::removeTranslator(&qtTranslator());

    QLocale::setDefault(QLocale(lang)); // numbers and sizes ("4,8 MB") in the same language
    if (qtTranslator().load(QStringLiteral("qtbase_") + lang,
                            QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        QCoreApplication::installTranslator(&qtTranslator());
    // Ukrainian is the source language: no translator needed.
    if (lang != QStringLiteral("uk")
        && appTranslator().load(QStringLiteral(":/i18n/xgallery_") + lang))
        QCoreApplication::installTranslator(&appTranslator());

    g_current = lang;
}

QString current()
{
    return g_current;
}

} // namespace I18n
