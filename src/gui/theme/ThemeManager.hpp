#pragma once

class QApplication;

namespace kuiper::theme {

enum class Theme { Light, Dark };

// Load the generated Harmonic stylesheet from the Qt resource bundle and apply
// it application-wide. Switching themes is just re-applying a different qss.
void applyTheme(QApplication& app, Theme theme = Theme::Light);

}  // namespace kuiper::theme
