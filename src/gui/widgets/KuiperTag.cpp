#include "KuiperTag.hpp"

#include "util/Repolish.hpp"

KuiperTag::KuiperTag(const QString& text, const QString& status, QWidget* parent)
    : QLabel(text, parent) {
    setProperty("kuiperTag", true);
    setProperty("status", status);
    // Keep the pill tight to its text rather than stretching in a layout.
    setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
}

void KuiperTag::setStatus(const QString& status) {
    setProperty("status", status);
    kuiper::gui::repolish(this);
}
