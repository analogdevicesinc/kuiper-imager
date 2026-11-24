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

#include <QLabel>

// tag: a small status pill. status is one of
// info | success | warning | danger | highlight.
class KuiperTag : public QLabel {
    Q_OBJECT
public:
    explicit KuiperTag(const QString& text, const QString& status = "info",
                    QWidget* parent = nullptr);

    // Re-tint the pill after construction (keyed on the dynamic "status"
    // property, so it repolishes — see theme-*.qss).
    void setStatus(const QString& status);
};
