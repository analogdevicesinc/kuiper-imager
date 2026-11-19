#pragma once

#include <QLabel>

// Harmonic tag: a small status pill. status is one of
// info | success | warning | danger | highlight.
class HmcTag : public QLabel {
    Q_OBJECT
public:
    explicit HmcTag(const QString& text, const QString& status = "info",
                    QWidget* parent = nullptr);

    // Re-tint the pill after construction (keyed on the dynamic "status"
    // property, so it repolishes — see theme-*.qss).
    void setStatus(const QString& status);
};
