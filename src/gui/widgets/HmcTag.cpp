#include "HmcTag.hpp"

#include <QStyle>

HmcTag::HmcTag(const QString& text, const QString& status, QWidget* parent)
    : QLabel(text, parent) {
    setProperty("hmcTag", true);
    setProperty("status", status);
    // Keep the pill tight to its text rather than stretching in a layout.
    setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
}

void HmcTag::setStatus(const QString& status) {
    setProperty("status", status);
    style()->unpolish(this);
    style()->polish(this);
}
