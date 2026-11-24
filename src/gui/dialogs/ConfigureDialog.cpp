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

#include "ConfigureDialog.hpp"

#include <QAbstractItemView>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include "task/BackgroundTask.hpp"
#include "widgets/KuiperBanner.hpp"
#include "widgets/KuiperButton.hpp"
#include "widgets/KuiperContainerHeader.hpp"
#include "widgets/KuiperProgressBar.hpp"
#include "widgets/KuiperSegmentedControl.hpp"

ConfigureDialog::ConfigureDialog(const QString& bootPath,
                                 std::vector<kuiper::Project> projects,
                                 kuiper::PreloaderSink preloaderSink,
                                 QWidget* parent)
    : QDialog(parent),
      bootPath_(bootPath),
      projects_(std::move(projects)),
      sink_(std::move(preloaderSink)),
      task_(new BackgroundTask(this)),
      out_(std::make_shared<
           std::optional<kuiper::Result<kuiper::ConfigureSummary>>>()) {
    setWindowTitle("Configure card");
    setModal(true);
    resize(620, 560);

    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(24, 24, 24, 24);
    col->setSpacing(16);

    col->addWidget(new KuiperContainerHeader(
        "Configure card", "Select the project for your target hardware."));

    filter_ = new KuiperSegmentedControl(this);
    filter_->addSegment("All", "all");
    filter_->addSegment("Xilinx", "xilinx");
    filter_->addSegment("Intel", "intel");
    filter_->addSegment("RPi", "rpi");
    connect(filter_, &KuiperSegmentedControl::valueChanged, this,
            [this](const QString& v) {
                platformFilter_ = v;
                rebuildTable();
            });
    col->addWidget(filter_);

    table_ = new QTableWidget(this);
    table_->setColumnCount(4);
    table_->setHorizontalHeaderLabels(
        {"Project", "Architecture", "Board", "Platform"});
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->verticalHeader()->setVisible(false);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(table_, &QTableWidget::itemSelectionChanged, this,
            &ConfigureDialog::onSelectionChanged);
    col->addWidget(table_, 1);

    intelWarning_ = new KuiperBanner(
        "warning", "This Intel project also writes a preloader.",
        "A raw preloader image will be written to the card's bootloader "
        "partition in addition to copying the boot files.");
    intelWarning_->setVisible(false);
    col->addWidget(intelWarning_);

    result_ = new KuiperBanner("info", QString());
    result_->setVisible(false);
    col->addWidget(result_);

    progress_ = new KuiperProgressBar(this);
    progress_->setVisible(false);
    col->addWidget(progress_);

    connect(task_, &BackgroundTask::progress, this, [this](kuiper::Progress p) {
        progress_->setPhase(QString::fromStdString(
            p.message.empty() ? "Configuring…" : p.message));
        if (p.bytesTotal > 0)
            progress_->setDetail(
                QString("%1%").arg(static_cast<int>(p.fraction * 100 + 0.5)));
        progress_->setFraction(p.fraction);
    });
    connect(task_, &BackgroundTask::finished, this, [this] {
        setRunning(false);
        if (!out_->has_value()) return;
        const auto& r = **out_;
        if (!r) {
            result_->setStatus("danger");
            result_->setTitle(QString::fromStdString(r.error().message));
            result_->setMessage(QString::fromStdString(r.error().details));
            result_->setVisible(true);
            return;
        }
        if (lastWasDryRun_) {
            QStringList lines;
            for (const auto& f : r->files)
                lines << QString("• %1 → %2")
                             .arg(QString::fromStdString(f.source),
                                  QString::fromStdString(f.dest));
            result_->setStatus("info");
            result_->setTitle(
                QString("Would copy %1 file(s)").arg(r->files.size()));
            result_->setMessage(lines.join("\n"));
        } else {
            result_->setStatus("success");
            result_->setTitle("Configuration applied.");
            result_->setMessage(
                QString("Copied %1 file(s) to the boot partition.%2")
                    .arg(r->files.size())
                    .arg(r->preloader ? "\nPreloader written." : ""));
            closeBtn_->setText("Done");
        }
        result_->setVisible(true);
    });

    auto* buttons = new QHBoxLayout();
    closeBtn_ = new KuiperButton("Cancel");
    closeBtn_->setKind("ghost");
    connect(closeBtn_, &KuiperButton::clicked, this, &QDialog::reject);
    buttons->addWidget(closeBtn_);
    buttons->addStretch(1);
    previewBtn_ = new KuiperButton("Preview changes");
    previewBtn_->setKind("secondary");
    connect(previewBtn_, &KuiperButton::clicked, this,
            [this] { runConfigure(true); });
    buttons->addWidget(previewBtn_);
    applyBtn_ = new KuiperButton("Apply");
    connect(applyBtn_, &KuiperButton::clicked, this,
            [this] { runConfigure(false); });
    buttons->addWidget(applyBtn_);
    col->addLayout(buttons);

    rebuildTable();
}

void ConfigureDialog::rebuildTable() {
    table_->setRowCount(0);
    int row = 0;
    for (int i = 0; i < static_cast<int>(projects_.size()); ++i) {
        const auto& p = projects_[i];
        if (platformFilter_ != "all" &&
            p.platform != platformFilter_.toStdString())
            continue;
        table_->insertRow(row);
        auto* name = new QTableWidgetItem(QString::fromStdString(p.name));
        name->setData(Qt::UserRole, i);  // index back into projects_
        table_->setItem(row, 0, name);
        table_->setItem(
            row, 1,
            new QTableWidgetItem(QString::fromStdString(p.architecture)));
        table_->setItem(row, 2,
                        new QTableWidgetItem(QString::fromStdString(p.board)));
        table_->setItem(
            row, 3, new QTableWidgetItem(QString::fromStdString(p.platform)));
        ++row;
    }
    if (table_->rowCount() > 0) table_->selectRow(0);
    onSelectionChanged();
}

const kuiper::Project* ConfigureDialog::selected() const {
    const int row = table_->currentRow();
    if (row < 0) return nullptr;
    auto* item = table_->item(row, 0);
    if (!item) return nullptr;
    const int idx = item->data(Qt::UserRole).toInt();
    if (idx < 0 || idx >= static_cast<int>(projects_.size())) return nullptr;
    return &projects_[idx];
}

void ConfigureDialog::onSelectionChanged() {
    const auto* p = selected();
    const bool intel = p && !p->preloader.empty();
    intelWarning_->setVisible(intel);
    const bool hasSel = p != nullptr;
    previewBtn_->setEnabled(hasSel);
    applyBtn_->setEnabled(hasSel);
}

void ConfigureDialog::reject() {
    if (task_->busy()) return;
    QDialog::reject();
}

void ConfigureDialog::runConfigure(bool dryRun) {
    const auto* p = selected();
    if (!p || task_->busy()) return;
    lastWasDryRun_ = dryRun;
    out_->reset();
    result_->setVisible(false);
    setRunning(true);

    const std::string bootPath = bootPath_.toStdString();
    const kuiper::Project project = *p;
    auto sink = sink_;
    auto out = out_;
    task_->run([this, bootPath, project, sink, out, dryRun](
                   kuiper::ProgressFn onProgress, kuiper::CancelToken) {
        kuiper::ConfigureOptions opts;
        opts.dryRun = dryRun;
        *out = config_.configureProject(bootPath, project, opts,
                                        std::move(onProgress), sink);
    });
}

void ConfigureDialog::setRunning(bool running) {
    progress_->setVisible(running);
    if (running) {
        progress_->setFraction(0);
        progress_->setPhase("Configuring…");
        progress_->setDetail(QString());
    }
    filter_->setEnabled(!running);
    table_->setEnabled(!running);
    previewBtn_->setEnabled(!running && selected());
    applyBtn_->setEnabled(!running && selected());
    closeBtn_->setEnabled(!running);
}
