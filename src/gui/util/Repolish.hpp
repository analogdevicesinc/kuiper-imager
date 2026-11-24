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

#pragma once

#include <QStyle>
#include <QWidget>

namespace kuiper::gui {

// Re-evaluate a widget's stylesheet after a dynamic style property changes — Qt
// does not do this automatically for dynamic properties. See theme-*.qss, which
// keys selectors on those properties (e.g. [status="danger"]).
//
// Descendants are re-polished too: selectors such as
// `QFrame[kuiperBanner="true"][status="danger"] QLabel` style a *child* by its
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
