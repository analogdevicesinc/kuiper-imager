// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Analog Devices, Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "KuiperSegmentedControl.hpp"

#include <QButtonGroup>
#include <QFrame>
#include <QHBoxLayout>
#include <QPushButton>

#include "util/Repolish.hpp"

KuiperSegmentedControl::KuiperSegmentedControl(QWidget* parent) : QWidget(parent) {
    auto* outer = new QHBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    auto* frame = new QFrame(this);
    frame->setProperty("kuiperSegmented", true);
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

void KuiperSegmentedControl::addSegment(const QString& text, const QString& value) {
    auto* btn = new QPushButton(text);
    btn->setCheckable(true);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setProperty("kuiperSegment", true);
    btn->setProperty("segValue", value);
    if (compact_) btn->setProperty("compact", true);
    if (group_->buttons().isEmpty()) btn->setChecked(true);
    group_->addButton(btn);
    row_->addWidget(btn);
    relayoutPositions();
}

// Tag each button first/mid/last so the QSS can round the outer corners and draw
// dividers only between segments.
void KuiperSegmentedControl::relayoutPositions() {
    const auto buttons = group_->buttons();
    for (int i = 0; i < buttons.size(); ++i) {
        const char* pos = i == 0 ? "first" : (i == buttons.size() - 1 ? "last" : "mid");
        auto* b = buttons[i];
        b->setProperty("pos", pos);
        kuiper::gui::repolish(b);
    }
}

QString KuiperSegmentedControl::value() const {
    auto* b = group_->checkedButton();
    return b ? b->property("segValue").toString() : QString();
}

void KuiperSegmentedControl::setCompact(bool compact) {
    compact_ = compact;
    for (auto* b : group_->buttons()) {
        b->setProperty("compact", compact);
        kuiper::gui::repolish(b);
    }
}
