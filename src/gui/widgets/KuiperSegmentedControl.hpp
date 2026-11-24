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

#include <QWidget>

class QButtonGroup;
class QHBoxLayout;

// segmented-control: a row of mutually-exclusive options inside a
// bordered frame. Used for small either/or choices (e.g. Local file / Download).
class KuiperSegmentedControl : public QWidget {
    Q_OBJECT
public:
    explicit KuiperSegmentedControl(QWidget* parent = nullptr);

    // Add a segment; `value` is returned by value()/emitted by valueChanged.
    void addSegment(const QString& text, const QString& value);
    QString value() const;
    // Slim the control down (shorter, tighter) for tight spots like the header.
    void setCompact(bool compact);

signals:
    void valueChanged(const QString& value);

private:
    void relayoutPositions();

    QButtonGroup* group_;
    QHBoxLayout* row_;
    bool compact_ = false;
};
