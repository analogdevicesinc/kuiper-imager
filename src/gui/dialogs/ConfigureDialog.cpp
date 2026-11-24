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
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
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
    table_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(table_, &QWidget::customContextMenuRequested, this,
            [this] { table_->clearSelection(); });
    connect(table_, &QTableWidget::itemSelectionChanged, this,
            &ConfigureDialog::onSelectionChanged);
    col->addWidget(table_, 1);

    intelWarning_ = new KuiperBanner(
        "warning", "This Intel project also writes a preloader.",
        "A raw preloader image will be written to the card's bootloader "
        "partition in addition to copying the boot files.");
    intelWarning_->setVisible(false);
    col->addWidget(intelWarning_);

    // --- extra local files (optional) ---
    auto* bootFileRow = new QHBoxLayout();
    bootFileRow->setContentsMargins(0, 0, 0, 0);
    bootFileRow->setSpacing(10);
    bootFileBtn_ = new KuiperButton("Local files…");
    bootFileBtn_->setKind("secondary");
    bootFileBtn_->setToolTip(
        "Pick one or more host files to copy into the boot partition "
        "(e.g. device-tree overlays, config files).");
    connect(bootFileBtn_, &KuiperButton::clicked, this, [this] {
        const QStringList files = QFileDialog::getOpenFileNames(
            this, "Choose files to copy into the boot partition");
        if (!files.isEmpty()) {
            bootFiles_ = files;
            QStringList names;
            for (const QString& p : bootFiles_)
                names << QFileInfo(p).fileName();
            bootFileLabel_->setText(names.join(", "));
            bootFileClear_->setVisible(true);
            onSelectionChanged();
        }
    });
    bootFileLabel_ =
        new QLabel("optional — copied into the boot partition", this);
    bootFileLabel_->setProperty("kuiperCaption", true);
    bootFileClear_ = new KuiperButton("✕");
    bootFileClear_->setKind("ghost");
    bootFileClear_->setToolTip("Remove all local files");
    bootFileClear_->setVisible(false);
    connect(bootFileClear_, &KuiperButton::clicked, this, [this] {
        bootFiles_.clear();
        bootFileLabel_->setText("optional — copied into the boot partition");
        bootFileClear_->setVisible(false);
        onSelectionChanged();
    });
    bootFileRow->addWidget(bootFileBtn_);
    bootFileRow->addWidget(bootFileLabel_, 1);
    bootFileRow->addWidget(bootFileClear_);
    col->addLayout(bootFileRow);

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
            for (const auto& f : r->extraFiles)
                lines << QString("• %1 → %2")
                             .arg(QString::fromStdString(f.source),
                                  QString::fromStdString(f.dest));
            const int total =
                static_cast<int>(r->files.size() + r->extraFiles.size());
            result_->setStatus("info");
            result_->setTitle(QString("Would copy %1 file(s)").arg(total));
            result_->setMessage(lines.join("\n"));
        } else {
            result_->setStatus("success");
            result_->setTitle("Configuration applied.");
            QString msg;
            if (!r->files.empty())
                msg +=
                    QString(
                        "Copied %1 file(s) from projects to the boot partition.")
                        .arg(r->files.size());
            if (!r->extraFiles.empty())
                msg +=
                    QString("%1%2 local file(s) copied to the boot partition.")
                        .arg(msg.isEmpty() ? "" : "\n")
                        .arg(r->extraFiles.size());
            if (r->preloader) msg += "\nPreloader written.";
            result_->setMessage(msg);
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
    table_->clearSelection();
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
    const bool canAct = p != nullptr || !bootFiles_.isEmpty();
    previewBtn_->setEnabled(canAct);
    applyBtn_->setEnabled(canAct);
}

void ConfigureDialog::reject() {
    if (task_->busy()) return;
    QDialog::reject();
}

void ConfigureDialog::runConfigure(bool dryRun) {
    const auto* p = selected();
    if ((!p && bootFiles_.isEmpty()) || task_->busy()) return;
    lastWasDryRun_ = dryRun;
    out_->reset();
    result_->setVisible(false);
    setRunning(true);

    const std::string bootPath = bootPath_.toStdString();
    const std::optional<kuiper::Project> project =
        p ? std::optional(*p) : std::nullopt;
    auto sink = sink_;
    auto out = out_;
    std::vector<std::string> extraFiles;
    for (const QString& f : bootFiles_) extraFiles.push_back(f.toStdString());
    task_->run([this, bootPath, project, sink, out, dryRun,
                extraFiles = std::move(extraFiles)](
                   kuiper::ProgressFn onProgress, kuiper::CancelToken) {
        if (project) {
            kuiper::ConfigureOptions opts;
            opts.dryRun = dryRun;
            opts.extraFiles = extraFiles;
            *out = config_.configureProject(bootPath, *project, opts,
                                            std::move(onProgress), sink);
        } else {
            *out = config_.copyLocalFiles(bootPath, extraFiles, dryRun,
                                          std::move(onProgress));
        }
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
    bootFileBtn_->setEnabled(!running);
    bootFileClear_->setEnabled(!running);
    const bool canAct = selected() || !bootFiles_.isEmpty();
    previewBtn_->setEnabled(!running && canAct);
    applyBtn_->setEnabled(!running && canAct);
    closeBtn_->setEnabled(!running);
}
