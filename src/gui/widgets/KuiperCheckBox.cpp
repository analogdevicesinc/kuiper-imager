#include "KuiperCheckBox.hpp"

KuiperCheckBox::KuiperCheckBox(const QString& text, QWidget* parent)
    : QCheckBox(text, parent) {
    setProperty("kuiperCheck", true);
    setCursor(Qt::PointingHandCursor);
}
