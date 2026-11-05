#include "HmcButton.hpp"

#include "util/Repolish.hpp"

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
    kuiper::gui::repolish(this);
}
