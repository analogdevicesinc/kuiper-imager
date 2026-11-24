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

#include "KuiperTag.hpp"

#include "util/Repolish.hpp"

KuiperTag::KuiperTag(const QString& text, const QString& status,
                     QWidget* parent)
    : QLabel(text, parent) {
    setProperty("kuiperTag", true);
    setProperty("status", status);
    // Keep the pill tight to its text rather than stretching in a layout.
    setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
}

void KuiperTag::setStatus(const QString& status) {
    setProperty("status", status);
    kuiper::gui::repolish(this);
}
