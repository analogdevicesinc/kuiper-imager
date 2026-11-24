// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Analog Devices, Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "ThemeManager.hpp"

#include <QApplication>
#include <QFile>
#include <QPalette>
#include <QString>

#include "theme/Tokens.hpp"

namespace kuiper::theme {

namespace {

// Build a palette from the generated tokens for this theme. QSS skins every
// styled widget, but palette-driven bits — a bare QLineEdit, text-edit viewports,
// tooltips, message boxes — fall back to the QApplication palette, which defaults
// to the platform's (usually light) one and so ignores the in-app theme. Setting
// the palette to match keeps those surfaces in step; QSS still wins where present.
QPalette buildPalette(Theme theme) {
    const bool dark = theme == Theme::Dark;
    const QColor window = dark ? dark::layoutCanvas : light::layoutCanvas;
    const QColor container =
        dark ? dark::layoutContainer : light::layoutContainer;
    const QColor content = dark ? dark::content : light::content;
    const QColor weak = dark ? dark::contentWeak : light::contentWeak;
    const QColor base = dark ? dark::inputBackground : light::inputBackground;
    const QColor primary =
        dark ? dark::interactivePrimary : light::interactivePrimary;

    QPalette p;
    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, content);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, container);
    p.setColor(QPalette::Text, content);
    p.setColor(QPalette::Button, container);
    p.setColor(QPalette::ButtonText, content);
    p.setColor(QPalette::PlaceholderText, weak);
    p.setColor(QPalette::ToolTipBase, container);
    p.setColor(QPalette::ToolTipText, content);
    p.setColor(QPalette::Highlight, primary);
    // Canvas is the colour farthest from content, so it reads on the highlight in
    // both themes (white-ish on light's blue, near-black on dark's light blue).
    p.setColor(QPalette::HighlightedText, window);
    p.setColor(QPalette::Disabled, QPalette::Text, weak);
    p.setColor(QPalette::Disabled, QPalette::WindowText, weak);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, weak);
    return p;
}

}  // namespace

void applyTheme(QApplication& app, Theme theme) {
    const QString path = theme == Theme::Dark
                             ? QStringLiteral(":/theme/theme-dark.qss")
                             : QStringLiteral(":/theme/theme-light.qss");
    QFile file(path);
    if (!file.open(QFile::ReadOnly | QFile::Text)) {
        qWarning("ThemeManager: could not load %s", qPrintable(path));
        return;
    }
    app.setPalette(buildPalette(theme));
    app.setStyleSheet(QString::fromUtf8(file.readAll()));
}

}  // namespace kuiper::theme
