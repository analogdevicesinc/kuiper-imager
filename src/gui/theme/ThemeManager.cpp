#include "ThemeManager.hpp"

#include <QApplication>
#include <QFile>
#include <QString>

namespace kuiper::theme {

void applyTheme(QApplication& app, Theme theme) {
    const QString path = theme == Theme::Dark ? QStringLiteral(":/theme/theme-dark.qss")
                                              : QStringLiteral(":/theme/theme-light.qss");
    QFile file(path);
    if (!file.open(QFile::ReadOnly | QFile::Text)) {
        qWarning("ThemeManager: could not load %s", qPrintable(path));
        return;
    }
    app.setStyleSheet(QString::fromUtf8(file.readAll()));
}

}  // namespace kuiper::theme
