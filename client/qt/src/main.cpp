// main.cpp
#include <QApplication>
#include "ServerListWindow.h"

int main(int argc, char* argv[])
{
    // QApplication (not QCoreApplication): required for widgets
    // Handles the event loop, fonts, styles, etc.
    QApplication app(argc, argv);

    // Fusion style: modern and consistent rendering across all platforms
    app.setStyle("Fusion");

    ServerListWindow window;
    window.show();

    return app.exec();
}
