#include <QApplication>
#include <QStringList>
#include <QTimer>
#include <memory>

#include "ConsoleWindow.hpp"
#include "GalleryWindow.hpp"
#include "MainWindow.hpp"
#include "theme/ThemeManager.hpp"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    kuiper::theme::applyTheme(app, kuiper::theme::Theme::Light);

    // Default: the flasher console. `--gallery` shows the component gallery used
    // for designing by hand; `--drives` shows the original drive-list window.
    std::unique_ptr<QWidget> window;
    if (app.arguments().contains("--gallery"))
        window = std::make_unique<GalleryWindow>();
    else if (app.arguments().contains("--drives"))
        window = std::make_unique<MainWindow>();
    else
        window = std::make_unique<ConsoleWindow>();
    window->show();

    // Dev aid for designing by hand: KUIPER_SHOT=<path> renders the window to a
    // PNG and exits, so screens can be iterated headlessly (QT_QPA_PLATFORM=
    // offscreen) without a display. No effect unless the variable is set.
    if (qEnvironmentVariableIsSet("KUIPER_SHOT")) {
        QTimer::singleShot(300, [&] {
            window->grab().save(qEnvironmentVariable("KUIPER_SHOT"));
            app.quit();
        });
    }
    return app.exec();
}
