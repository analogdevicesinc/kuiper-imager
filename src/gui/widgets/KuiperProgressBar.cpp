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

#include "KuiperProgressBar.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

#include "util/Repolish.hpp"

KuiperProgressBar::KuiperProgressBar(QWidget* parent) : QWidget(parent) {
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(4);  // progress-bar gap

    auto* row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    phase_ = new QLabel(this);
    detail_ = new QLabel(this);
    detail_->setProperty("kuiperCaption", true);
    detail_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    row->addWidget(phase_, 1);
    row->addWidget(detail_, 0);
    col->addLayout(row);

    bar_ = new QProgressBar(this);
    bar_->setProperty("kuiperProgress", true);
    bar_->setTextVisible(false);
    bar_->setRange(0, 1000);
    col->addWidget(bar_);
}

void KuiperProgressBar::setPhase(const QString& text) { phase_->setText(text); }
void KuiperProgressBar::setDetail(const QString& text) { detail_->setText(text); }

void KuiperProgressBar::setFraction(double f) {
    f = std::clamp(f, 0.0, 1.0);
    bar_->setValue(static_cast<int>(std::lround(f * 1000)));
}

void KuiperProgressBar::setStatus(const QString& status) {
    bar_->setProperty("status", status);
    kuiper::gui::repolish(bar_);
}

void KuiperProgressBar::setIndeterminate(bool on) {
    bar_->setRange(0, on ? 0 : 1000);
}
