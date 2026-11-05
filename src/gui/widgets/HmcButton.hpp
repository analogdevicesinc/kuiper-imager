#pragma once

#include <QPushButton>

// A QPushButton skinned to Harmonic's button component. Variant and state are
// carried as dynamic properties that the generated QSS keys on
// (QPushButton[hmc="true"][kind="..."]); changing one re-polishes the widget so
// the stylesheet re-evaluates. See src/gui/theme/theme-*.qss.
class HmcButton : public QPushButton {
    Q_OBJECT
public:
    explicit HmcButton(const QString& text, QWidget* parent = nullptr);
    explicit HmcButton(QWidget* parent = nullptr);

    // "primary" | "secondary" | "tertiary" | "ghost" (default "primary").
    void setKind(const QString& kind);
};
