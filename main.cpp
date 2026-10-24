#include "mainwindow.h"

#include "database.h"
#include "i18n.h"
#include "languagedialog.h"

#include <QApplication>
#include <QIcon>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    QApplication::setOrganizationName("Nova30");
    QApplication::setApplicationName("XGallery");
    QApplication::setWindowIcon(QIcon(":/xgallery.ico")); // all windows; the .exe icon comes from xgallery.rc

    // The UI language lives in the same DB as the galleries. First start: ask with flag buttons;
    // closing that window means Ukrainian (and the question comes again next time).
    QString language;
    if (Db::open(Db::MainConnection).isOpen()) {
        language = Db::setting(I18n::SettingKey);
        if (!I18n::isKnown(language)) {
            language = LanguageDialog::ask();
            if (!language.isEmpty())
                Db::setSetting(I18n::SettingKey, language);
        }
    }
    I18n::apply(language);

    MainWindow w;
    w.show();
    return a.exec();
}
