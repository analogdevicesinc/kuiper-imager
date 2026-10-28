#include <QApplication>

#include "MainWindow.hpp"
#include "theme/ThemeManager.hpp"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    kuiper::theme::applyTheme(app, kuiper::theme::Theme::Light);
    MainWindow window;
    window.show();
    return app.exec();
}
