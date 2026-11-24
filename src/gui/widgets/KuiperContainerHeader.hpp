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

class QLabel;

// container-header: a heading with an optional subtitle, used at the top
// of cards and screen sections.
class KuiperContainerHeader : public QWidget {
    Q_OBJECT
public:
    explicit KuiperContainerHeader(const QString& heading,
                                const QString& subtitle = QString(),
                                QWidget* parent = nullptr);

    // "low" bumps the heading to 20px (screen titles); default is section size.
    void setDensity(const QString& density);

private:
    QLabel* heading_;
    QLabel* subtitle_;
};
