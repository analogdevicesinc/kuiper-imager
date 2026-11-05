#include <QApplication>
#include <QDir>
#include <QGuiApplication>
#include <QFontDatabase>
#include <QTimer>
#include <memory>

#include "ConsoleWindow.hpp"
#include "theme/ThemeManager.hpp"

namespace {

// Register any bundled OFL fonts (Barlow headings, Inter body) so the Harmonic
// typography renders true to ADI instead of a system fallback. No-op until the
// TTFs are added under theme/fonts/ and listed in theme.qrc.
void loadBundledFonts() {
    const QDir fonts(":/theme/fonts");
    for (const QString& f : fonts.entryList({"*.ttf", "*.otf"}, QDir::Files))
        QFontDatabase::addApplicationFont(fonts.filePath(f));
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    // Tie the process to its installed .desktop so the compositor can resolve the
    // app's icon and group its windows — the reliable mechanism under Wayland
    // (StartupWMClass in the .desktop covers the X11 case). Harmless if unset.
    QGuiApplication::setDesktopFileName("com.analogdevices.kuiperimager");
    loadBundledFonts();
    kuiper::theme::applyTheme(app, kuiper::theme::Theme::Light);

    auto window = std::make_unique<ConsoleWindow>();
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
