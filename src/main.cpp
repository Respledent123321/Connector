#include <QApplication>
#include <QDir>

#include "app/MainWindow.h"
#include "core/AppPaths.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QDir::setCurrent(AppPaths::rootDir());

    MainWindow window;
    window.show();

    return app.exec();
}
