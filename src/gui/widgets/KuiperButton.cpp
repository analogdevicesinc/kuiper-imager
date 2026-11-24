#include "KuiperButton.hpp"

#include "util/Repolish.hpp"

KuiperButton::KuiperButton(const QString& text, QWidget* parent)
    : QPushButton(text, parent) {
    setProperty("kuiperButton", true);
    setProperty("kind", "primary");
    setProperty("size", "md");
    setCursor(Qt::PointingHandCursor);
}

KuiperButton::KuiperButton(QWidget* parent) : KuiperButton(QString(), parent) {}

void KuiperButton::setKind(const QString& kind) {
    setProperty("kind", kind);
    kuiper::gui::repolish(this);
}
