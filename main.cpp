#include "mainwindow.h"

#include <QApplication>
#include <QIcon>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    QApplication::setOrganizationName("Nova30");
    QApplication::setApplicationName("XGallery");
    QApplication::setWindowIcon(QIcon(":/xgallery.ico")); // all windows; the .exe icon comes from xgallery.rc

    MainWindow w;
    w.show();
    return a.exec();
}
