#include <QApplication>
#include <QCoreApplication>
#include <QFont>
#include <QFontDatabase>
#include <QStringList>
#include "dashboardwindow.h"
#include "arm_console.hpp"
#include <cstring>
#include <cstdio>
#include <QDebug>

int main(int argc, char *argv[])
{
    // The serial client must never instantiate a GUI or another GPIO owner.
    for(int i=1;i<argc;++i){
        if(std::strcmp(argv[i],"--arm-command")==0){
            QCoreApplication client(argc,argv);
            if(i+1!=argc-1){std::fputs("Expected --arm-command followed by one JSON argument\n",stderr);return 2;}
            return ArmConsole::command(QString::fromLocal8Bit(argv[i+1]));
        }
    }
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("hdmi_ui"));
    app.setOrganizationName(QStringLiteral("DR1"));

    // Register Latin/digits as well as CJK. The embedded Linux font directory
    // can be empty, and DroidSansFallback alone has no Latin glyphs.
    QFontDatabase::addApplicationFont(QApplication::applicationDirPath()
        + QStringLiteral("/fonts/DejaVuSans.ttf"));
    QString fontFamily = QStringLiteral("Noto Sans CJK SC");
    const int fontId = QFontDatabase::addApplicationFont(
        QApplication::applicationDirPath()
        + QStringLiteral("/fonts/DroidSansFallbackFull.ttf"));
    if (fontId >= 0) {
        const QStringList families = QFontDatabase::applicationFontFamilies(fontId);
        if (!families.isEmpty())
            fontFamily = families.first();
    }
    QFont font(fontFamily);
    font.setPointSize(11);
    app.setFont(font);

    DashboardWindow window;
    window.showFullScreen();
    if(app.arguments().contains(QStringLiteral("--arm-page")))window.showArmPage();
    if(app.arguments().contains(QStringLiteral("--conveyor-page")))window.showConveyorPage();
    ArmConsole console([&window](const QJsonObject &command){return window.handleArmConsoleCommand(command);});
    QString consoleError;
    if(!console.start(consoleError))qWarning()<<"Arm console unavailable:"<<consoleError;
    return app.exec();
}
