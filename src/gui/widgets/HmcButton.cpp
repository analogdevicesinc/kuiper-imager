#include "HmcButton.hpp"

#include <QStyle>

HmcButton::HmcButton(const QString& text, QWidget* parent)
    : QPushButton(text, parent) {
    setProperty("hmc", true);
    setProperty("kind", "primary");
    setProperty("size", "md");
    setCursor(Qt::PointingHandCursor);
}

HmcButton::HmcButton(QWidget* parent) : HmcButton(QString(), parent) {}

void HmcButton::setKind(const QString& kind) {
    setProperty("kind", kind);
    repolish();
}

void HmcButton::setSize(const QString& size) {
    setProperty("size", size);
    repolish();
}

void HmcButton::setProcessing(bool processing) {
    setProperty("processing", processing);
    setEnabled(!processing);
    repolish();
}

// Re-evaluate the stylesheet after a styling property changes — Qt does not do
// this automatically for dynamic properties.
void HmcButton::repolish() {
    style()->unpolish(this);
    style()->polish(this);
    update();
}
