#ifndef I18N_H
#define I18N_H

#include <QList>
#include <QString>

// UI language. Source strings are Ukrainian; English and French come from translations
// (translations/*.ts, compiled into the exe). Switching installs other translators, and
// Qt then sends QEvent::LanguageChange to every window (see MainWindow::changeEvent).
namespace I18n {

// Key in the DB settings table; empty = not chosen yet (the language dialog is shown).
inline constexpr const char *SettingKey = "language";

struct Language
{
    QString code;       // "uk", "en", "fr"
    QString nativeName; // the language's own name: "Українська", "English", "Français"
    QString flag;       // resource path of the flag picture
};

QList<Language> languages();
bool isKnown(const QString &code);

// Installs the translators for `code` (unknown -> Ukrainian).
void apply(const QString &code);
QString current();

} // namespace I18n

#endif // I18N_H
