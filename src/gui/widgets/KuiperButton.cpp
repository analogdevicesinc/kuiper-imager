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
