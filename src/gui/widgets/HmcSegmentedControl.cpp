#include "HmcSegmentedControl.hpp"

#include <QButtonGroup>
#include <QFrame>
#include <QHBoxLayout>
#include <QPushButton>

#include "util/Repolish.hpp"

HmcSegmentedControl::HmcSegmentedControl(QWidget* parent) : QWidget(parent) {
    auto* outer = new QHBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    auto* frame = new QFrame(this);
    frame->setProperty("hmcSegmented", true);
    row_ = new QHBoxLayout(frame);
    row_->setContentsMargins(0, 0, 0, 0);
    row_->setSpacing(0);
    outer->addWidget(frame);
    outer->addStretch(1);  // keep the control its natural width

    group_ = new QButtonGroup(this);
    group_->setExclusive(true);
    connect(group_, &QButtonGroup::buttonToggled, this,
            [this](QAbstractButton* b, bool on) {
                if (on) emit valueChanged(b->property("segValue").toString());
            });
}

void HmcSegmentedControl::addSegment(const QString& text, const QString& value) {
    auto* btn = new QPushButton(text);
    btn->setCheckable(true);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setProperty("hmcSegment", true);
    btn->setProperty("segValue", value);
    if (compact_) btn->setProperty("compact", true);
    if (group_->buttons().isEmpty()) btn->setChecked(true);
    group_->addButton(btn);
    row_->addWidget(btn);
    relayoutPositions();
}

// Tag each button first/mid/last so the QSS can round the outer corners and draw
// dividers only between segments.
void HmcSegmentedControl::relayoutPositions() {
    const auto buttons = group_->buttons();
    for (int i = 0; i < buttons.size(); ++i) {
        const char* pos = i == 0 ? "first" : (i == buttons.size() - 1 ? "last" : "mid");
        auto* b = buttons[i];
        b->setProperty("pos", pos);
        kuiper::gui::repolish(b);
    }
}

QString HmcSegmentedControl::value() const {
    auto* b = group_->checkedButton();
    return b ? b->property("segValue").toString() : QString();
}

void HmcSegmentedControl::setCompact(bool compact) {
    compact_ = compact;
    for (auto* b : group_->buttons()) {
        b->setProperty("compact", compact);
        kuiper::gui::repolish(b);
    }
}
