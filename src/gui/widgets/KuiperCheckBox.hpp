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

#include <QCheckBox>

// checkbox: a QCheckBox skinned with the selection tokens and a
// resource checkmark.
class KuiperCheckBox : public QCheckBox {
    Q_OBJECT
public:
    explicit KuiperCheckBox(const QString& text, QWidget* parent = nullptr);
};
