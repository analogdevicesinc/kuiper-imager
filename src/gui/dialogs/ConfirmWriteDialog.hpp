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

#include <QDialog>
#include <QString>

// A destructive-action confirmation: a danger banner naming exactly what will be
// erased, with Cancel / Write. exec() returns QDialog::Accepted on confirm.
class ConfirmWriteDialog : public QDialog {
    Q_OBJECT
public:
    // `target` is the human summary of the drive (e.g. "KIOXIA EXCERIA
    // (/dev/sdb) · 1.0 TB"); `image` is the image filename.
    ConfirmWriteDialog(const QString& target, const QString& image,
                       QWidget* parent = nullptr);
};
