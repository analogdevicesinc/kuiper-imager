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

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "kuiper/ImageCustomizer.hpp"

class QLabel;
class QLineEdit;
class KuiperCheckBox;
class KuiperBanner;
class KuiperButton;
class KuiperProgressBar;
class KuiperLogView;
class QScrollArea;
class HelperProcess;

// Modal "Customize image" dialog: pick ADI features and install them into a local
// basic .img in place. The install is privileged, so it runs in the elevated
// helper via a HelperProcess the dialog owns. Linux-only. See docs:
// customize-pipeline.
class CustomizeDialog : public QDialog {
    Q_OBJECT
public:
    CustomizeDialog(const QString& imagePath, QWidget* parent = nullptr);

    // Esc / title-bar close: refused while a customize run is in flight (the
    // helper is a child of this dialog, and the image is mid-modification). Use
    // the Cancel button, which asks the helper to stop at a feature boundary.
    void reject() override;

private:
    std::vector<std::string> selectedIds() const;
    void updateEstimate();  // refresh the "~added / final" label on toggles
    void run();
    void setRunning(bool running);
    void onFinished();

    QString imagePath_;
    std::uint64_t imageBytes_ = 0;
    HelperProcess* helper_;

    std::vector<std::pair<std::string, KuiperCheckBox*>> checks_;  // id -> checkbox
    QScrollArea* scroll_;  // the feature list; hidden by the log during a run
    QLabel* estimate_;
    KuiperCheckBox* grow_;    // auto-grow the rootfs to fit (default on)
    QLineEdit* growSize_;  // optional explicit size (e.g. "4G"); overrides auto
    QString scriptPath_;      // host script run as root in the chroot (optional)
    KuiperButton* scriptBtn_;    // picks the script
    QLabel* scriptLabel_;     // shows the chosen script's name, or a hint
    KuiperButton* scriptClear_;  // clears the chosen script
    KuiperBanner* result_;
    KuiperProgressBar* progress_;  // indeterminate activity + coarse phase heading
    KuiperLogView* log_;           // streamed helper/apt output (the real readout)
    KuiperButton* runBtn_;
    KuiperButton* closeBtn_;  // "Close", or "Cancel" while running
};
