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

#include <QColor>
#include <QWidget>

class QTimer;

// spinner: an indeterminate rotating arc. size is "sm" (16px) or "md"
// (24px). The arc colour is a styleable property (qproperty-arcColor in the
// generated QSS) so it tracks the active theme like every other widget; it
// defaults to the light interactive-primary token as a fallback.
class KuiperSpinner : public QWidget {
    Q_OBJECT
    Q_PROPERTY(QColor arcColor READ arcColor WRITE setArcColor)
public:
    explicit KuiperSpinner(const QString& size = "sm", QWidget* parent = nullptr);

    QColor arcColor() const { return arcColor_; }
    void setArcColor(const QColor& c) {
        arcColor_ = c;
        update();
    }

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent*) override;
    void showEvent(QShowEvent*) override;
    void hideEvent(QHideEvent*) override;

private:
    int extent_;
    int angle_ = 0;
    QTimer* timer_;
    QColor arcColor_;
};
