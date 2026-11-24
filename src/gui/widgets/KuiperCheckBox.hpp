#pragma once

#include <QCheckBox>

// checkbox: a QCheckBox skinned with the selection tokens and a
// resource checkmark.
class KuiperCheckBox : public QCheckBox {
    Q_OBJECT
public:
    explicit KuiperCheckBox(const QString& text, QWidget* parent = nullptr);
};
