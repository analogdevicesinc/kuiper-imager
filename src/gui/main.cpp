#include <QApplication>
#include <QDir>
#include <QGuiApplication>
#include <QFontDatabase>
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
    QGuiApplication::setDesktopFileName("com.analogdevices.kuiperimager");
    loadBundledFonts();
    kuiper::theme::applyTheme(app, kuiper::theme::Theme::Light);

    auto window = std::make_unique<ConsoleWindow>();
    window->show();

    return app.exec();
}
