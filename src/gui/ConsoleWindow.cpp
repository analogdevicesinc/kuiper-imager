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

#include "ConsoleWindow.hpp"

#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QScreen>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>

#include <utility>

#include "dialogs/ConfigureDialog.hpp"
#include "dialogs/ConfirmWriteDialog.hpp"
#include "dialogs/CustomizeDialog.hpp"
#include "dialogs/ReleasePickerDialog.hpp"
#include "kuiper/Version.hpp"
#include "task/BackgroundTask.hpp"
#include "task/HelperProcess.hpp"
#include "theme/ThemeManager.hpp"
#include "util/Format.hpp"
#include "widgets/KuiperBanner.hpp"
#include "widgets/KuiperButton.hpp"
#include "widgets/KuiperCard.hpp"
#include "widgets/KuiperCheckBox.hpp"
#include "widgets/KuiperContainerHeader.hpp"
#include "widgets/KuiperProgressBar.hpp"
#include "widgets/KuiperSegmentedControl.hpp"
#include "widgets/KuiperSpinner.hpp"
#include "widgets/KuiperTag.hpp"

namespace {

// Post-write re-probe budget: up to kWriteReprobeTries attempts spaced
// kWriteReprobeIntervalMs apart (~3s total) while the just-written card's BOOT
// partition settles enough for udisks2 to mount and readRelease to succeed.
constexpr int kWriteReprobeTries = 8;
constexpr int kWriteReprobeIntervalMs = 400;

QString phaseLabel(kuiper::Progress::Phase p) {
    using P = kuiper::Progress::Phase;
    switch (p) {
        case P::Preparing:
            return "Preparing…";
        case P::Downloading:
            return "Downloading…";
        case P::Writing:
            return "Writing…";
        case P::Finalizing:
            return "Finalizing…";
        case P::Verifying:
            return "Verifying…";
        case P::Configuring:
            return "Configuring…";
        case P::Done:
            return "Done";
    }
    return "Working…";
}

QLabel* valueLabel(const QString& text) {
    auto* l = new QLabel(text);
    QFont f = l->font();
    f.setPointSizeF(f.pointSizeF() + 1);
    f.setWeight(QFont::DemiBold);
    l->setFont(f);
    l->setWordWrap(true);
    return l;
}

QLabel* caption(const QString& text) {
    auto* l = new QLabel(text);
    l->setProperty("kuiperCaption", true);
    return l;
}

}  // namespace

ConsoleWindow::ConsoleWindow(QWidget* parent)
    : QMainWindow(parent),
      task_(new BackgroundTask(this)),
      helper_(new HelperProcess(this)),
      drivesOut_(
          std::make_shared<std::optional<kuiper::Result<kuiper::DriveList>>>()),
      probeOut_(std::make_shared<ProbeResult>()),
      releasesOut_(
          std::make_shared<
              std::optional<kuiper::Result<std::vector<kuiper::Release>>>>()),
      fetchOut_(std::make_shared<
                std::optional<kuiper::Result<kuiper::FetchSummary>>>()),
      ejectOut_(std::make_shared<std::optional<kuiper::Result<void>>>()) {
    setWindowTitle("Kuiper Imager");
    // Fixed-width column (the whole layout is tuned to 900px); height is free so
    // optional rows (the result banner, the Configure button, a wrapped error
    // message) can't force the body below its minimum and overlap. The body
    // scrolls if the window is ever dragged shorter than its content.
    setFixedWidth(900);
    setMinimumHeight(560);

    auto* central = new QWidget(this);
    auto* root = new QVBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // --- header bar ---
    auto* header = new QFrame(central);
    header->setProperty("kuiperHeaderBar", true);
    auto* headerRow = new QHBoxLayout(header);
    headerRow->setContentsMargins(24, 16, 24, 16);
    auto* wordmark = new KuiperContainerHeader("Kuiper Imager");
    wordmark->setDensity("low");
    headerRow->addWidget(wordmark);
    headerRow->addStretch(1);
    auto* themeToggle = new KuiperSegmentedControl(header);
    themeToggle->setCompact(true);
    themeToggle->addSegment("Light", "light");
    themeToggle->addSegment("Dark", "dark");
    connect(themeToggle, &KuiperSegmentedControl::valueChanged, this,
            [](const QString& v) {
                kuiper::theme::applyTheme(
                    *qApp, v == "dark" ? kuiper::theme::Theme::Dark
                                       : kuiper::theme::Theme::Light);
            });
    headerRow->addWidget(themeToggle);
    root->addWidget(header);

    // --- body ---
    auto* body = new QWidget(central);
    auto* col = new QVBoxLayout(body);
    col->setContentsMargins(24, 20, 24, 20);
    col->setSpacing(14);
    col->addWidget(buildImageCard());
    col->addWidget(buildStorageCard());

    verify_ = new KuiperCheckBox("Verify after writing");
    verify_->setChecked(true);
    col->addWidget(verify_);

    col->addWidget(buildActionArea());

    result_ = new KuiperBanner("info", QString());
    result_->setVisible(false);
    col->addWidget(result_);

    col->addStretch(1);

    // The header bar stays pinned; only the body scrolls when it outgrows the
    // window. widgetResizable keeps the body stretched to the viewport width so
    // the cards still fill the 900px column.
    auto* scroll = new QScrollArea(central);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(body);
    root->addWidget(scroll, 1);
    setCentralWidget(central);

    resize(900, 780);

    statusBar()->showMessage(QString("libkuiper %1  ·  %2")
                                 .arg(kuiper::version())
                                 .arg(drives_.backendName()));

    // Persistent progress sink, shared by the thread runner and the elevated
    // helper (both emit kuiper::Progress with the same meaning).
    auto onProgress = [this](kuiper::Progress p) {
        progress_->setPhase(phaseLabel(p.phase));
        if (p.bytesTotal > 0)
            progress_->setDetail(kuiper::gui::formatBytes(p.bytesDone) + " / " +
                                 kuiper::gui::formatBytes(p.bytesTotal));
        else
            progress_->setDetail(
                QString("%1%").arg(static_cast<int>(p.fraction * 100 + 0.5)));
        progress_->setFraction(p.fraction);
    };
    connect(task_, &BackgroundTask::progress, this, onProgress);
    connect(helper_, &HelperProcess::progress, this, onProgress);

    // Center on screen.
    if (auto* scr = QGuiApplication::primaryScreen()) {
        const QRect g = scr->availableGeometry();
        move(g.center() - frameGeometry().center());
    }

    // Deferred close (see closeEvent): retry once the running job is done.
    // Queued so the job's own finished handler runs first; if it chained a
    // follow-up job (a scan chains a probe), close() cancels that one too.
    auto retryClose = [this] {
        if (closePending_)
            QMetaObject::invokeMethod(
                this, [this] { close(); }, Qt::QueuedConnection);
    };
    connect(task_, &BackgroundTask::finished, this, retryClose);
    connect(helper_, &HelperProcess::finished, this, retryClose);

    refreshDrives();
    applyState();
}

QWidget* ConsoleWindow::buildImageCard() {
    auto* card = new KuiperCard(this);
    card->addWidget(new KuiperContainerHeader("Image", "Choose what to write"));

    // Name + provenance pill (Local | Downloaded). See docs: gui-console.
    auto* nameLine = new QHBoxLayout();
    nameLine->setContentsMargins(0, 0, 0, 0);
    imageValue_ = valueLabel("No image selected");
    imageTag_ = new KuiperTag("", "info");
    imageTag_->setVisible(false);
    nameLine->addWidget(imageValue_, 1);
    nameLine->addWidget(imageTag_, 0, Qt::AlignVCenter);
    card->addLayout(nameLine);

    imageCaption_ = caption("Choose a local image file or download a release.");
    card->addWidget(imageCaption_);

    // Two explicit sources plus the Linux-only in-place edit, acting on the image.
    auto* actionRow = new QHBoxLayout();
    actionRow->setContentsMargins(0, 0, 0, 0);
    actionRow->setSpacing(12);

    chooseFileBtn_ = new KuiperButton("Choose file…");
    chooseFileBtn_->setKind("secondary");
    chooseFileBtn_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(chooseFileBtn_, &KuiperButton::clicked, this,
            [this] { pickImage(); });
    actionRow->addWidget(chooseFileBtn_, 1);

    downloadBtn_ = new KuiperButton("Download release…");
    downloadBtn_->setKind("secondary");
    downloadBtn_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(downloadBtn_, &KuiperButton::clicked, this,
            [this] { chooseRelease(); });
    actionRow->addWidget(downloadBtn_, 1);

#ifdef Q_OS_LINUX
    // Privileged, Linux-only in-place edit of a local .img; offered only here and
    // gated to a raw local image by applyState(). See docs: customize-pipeline.
    customizeBtn_ = new KuiperButton("Customize…");
    customizeBtn_->setKind("secondary");
    customizeBtn_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    customizeBtn_->setToolTip(
        "Add ADI full-image features (desktop, libraries, apps) to this image");
    connect(customizeBtn_, &KuiperButton::clicked, this,
            [this] { openCustomize(); });
    actionRow->addWidget(customizeBtn_, 1);
#endif
    card->addLayout(actionRow);
    return card;
}

QWidget* ConsoleWindow::buildStorageCard() {
    auto* card = new KuiperCard(this);
    card->addWidget(
        new KuiperContainerHeader("Storage", "Choose the device to write to"));

    auto* pickRow = new QHBoxLayout();
    pickRow->setContentsMargins(0, 0, 0, 0);
    driveCombo_ = new QComboBox(card);
    driveCombo_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(driveCombo_, &QComboBox::currentIndexChanged, this,
            [this](int) { onDriveChanged(); });
    pickRow->addWidget(driveCombo_, 1);
    rescanBtn_ = new KuiperButton("↻");
    rescanBtn_->setKind("ghost");
    rescanBtn_->setToolTip("Rescan drives");
    connect(rescanBtn_, &KuiperButton::clicked, this,
            [this] { refreshDrives(); });
    pickRow->addWidget(rescanBtn_);
    card->addLayout(pickRow);

    auto* tagRow = new QHBoxLayout();
    tagRow->setContentsMargins(0, 0, 0, 0);
    tagRow->setSpacing(8);
    removableTag_ = new KuiperTag("Removable", "success");
    systemTag_ = new KuiperTag("System", "danger");
    kuiperTag_ = new KuiperTag("Kuiper 2", "highlight");
    probeSpinner_ = new KuiperSpinner("sm", card);
    probeLabel_ = caption("Checking card…");
    for (auto* w :
         {static_cast<QWidget*>(removableTag_),
          static_cast<QWidget*>(systemTag_), static_cast<QWidget*>(kuiperTag_),
          static_cast<QWidget*>(probeSpinner_),
          static_cast<QWidget*>(probeLabel_)})
        w->setVisible(false);
    tagRow->addWidget(removableTag_);
    tagRow->addWidget(systemTag_);
    tagRow->addWidget(kuiperTag_);
    tagRow->addWidget(probeSpinner_);
    tagRow->addWidget(probeLabel_);
    tagRow->addStretch(1);
    card->addLayout(tagRow);

    // Configure acts on the card in this drive (it edits projects on the mounted
    // BOOT partition), so it lives with Storage, not beside Write. Shown only
    // once the drive is recognised as a flashed Kuiper 2 (see applyState()).
    configureBtn_ = new KuiperButton("Configure projects →");
    configureBtn_->setKind("secondary");
    configureBtn_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(configureBtn_, &KuiperButton::clicked, this,
            [this] { openConfigure(); });
    card->addWidget(configureBtn_);

    return card;
}

QWidget* ConsoleWindow::buildActionArea() {
    auto* area = new QWidget(this);
    auto* col = new QVBoxLayout(area);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(12);

    buttonRow_ = new QWidget(area);
    auto* row = new QHBoxLayout(buttonRow_);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(12);
    writeBtn_ = new KuiperButton("Write");
    writeBtn_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(writeBtn_, &KuiperButton::clicked, this, [this] { startWrite(); });
    row->addWidget(writeBtn_, 1);
    col->addWidget(buttonRow_);

    progressRow_ = new QWidget(area);
    auto* prow = new QHBoxLayout(progressRow_);
    prow->setContentsMargins(0, 0, 0, 0);
    prow->setSpacing(12);
    progress_ = new KuiperProgressBar(progressRow_);
    prow->addWidget(progress_, 1);
    cancelBtn_ = new KuiperButton("Cancel");
    cancelBtn_->setKind("secondary");
    connect(cancelBtn_, &KuiperButton::clicked, this, [this] {
        if (helper_->busy()) helper_->cancel();
        task_->cancel();
        cancelBtn_->setEnabled(false);
    });
    prow->addWidget(cancelBtn_);
    progressRow_->setVisible(false);
    col->addWidget(progressRow_);

    return area;
}

void ConsoleWindow::refreshDrives(const QString& preferNode) {
    if (busy()) return;
    auto out = drivesOut_;
    connect(
        task_, &BackgroundTask::finished, this,
        [this, out, preferNode] {
            setBusyUi(false);
            if (!out->has_value()) return;
            const auto& r = **out;
            if (!r) {
                showResult("danger", "Could not list drives",
                           QString::fromStdString(r.error().message));
                return;
            }
            driveList_ = *r;
            QSignalBlocker block(driveCombo_);
            driveCombo_->clear();
            for (int i = 0; i < static_cast<int>(driveList_.size()); ++i) {
                const auto& d = driveList_[i];
                driveCombo_->addItem(
                    QString("%1 — %2 — %3")
                        .arg(QString::fromStdString(d.description.empty()
                                                        ? d.node
                                                        : d.description),
                             QString::fromStdString(d.node),
                             kuiper::gui::formatBytes(d.sizeBytes)),
                    i);
            }
            if (driveCombo_->count() == 0)
                driveCombo_->addItem("No drives found", -1);
            if (!preferNode.isEmpty()) {
                for (int i = 0; i < static_cast<int>(driveList_.size()); ++i)
                    if (QString::fromStdString(driveList_[i].node) ==
                        preferNode) {
                        driveCombo_->setCurrentIndex(i);
                        break;
                    }
            }
            block.unblock();
            onDriveChanged();
        },
        Qt::SingleShotConnection);
    setBusyUi(true);
    progress_->setIndeterminate(true);
    progress_->setPhase("Scanning drives…");
    task_->run([this, out](kuiper::ProgressFn, kuiper::CancelToken) {
        *out = drives_.listDrives();
    });
}

const kuiper::Drive* ConsoleWindow::currentDrive() const {
    if (!driveCombo_) return nullptr;
    const int idx = driveCombo_->currentData().toInt();
    if (idx < 0 || idx >= static_cast<int>(driveList_.size())) return nullptr;
    return &driveList_[idx];
}

void ConsoleWindow::onDriveChanged() {
    // probeCurrentDrive() takes over (and releases) the previous card's mount.
    kuiperTag_->setVisible(false);
    probeCurrentDrive();
    applyState();
}

std::shared_ptr<std::optional<ConsoleWindow::BootContext>>
ConsoleWindow::takeBootContext() {
    return std::make_shared<std::optional<BootContext>>(
        std::exchange(bootCtx_, std::nullopt));
}

void ConsoleWindow::probeCurrentDrive() {
    auto stale = takeBootContext();
    const kuiper::Drive* d = currentDrive();
    if (!d || busy()) {
        removableTag_->setVisible(false);
        systemTag_->setVisible(false);
        // Nothing to probe, but still release the old mount off the GUI thread.
        // (When busy, a probe is already in flight and took the context, so
        // `stale` is empty here.)
        if (*stale && !task_->busy())
            task_->run([stale](kuiper::ProgressFn, kuiper::CancelToken) {
                stale->reset();
            });
        return;
    }
    removableTag_->setVisible(d->isRemovable);
    systemTag_->setVisible(d->isSystem);

    const kuiper::Drive drive = *d;
    auto out = probeOut_;
    *out = ProbeResult{};
    probing_ = true;
    probeSpinner_->setVisible(true);
    probeLabel_->setVisible(true);
    applyState();

    connect(
        task_, &BackgroundTask::finished, this,
        [this, out, probedNode = drive.node] {
            probing_ = false;
            probeSpinner_->setVisible(false);
            probeLabel_->setVisible(false);
            // The combo stays live during a probe, and a selection change made
            // meanwhile is skipped as busy. Never attach this card's context to
            // a different selection: park it so the follow-up probe releases it
            // (off-thread), and probe what is selected now.
            const kuiper::Drive* cur = currentDrive();
            if (!cur || cur->node != probedNode) {
                bootCtx_ = std::move(out->ctx);
                probeCurrentDrive();
                applyState();
                return;
            }
            bootCtx_ = std::move(out->ctx);
            const bool kuiper2 = bootCtx_ && bootCtx_->release.has_value();
            if (kuiper2) {
                const QString ver =
                    QString::fromStdString(bootCtx_->release->version);
                kuiperTag_->setText(ver.isEmpty() ? "Kuiper 2"
                                                  : "Kuiper 2 · " + ver);
            }
            kuiperTag_->setVisible(kuiper2);
            applyState();

            // Post-write re-probe: a just-flashed card may need a moment before
            // its BOOT partition mounts and reads clean. Retry until recognised
            // as Kuiper 2 or the budget is spent (then the ↻ button remains).
            if (writeReprobeLeft_ > 0) {
                if (kuiper2 || --writeReprobeLeft_ <= 0) {
                    writeReprobeLeft_ = 0;
                    writeReprobeNode_.clear();
                    showWriteComplete();  // the card has settled; now show it
                } else {
                    const QString node = writeReprobeNode_;
                    QTimer::singleShot(kWriteReprobeIntervalMs, this,
                                       [this, node] { refreshDrives(node); });
                }
            }
        },
        Qt::SingleShotConnection);

    task_->run([this, drive, out, stale](kuiper::ProgressFn,
                                         kuiper::CancelToken) {
        stale->reset();  // unmount the previous card first (may block)
        auto layout = kuiper::identifyLayout(drive);
        if (!layout || !layout->boot) {
            out->note = "Not a flashed Kuiper card.";
            return;
        }
        auto mounted = drives_.mount(*layout->boot);
        if (!mounted) {
            out->note = QString::fromStdString(mounted.error().message);
            return;
        }
        // Read both the marker (the gate) and the projects (for Configure) while
        // the mount handle is still valid — before it is moved into the context.
        const std::string bootPath = mounted->path();
        auto release = config_.readRelease(bootPath);
        auto projects = config_.listProjects(bootPath);
        BootContext ctx{drive, std::move(*layout), std::move(*mounted), {}, {}};
        if (release) ctx.release = std::move(*release);
        if (projects) ctx.projects = std::move(*projects);
        out->ctx = std::move(ctx);
    });
}

void ConsoleWindow::pickImage() {
    if (busy()) return;
    const QString path = QFileDialog::getOpenFileName(
        this, "Choose image", QString(),
        "Disk images (*.img *.zip *.xz *.zst *.gz);;All files (*)");
    if (!path.isEmpty()) setLocalImage(path, ImageOrigin::Local);
}

void ConsoleWindow::setLocalImage(const QString& path, ImageOrigin origin) {
    imagePath_ = path;
    imageOrigin_ = origin;
    QFileInfo fi(path);
    imageLabel_ = fi.fileName();
    imageValue_->setText(fi.fileName());
    imageCaption_->setText(kuiper::gui::formatBytes(fi.size()));
    applyState();
}

void ConsoleWindow::clearImage() {
    imagePath_.clear();
    imageLabel_.clear();
    imageOrigin_ = ImageOrigin::None;
    applyState();
}

QString ConsoleWindow::imageCacheDir() const {
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) +
           "/images";
}

QString ConsoleWindow::cachedImagePath(const QString& id) {
    auto p =
        images_.artifactPath(id.toStdString(), imageCacheDir().toStdString());
    if (!p) return QString();
    const QString path = QString::fromStdString(*p);
    return QFileInfo::exists(path) ? path : QString();
}

void ConsoleWindow::chooseRelease() {
    if (busy()) return;
    auto out = releasesOut_;
    *out = std::nullopt;
    connect(
        task_, &BackgroundTask::finished, this,
        [this, out] {
            setBusyUi(false);
            if (!out->has_value()) return;
            const auto& r = **out;
            if (!r) {
                showResult("danger", "Could not list releases",
                           QString::fromStdString(r.error().message));
                return;
            }
            if (r->empty()) {
                showResult("info", "No releases available", QString());
                return;
            }
            // The picker badges/deletes cached releases; tell it where each lives.
            ReleasePickerDialog dlg(
                *r,
                [this](const kuiper::Release& rel) {
                    return cachedImagePath(QString::fromStdString(rel.id));
                },
                this);
            const int code = dlg.exec();
            // A delete in the picker may have removed our current selection.
            if (!imagePath_.isEmpty() && !QFileInfo::exists(imagePath_))
                clearImage();
            if (code != QDialog::Accepted || dlg.selectedId().isEmpty()) return;

            // Reuse a cached artifact instead of re-fetching.
            const QString cached = cachedImagePath(dlg.selectedId());
            if (!cached.isEmpty()) {
                setLocalImage(cached, ImageOrigin::Downloaded);
                showResult("success", "Using " + dlg.selectedLabel(),
                           "Already downloaded — reused from cache.");
                return;
            }
            startFetch(dlg.selectedId(), dlg.selectedLabel());
        },
        Qt::SingleShotConnection);
    setBusyUi(true);
    progress_->setIndeterminate(true);
    progress_->setPhase("Fetching release list…");
    task_->run([this, out](kuiper::ProgressFn, kuiper::CancelToken) {
        kuiper::ReleaseQuery q;
        q.channel = "stable";
        *out = images_.listReleases(q);
    });
}

void ConsoleWindow::startFetch(const QString& id, const QString& label) {
    const QString dir = imageCacheDir();
    QDir().mkpath(dir);
    const std::string outDir = dir.toStdString();
    const std::string fid = id.toStdString();
    auto out = fetchOut_;
    *out = std::nullopt;

    connect(
        task_, &BackgroundTask::finished, this,
        [this, out, label, dir] {
            setBusyUi(false);
            if (!out->has_value()) return;
            const auto& r = **out;
            if (!r) {
                if (r.error().code != kuiper::ErrorCode::UserCancelled)
                    showResult("danger", "Download failed",
                               QString::fromStdString(r.error().message));
                return;
            }
            setLocalImage(QString::fromStdString(r->outputPath),
                          ImageOrigin::Downloaded);
            showResult("success", "Downloaded " + label, "Saved to " + dir);
        },
        Qt::SingleShotConnection);

    setBusyUi(true);
    progress_->setIndeterminate(false);
    task_->run(
        [this, fid, outDir, out](kuiper::ProgressFn p, kuiper::CancelToken c) {
            *out = images_.fetch(fid, outDir, /*force=*/false, std::move(p), c);
        });
}

void ConsoleWindow::startWrite() {
    const kuiper::Drive* d = currentDrive();
    // Re-check the guard the disabled Write button enforces (never a system disk).
    if (imagePath_.isEmpty() || !d || d->isSystem || busy()) return;

    const QString target = QString("%1 (%2) · %3")
                               .arg(QString::fromStdString(d->description),
                                    QString::fromStdString(d->node),
                                    kuiper::gui::formatBytes(d->sizeBytes));
    ConfirmWriteDialog dlg(target, imageLabel_, this);
    if (dlg.exec() != QDialog::Accepted) return;

    const QString node = QString::fromStdString(d->node);
    const QString deviceLabel =
        QString("%1 (%2)").arg(QString::fromStdString(d->description), node);
    const QString image = imageLabel_;
    const bool verify = verify_->isChecked();

    // The write is the one privileged step: it runs in the elevated helper
    // (pkexec / UAC), not in-process. The result lands in helper_->result().
    const QMetaObject::Connection onHelperDone = connect(
        helper_, &HelperProcess::finished, this,
        [this, node, deviceLabel, image] {
            setBusyUi(false);
            const auto& r = helper_->result();
            if (!r.has_value()) return;
            if (!*r) {
                if (r->error().code != kuiper::ErrorCode::UserCancelled)
                    showResult("danger", "Write failed",
                               QString::fromStdString(r->error().message) +
                                   "\n" +
                                   QString::fromStdString(r->error().details));
                else
                    showResult("warning", "Write cancelled",
                               "The card was left non-bootable.");
                return;
            }
            // Defer the banner past the re-probe below (its busy UI would wipe it
            // the same frame). See docs: gui-console (post-write flow).
            pendingWriteTitle_ = "Write complete";
            pendingWriteMsg_ =
                QString("Wrote %1 to %2.\n%3 written · sha256 %4")
                    .arg(image, deviceLabel,
                         kuiper::gui::formatBytes((*r)->bytesWritten),
                         QString::fromStdString((*r)->sha256.substr(0, 12)));
            pendingEjectNode_ = node;
            // Re-enumerate (driveList_ is stale) and re-probe the written card,
            // retrying briefly until it reads as Kuiper 2.
            writeReprobeNode_ = node;
            writeReprobeLeft_ = kWriteReprobeTries;
            refreshDrives(node);
        },
        Qt::SingleShotConnection);

    setBusyUi(true);
    progress_->setIndeterminate(false);

    // Writing invalidates any mount we hold, and it must be released *before*
    // the write: a handle dropped later would unmount the freshly written
    // partition by node. Releasing can block (udisksctl), so it runs on the
    // worker and the helper launches when it is done — unless Cancel was hit
    // meanwhile, in which case nothing has been written yet.
    auto stale = takeBootContext();
    if (!*stale) {
        helper_->runFlash(node, imagePath_, verify);
        return;
    }
    progress_->setPhase("Preparing…");
    connect(
        task_, &BackgroundTask::finished, this,
        [this, node, verify, onHelperDone] {
            if (task_->cancelRequested() || closePending_) {
                disconnect(onHelperDone);  // no helper will run
                setBusyUi(false);
                showResult("info", "Write cancelled", "Nothing was written.");
                return;
            }
            helper_->runFlash(node, imagePath_, verify);
        },
        Qt::SingleShotConnection);
    task_->run(
        [stale](kuiper::ProgressFn, kuiper::CancelToken) { stale->reset(); });
}

void ConsoleWindow::showWriteComplete() {
    if (pendingWriteTitle_.isEmpty()) return;
    showResult("success", pendingWriteTitle_, pendingWriteMsg_);
    pendingWriteTitle_.clear();
    pendingWriteMsg_.clear();
#if defined(Q_OS_LINUX) || defined(Q_OS_WIN)
    // Offer Safe eject where the backend supports it (not macOS); the card is
    // left mounted so Configure keeps working. See docs: gui-console.
    if (!pendingEjectNode_.isEmpty()) {
        const QString node = pendingEjectNode_;
        result_->setAction("Safe eject", [this, node] { startEject(node); });
    }
#endif
    pendingEjectNode_.clear();
}

void ConsoleWindow::startEject(const QString& node) {
    if (busy()) return;
    // Release our BOOT mount first (off-thread — it can block), then eject.
    auto stale = takeBootContext();
    auto out = ejectOut_;
    *out = std::nullopt;
    connect(
        task_, &BackgroundTask::finished, this,
        [this, out, node] {
            setBusyUi(false);
            if (out->has_value() && !**out) {
                showResult("warning", "Couldn't eject the card",
                           QString::fromStdString((*out)->error().message));
                // Let the user retry once whatever held it is released.
                result_->setAction("Safe eject",
                                   [this, node] { startEject(node); });
                return;
            }
            // Ejected: applyState drops the now-stale Kuiper 2 context/Configure.
            showResult("success", "Safe to remove",
                       "The card has been ejected — you can remove it now.");
            result_->clearAction();
            applyState();
        },
        Qt::SingleShotConnection);
    setBusyUi(true);
    progress_->setIndeterminate(true);
    progress_->setPhase("Ejecting…");
    task_->run(
        [this, stale, node, out](kuiper::ProgressFn, kuiper::CancelToken) {
            stale->reset();
            *out = drives_.eject(node.toStdString());
        });
}

void ConsoleWindow::openConfigure() {
    if (!bootCtx_ || bootCtx_->projects.empty() || busy()) return;

        // Composition root: wire the Intel-only raw preloader write, keeping
        // ConfigurationService portable. The partition comes from the identified
        // layout, never a composed node string (mirrors the CLI).
#ifdef Q_OS_WIN
    // Also mirrors the CLI: the Windows backend can't yet do the raw preloader
    // write (a base-offset write into a partition of the physical drive). An
    // unwired sink makes configureProject refuse an Intel project up front,
    // before copying anything, instead of failing half-way through.
    kuiper::PreloaderSink sink{};
#else
    // The raw write itself runs in the elevated helper, like the flash: the
    // unprivileged GUI cannot open the partition.
    kuiper::PreloaderSink sink;
    if (bootCtx_->layout.bootloader) {
        sink =
            makeElevatedPreloaderSink(this, bootCtx_->layout.bootloader->node);
    } else {
        sink = [](const std::string&,
                  kuiper::ProgressFn) -> kuiper::Result<void> {
            return kuiper::Err(kuiper::ErrorCode::NotFound,
                               "No bootloader partition on this card",
                               "This Intel project needs a raw preloader "
                               "partition, which this card doesn't have.");
        };
    }
#endif

    ConfigureDialog dlg(QString::fromStdString(bootCtx_->boot.path()),
                        bootCtx_->projects, std::move(sink), this);
    dlg.exec();
}

void ConsoleWindow::openCustomize() {
    if (imagePath_.isEmpty() || busy()) return;
    CustomizeDialog dlg(imagePath_, this);
    dlg.exec();
    // Refresh the caption (the image may have grown), keeping its provenance.
    if (!imagePath_.isEmpty()) setLocalImage(imagePath_, imageOrigin_);
}

void ConsoleWindow::applyState() {
    const bool busy = this->busy();
    const bool hasImage = !imagePath_.isEmpty();
    const kuiper::Drive* drive = currentDrive();
    const bool hasDrive = drive != nullptr;
    const bool isSystem = drive && drive->isSystem;
    const bool kuiper2 = bootCtx_ && bootCtx_->release.has_value();
    const bool hasProjects = bootCtx_ && !bootCtx_->projects.empty();

    // Image card: a selection shows its provenance pill; none reverts to the hint.
    if (hasImage) {
        const bool downloaded = imageOrigin_ == ImageOrigin::Downloaded;
        imageTag_->setText(downloaded ? "Downloaded" : "Local");
        imageTag_->setStatus(downloaded ? "info" : "highlight");
        imageTag_->setVisible(true);
    } else {
        imageValue_->setText("No image selected");
        imageCaption_->setText(
            "Choose a local image file or download a release.");
        imageTag_->setVisible(false);
    }

    // Customize applies only to a raw local .img (not a compressed download), and
    // only on Linux (the button exists only there).
    if (customizeBtn_) {
        const bool rawLocalImg =
            hasImage && imageOrigin_ == ImageOrigin::Local &&
            QFileInfo(imagePath_)
                    .suffix()
                    .compare("img", Qt::CaseInsensitive) == 0;
        customizeBtn_->setEnabled(rawLocalImg && !busy);
    }

    // Write is the one commit action; Configure appears for a flashed Kuiper 2.
    // Never Write to a system disk (the red "System" tag says why).
    configureBtn_->setVisible(kuiper2);
    writeBtn_->setEnabled(hasImage && hasDrive && !isSystem && !busy);
    configureBtn_->setEnabled(kuiper2 && hasProjects && !busy && !probing_);
}

bool ConsoleWindow::busy() const { return task_->busy() || helper_->busy(); }

void ConsoleWindow::setBusyUi(bool busy) {
    buttonRow_->setVisible(!busy);
    progressRow_->setVisible(busy);
    if (busy) {
        result_->setVisible(false);
        cancelBtn_->setEnabled(true);
        progress_->setStatus("info");
        progress_->setFraction(0);
        progress_->setDetail(QString());
    }
    chooseFileBtn_->setEnabled(!busy);
    downloadBtn_->setEnabled(!busy);
    if (customizeBtn_) customizeBtn_->setEnabled(!busy);
    driveCombo_->setEnabled(!busy);
    rescanBtn_->setEnabled(!busy);
    verify_->setEnabled(!busy);
    if (!busy) applyState();
}

void ConsoleWindow::showResult(const QString& status, const QString& title,
                               const QString& message) {
    result_->setStatus(status);
    result_->setTitle(title);
    result_->setMessage(message);
    result_->clearAction();  // a fresh message drops any prior action button
    result_->setVisible(true);
}

void ConsoleWindow::closeEvent(QCloseEvent* event) {
    if (!busy()) {
        event->accept();
        return;
    }
    event->ignore();
    if (!closePending_) {
        // Ask only about operations the user can see (download, write, …); a
        // background scan or probe just finishes quietly.
        if (progressRow_->isVisible() &&
            QMessageBox::question(this, "Quit Kuiper Imager?",
                                  "An operation is still running. Cancel it "
                                  "and quit?") != QMessageBox::Yes)
            return;
        closePending_ = true;
        if (!busy()) {  // it finished while we were asking
            event->accept();
            return;
        }
    }
    if (helper_->busy()) helper_->cancel();
    task_->cancel();
    cancelBtn_->setEnabled(false);
}
