#pragma once

#include <QStyle>
#include <QWidget>

namespace kuiper::gui {

// Re-evaluate a widget's stylesheet after a dynamic style property changes — Qt
// does not do this automatically for dynamic properties. See theme-*.qss, which
// keys selectors on those properties (e.g. [status="danger"]).
//
// Descendants are re-polished too: selectors such as
// `QFrame[hmcBanner="true"][status="danger"] QLabel` style a *child* by its
// parent's property, and Qt only re-resolves the widget it is asked to polish.
inline void repolish(QWidget* w) {
    QStyle* style = w->style();
    style->unpolish(w);
    style->polish(w);
    for (QWidget* child : w->findChildren<QWidget*>()) {
        child->style()->unpolish(child);
        child->style()->polish(child);
    }
    w->update();
}

}  // namespace kuiper::gui
