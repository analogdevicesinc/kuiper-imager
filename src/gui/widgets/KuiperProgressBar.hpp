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
class QProgressBar;

// progress-bar: a labelled 8px bar with a trailing detail (percentage or
// byte readout). status drives the fill colour: info | success | danger.
class KuiperProgressBar : public QWidget {
    Q_OBJECT
public:
    explicit KuiperProgressBar(QWidget* parent = nullptr);

    void setPhase(const QString& text);   // left label, e.g. "Writing…"
    void setDetail(const QString& text);  // right label, e.g. "5.9 / 8.0 GB"
    void setFraction(double f);           // [0,1]
    void setStatus(const QString& status);
    void setIndeterminate(bool on);

private:
    QLabel* phase_;
    QLabel* detail_;
    QProgressBar* bar_;
};
