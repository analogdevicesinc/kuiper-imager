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

#include <QPushButton>

// A styled QPushButton. Variant and state are carried as dynamic properties
// that the QSS keys on (QPushButton[kuiperButton="true"][kind="..."]); changing
// one re-polishes the widget so the stylesheet re-evaluates.
// See src/gui/theme/theme-*.qss.
class KuiperButton : public QPushButton {
    Q_OBJECT
public:
    explicit KuiperButton(const QString& text, QWidget* parent = nullptr);
    explicit KuiperButton(QWidget* parent = nullptr);

    // "primary" | "secondary" | "tertiary" | "ghost" (default "primary").
    void setKind(const QString& kind);
};
