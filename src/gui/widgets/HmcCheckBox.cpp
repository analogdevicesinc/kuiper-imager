#include "HmcCheckBox.hpp"

HmcCheckBox::HmcCheckBox(const QString& text, QWidget* parent)
    : QCheckBox(text, parent) {
    setProperty("hmcCheck", true);
    setCursor(Qt::PointingHandCursor);
}
