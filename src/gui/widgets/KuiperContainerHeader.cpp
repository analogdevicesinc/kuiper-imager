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

#include "KuiperContainerHeader.hpp"

#include <QLabel>
#include <QVBoxLayout>

#include "util/Repolish.hpp"

KuiperContainerHeader::KuiperContainerHeader(const QString& heading,
                                       const QString& subtitle, QWidget* parent)
    : QWidget(parent) {
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(2);

    heading_ = new QLabel(heading, this);
    heading_->setProperty("kuiperHeading", true);
    col->addWidget(heading_);

    subtitle_ = new QLabel(subtitle, this);
    subtitle_->setProperty("kuiperSubtitle", true);
    subtitle_->setWordWrap(true);
    subtitle_->setVisible(!subtitle.isEmpty());
    col->addWidget(subtitle_);
}

void KuiperContainerHeader::setDensity(const QString& density) {
    heading_->setProperty("density", density);
    kuiper::gui::repolish(heading_);
}
