#pragma once

#include <QCheckBox>

// Harmonic checkbox: a QCheckBox skinned with the selection tokens and a
// resource checkmark.
class HmcCheckBox : public QCheckBox {
    Q_OBJECT
public:
    explicit HmcCheckBox(const QString& text, QWidget* parent = nullptr);
};
