//include <QCoreApplication>
#include <QtGui>
#include <QWindow>
#include <QSystemTrayIcon>
#include <QObject>

void iconActivated(QSystemTrayIcon::ActivationReason reason)
{
    printf("activated\n");
}

int main(int argc, char *argv[])
{
    //QApplication app(argc, argv);
    QGuiApplication app(argc, argv);

    //QIcon icon = QIcon::fromTheme("edit-undo");
    QIcon icon("Cloud.ico");
    printf("%s\n", icon.name().toStdString().c_str());


    QWindow window;
    window.resize(320, 240);
    window.setTitle("Hello world");
    window.setIcon(icon);
    window.show();
printf("Hello world\n");

QSystemTrayIcon trayicon;
/*
connect(
        &trayicon,
        SIGNAL(activated(QSystemTrayIcon::ActivationReason)),
        SLOT(iconActivated(QSystemTrayIcon::ActivationReason))
            );
*/
/*
QObject::connect(
        &trayicon,
        SIGNAL(activated(QSystemTrayIcon::ActivationReason)),
        &iconActivated
            );
*/


QObject::connect(
        &trayicon,
        QSystemTrayIcon::activated,
        iconActivated
       );


trayicon.setIcon(icon);
//trayicon.setIcon(QSystemTrayIcon::Information);

trayicon.setToolTip("CoverFS");
trayicon.show();


    return app.exec();

    /*
    QCoreApplication a(argc, argv);

    return a.exec();
    */

}
