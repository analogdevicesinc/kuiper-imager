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

#include <QFrame>

class QVBoxLayout;

// A card: container surface, 1px divider, 8px radius. Content is laid
// out vertically with the design's default padding/gap; add children via the
// layout accessors.
class KuiperCard : public QFrame {
    Q_OBJECT
public:
    explicit KuiperCard(QWidget* parent = nullptr);

    void addWidget(QWidget* w);
    void addLayout(QLayout* l);

private:
    QVBoxLayout* layout_;
};
