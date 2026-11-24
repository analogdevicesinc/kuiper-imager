#pragma once

#include <QPushButton>

// A styled QPushButton. Variant and state are carried as dynamic properties
// that the QSS keys on (QPushButton[kuiperButton="true"][kind="..."]); changing
// one re-polishes the widget so the stylesheet re-evaluates.
// See src/gui/theme/theme-*.qss.
class KuiperButton : public QPushButton {
    Q_OBJECT
public:
    explicit KuiperButton(const QString& text, QWidget* parent = nullptr);
    explicit KuiperButton(QWidget* parent = nullptr);

    // "primary" | "secondary" | "tertiary" | "ghost" (default "primary").
    void setKind(const QString& kind);
};
