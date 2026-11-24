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

#include "KuiperCard.hpp"

#include <QVBoxLayout>

KuiperCard::KuiperCard(QWidget* parent) : QFrame(parent) {
    setProperty("kuiperCard", true);
    layout_ = new QVBoxLayout(this);
    layout_->setContentsMargins(20, 20, 20, 20);  // medium-density card
    layout_->setSpacing(12);
}

void KuiperCard::addWidget(QWidget* w) { layout_->addWidget(w); }
void KuiperCard::addLayout(QLayout* l) { layout_->addLayout(l); }
