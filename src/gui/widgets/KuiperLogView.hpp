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

class QPlainTextEdit;

// Append-only, read-only monospace console log for streamed command output (e.g. a
// customize run's apt output). Bounds its own memory (last kMaxBlocks lines), wraps
// rather than scrolling sideways so a long line can't widen the parent, and follows
// the tail only when the user is already at the bottom.
class KuiperLogView : public QWidget {
    Q_OBJECT
public:
    explicit KuiperLogView(QWidget* parent = nullptr);

    void appendLine(const QString& line);
    void clear();

private:
    QPlainTextEdit* edit_;
};
