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
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QStatusBar>
#include <QVBoxLayout>

#include <utility>

#include "dialogs/ConfigureDialog.hpp"
#include "dialogs/ConfirmWriteDialog.hpp"
#include "dialogs/ReleasePickerDialog.hpp"
#include "kuiper/Version.hpp"
#include "task/BackgroundTask.hpp"
#include "theme/ThemeManager.hpp"
#include "util/Format.hpp"
#include "widgets/HmcBanner.hpp"
#include "widgets/HmcButton.hpp"
#include "widgets/HmcCard.hpp"
#include "widgets/HmcCheckBox.hpp"
#include "widgets/HmcContainerHeader.hpp"
#include "widgets/HmcProgressBar.hpp"
#include "widgets/HmcSegmentedControl.hpp"
#include "widgets/HmcSpinner.hpp"
#include "widgets/HmcTag.hpp"

namespace {

QString phaseLabel(kuiper::Progress::Phase p) {
    using P = kuiper::Progress::Phase;
    switch (p) {
        case P::Preparing: return "Preparing…";
        case P::Downloading: return "Downloading…";
        case P::Writing: return "Writing…";
        case P::Finalizing: return "Finalizing…";
        case P::Verifying: return "Verifying…";
        case P::Configuring: return "Configuring…";
        case P::Done: return "Done";
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
    l->setProperty("hmcCaption", true);
    return l;
}

}  // namespace

ConsoleWindow::ConsoleWindow(QWidget* parent)
    : QMainWindow(parent),
      task_(new BackgroundTask(this)),
      drivesOut_(std::make_shared<std::optional<kuiper::Result<kuiper::DriveList>>>()),
      probeOut_(std::make_shared<ProbeResult>()),
      releasesOut_(std::make_shared<
                   std::optional<kuiper::Result<std::vector<kuiper::Release>>>>()),
      fetchOut_(std::make_shared<std::optional<kuiper::Result<kuiper::FetchSummary>>>()),
      flashOut_(std::make_shared<std::optional<kuiper::Result<kuiper::FlashSummary>>>()) {
    setWindowTitle("Kuiper Imager");
    setFixedSize(900, 700);

    auto* central = new QWidget(this);
    auto* root = new QVBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // --- header bar ---
    auto* header = new QFrame(central);
    header->setProperty("hmcHeaderBar", true);
    auto* headerRow = new QHBoxLayout(header);
    headerRow->setContentsMargins(24, 16, 24, 16);
    auto* wordmark = new HmcContainerHeader("Kuiper Imager");
    wordmark->setDensity("low");
    headerRow->addWidget(wordmark);
    headerRow->addStretch(1);
    auto* themeToggle = new HmcSegmentedControl(header);
    themeToggle->setCompact(true);
    themeToggle->addSegment("Light", "light");
    themeToggle->addSegment("Dark", "dark");
    connect(themeToggle, &HmcSegmentedControl::valueChanged, this,
            [](const QString& v) {
                kuiper::theme::applyTheme(*qApp, v == "dark"
                                                     ? kuiper::theme::Theme::Dark
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

    verify_ = new HmcCheckBox("Verify after writing");
    verify_->setChecked(true);
    col->addWidget(verify_);

    col->addWidget(buildActionArea());

    result_ = new HmcBanner("info", QString());
    result_->setVisible(false);
    col->addWidget(result_);

    col->addStretch(1);
    root->addWidget(body, 1);
    setCentralWidget(central);

    statusBar()->showMessage(QString("libkuiper %1  ·  %2")
                                 .arg(kuiper::version())
                                 .arg(drives_.backendName()));

    // Persistent progress sink for every operation.
    connect(task_, &BackgroundTask::progress, this, [this](kuiper::Progress p) {
        progress_->setPhase(phaseLabel(p.phase));
        if (p.bytesTotal > 0)
            progress_->setDetail(kuiper::gui::formatBytes(p.bytesDone) + " / " +
                                 kuiper::gui::formatBytes(p.bytesTotal));
        else
            progress_->setDetail(
                QString("%1%").arg(static_cast<int>(p.fraction * 100 + 0.5)));
        progress_->setFraction(p.fraction);
    });

    // Center on screen.
    if (auto* scr = QGuiApplication::primaryScreen()) {
        const QRect g = scr->availableGeometry();
        move(g.center() - frameGeometry().center());
    }

    // Deferred close (see closeEvent): retry once the running job is done.
    // Queued so the job's own finished handler runs first; if it chained a
    // follow-up job (a scan chains a probe), close() cancels that one too.
    connect(task_, &BackgroundTask::finished, this, [this] {
        if (closePending_)
            QMetaObject::invokeMethod(this, [this] { close(); }, Qt::QueuedConnection);
    });

    refreshDrives();
    applyState();
}

QWidget* ConsoleWindow::buildImageCard() {
    auto* card = new HmcCard(this);
    card->addWidget(new HmcContainerHeader("Image", "Choose what to write"));

    source_ = new HmcSegmentedControl(card);
    source_->addSegment("Local file", "local");
    source_->addSegment("Download", "download");
    connect(source_, &HmcSegmentedControl::valueChanged, this,
            [this](const QString&) { applyState(); });
    card->addWidget(source_);

    imageValue_ = valueLabel("No image selected");
    card->addWidget(imageValue_);

    auto* tagLine = new QHBoxLayout();
    tagLine->setContentsMargins(0, 0, 0, 0);
    imageCaption_ = caption("Choose a local image file.");
    imageTag_ = new HmcTag("", "info");
    imageTag_->setVisible(false);
    tagLine->addWidget(imageCaption_);
    tagLine->addWidget(imageTag_);
    tagLine->addStretch(1);
    card->addLayout(tagLine);

    imageBtn_ = new HmcButton("Browse…");
    imageBtn_->setKind("secondary");
    connect(imageBtn_, &HmcButton::clicked, this, [this] {
        if (source_->value() == "download")
            chooseRelease();
        else
            pickImage();
    });
    card->addWidget(imageBtn_);
    return card;
}

QWidget* ConsoleWindow::buildStorageCard() {
    auto* card = new HmcCard(this);
    card->addWidget(new HmcContainerHeader("Storage", "Choose the device to write to"));

    auto* pickRow = new QHBoxLayout();
    pickRow->setContentsMargins(0, 0, 0, 0);
    driveCombo_ = new QComboBox(card);
    driveCombo_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(driveCombo_, &QComboBox::currentIndexChanged, this,
            [this](int) { onDriveChanged(); });
    pickRow->addWidget(driveCombo_, 1);
    rescanBtn_ = new HmcButton("↻");
    rescanBtn_->setKind("ghost");
    rescanBtn_->setToolTip("Rescan drives");
    connect(rescanBtn_, &HmcButton::clicked, this, [this] { refreshDrives(); });
    pickRow->addWidget(rescanBtn_);
    card->addLayout(pickRow);

    auto* tagRow = new QHBoxLayout();
    tagRow->setContentsMargins(0, 0, 0, 0);
    tagRow->setSpacing(8);
    removableTag_ = new HmcTag("Removable", "success");
    systemTag_ = new HmcTag("System", "danger");
    kuiperTag_ = new HmcTag("Kuiper 2", "highlight");
    probeSpinner_ = new HmcSpinner("sm", card);
    probeLabel_ = caption("Checking card…");
    for (auto* w : {static_cast<QWidget*>(removableTag_), static_cast<QWidget*>(systemTag_),
                    static_cast<QWidget*>(kuiperTag_), static_cast<QWidget*>(probeSpinner_),
                    static_cast<QWidget*>(probeLabel_)})
        w->setVisible(false);
    tagRow->addWidget(removableTag_);
    tagRow->addWidget(systemTag_);
    tagRow->addWidget(kuiperTag_);
    tagRow->addWidget(probeSpinner_);
    tagRow->addWidget(probeLabel_);
    tagRow->addStretch(1);
    card->addLayout(tagRow);

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
    writeBtn_ = new HmcButton("Write");
    writeBtn_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(writeBtn_, &HmcButton::clicked, this, [this] { startWrite(); });
    configureBtn_ = new HmcButton("Configure projects →");
    configureBtn_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(configureBtn_, &HmcButton::clicked, this, [this] { openConfigure(); });
    row->addWidget(writeBtn_, 1);
    row->addWidget(configureBtn_, 1);
    col->addWidget(buttonRow_);

    progressRow_ = new QWidget(area);
    auto* prow = new QHBoxLayout(progressRow_);
    prow->setContentsMargins(0, 0, 0, 0);
    prow->setSpacing(12);
    progress_ = new HmcProgressBar(progressRow_);
    prow->addWidget(progress_, 1);
    cancelBtn_ = new HmcButton("Cancel");
    cancelBtn_->setKind("secondary");
    connect(cancelBtn_, &HmcButton::clicked, this, [this] {
        task_->cancel();
        cancelBtn_->setEnabled(false);
    });
    prow->addWidget(cancelBtn_);
    progressRow_->setVisible(false);
    col->addWidget(progressRow_);

    return area;
}

void ConsoleWindow::refreshDrives() {
    if (task_->busy()) return;
    auto out = drivesOut_;
    connect(
        task_, &BackgroundTask::finished, this,
        [this, out] {
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
    if (!d || task_->busy()) {
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
        },
        Qt::SingleShotConnection);

    task_->run([this, drive, out, stale](kuiper::ProgressFn, kuiper::CancelToken) {
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
    const QString path = QFileDialog::getOpenFileName(
        this, "Choose image", QString(),
        "Disk images (*.img *.zip *.xz *.zst *.gz);;All files (*)");
    if (!path.isEmpty()) setLocalImage(path);
}

void ConsoleWindow::setLocalImage(const QString& path) {
    imagePath_ = path;
    QFileInfo fi(path);
    imageLabel_ = fi.fileName();
    imageValue_->setText(fi.fileName());
    imageCaption_->setText(kuiper::gui::formatBytes(fi.size()));
    imageTag_->setText("." + fi.suffix());
    imageTag_->setVisible(!fi.suffix().isEmpty());
    applyState();
}

void ConsoleWindow::chooseRelease() {
    if (task_->busy()) return;
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
            ReleasePickerDialog dlg(*r, this);
            if (dlg.exec() == QDialog::Accepted && !dlg.selectedId().isEmpty())
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
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation) +
        "/images";
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
            setLocalImage(QString::fromStdString(r->outputPath));
            showResult("success", "Downloaded " + label, "Saved to " + dir);
        },
        Qt::SingleShotConnection);

    setBusyUi(true);
    progress_->setIndeterminate(false);
    task_->run([this, fid, outDir, out](kuiper::ProgressFn p,
                                        kuiper::CancelToken c) {
        *out = images_.fetch(fid, outDir, /*force=*/false, std::move(p), c);
    });
}

void ConsoleWindow::startWrite() {
    const kuiper::Drive* d = currentDrive();
    if (imagePath_.isEmpty() || !d || task_->busy()) return;

    const QString target = QString("%1 (%2) · %3")
                               .arg(QString::fromStdString(d->description),
                                    QString::fromStdString(d->node),
                                    kuiper::gui::formatBytes(d->sizeBytes));
    ConfirmWriteDialog dlg(target, imageLabel_, this);
    if (dlg.exec() != QDialog::Accepted) return;

    const std::string node = d->node;
    const std::string img = imagePath_.toStdString();
    const bool verify = verify_->isChecked();
    bootCtx_.reset();  // writing invalidates any mount we hold
    auto out = flashOut_;
    *out = std::nullopt;

    connect(
        task_, &BackgroundTask::finished, this,
        [this, out] {
            setBusyUi(false);
            if (!out->has_value()) return;
            const auto& r = **out;
            if (!r) {
                if (r.error().code != kuiper::ErrorCode::UserCancelled)
                    showResult("danger", "Write failed",
                               QString::fromStdString(r.error().message) + "\n" +
                                   QString::fromStdString(r.error().details));
                else
                    showResult("warning", "Write cancelled",
                               "The card was left non-bootable.");
                return;
            }
            showResult("success", "Write complete",
                       QString("%1 written · sha256 %2")
                           .arg(kuiper::gui::formatBytes(r->bytesWritten),
                                QString::fromStdString(r->sha256.substr(0, 12))));
            probeCurrentDrive();  // re-probe: it may now be a Kuiper 2 card
        },
        Qt::SingleShotConnection);

    setBusyUi(true);
    progress_->setIndeterminate(false);
    task_->run([this, node, img, verify, out](kuiper::ProgressFn p,
                                              kuiper::CancelToken c) {
        kuiper::WriteOptions o;
        o.force = false;
        o.verify = verify;
        *out = drives_.flash(node, img, o, std::move(p), c);
    });
}

void ConsoleWindow::openConfigure() {
    if (!bootCtx_ || bootCtx_->projects.empty() || task_->busy()) return;

    // Composition root: wire the Intel-only raw preloader write to DriveService,
    // keeping ConfigurationService portable. The partition comes from the
    // identified layout, never a composed node string (mirrors the CLI).
#ifdef Q_OS_WIN
    // Also mirrors the CLI: the Windows backend can't yet do the raw preloader
    // write (a base-offset write into a partition of the physical drive). An
    // unwired sink makes configureProject refuse an Intel project up front,
    // before copying anything, instead of failing half-way through.
    kuiper::PreloaderSink sink{};
#else
    const std::optional<std::string> bootloaderNode =
        bootCtx_->layout.bootloader
            ? std::optional<std::string>(bootCtx_->layout.bootloader->node)
            : std::nullopt;
    auto* drives = &drives_;
    kuiper::PreloaderSink sink =
        [drives, bootloaderNode](
            const std::string& preloader,
            kuiper::ProgressFn prog) -> kuiper::Result<void> {
        if (!bootloaderNode)
            return kuiper::Err(kuiper::ErrorCode::NotFound,
                               "No bootloader partition on this card",
                               "This Intel project needs a raw preloader "
                               "partition, which this card doesn't have.");
        kuiper::WriteOptions po;
        po.force = false;
        po.verify = true;
        auto r = drives->writePreloader(*bootloaderNode, preloader, po,
                                        std::move(prog));
        if (!r) return std::unexpected(r.error());
        return {};
    };
#endif

    ConfigureDialog dlg(QString::fromStdString(bootCtx_->boot.path()),
                        bootCtx_->projects, std::move(sink), this);
    dlg.exec();
}

void ConsoleWindow::applyState() {
    const bool busy = task_->busy();
    const bool hasImage = !imagePath_.isEmpty();
    const bool hasDrive = currentDrive() != nullptr;
    const bool kuiper2 = bootCtx_ && bootCtx_->release.has_value();
    const bool hasProjects = bootCtx_ && !bootCtx_->projects.empty();

    // Image card: button label follows source + whether an image is set.
    if (hasImage)
        imageBtn_->setText("Change…");
    else if (source_->value() == "download")
        imageBtn_->setText("Choose release…");
    else
        imageBtn_->setText("Browse…");
    if (!hasImage) {
        imageValue_->setText("No image selected");
        imageCaption_->setText(source_->value() == "download"
                                   ? "Download a stable release."
                                   : "Choose a local image file.");
        imageTag_->setVisible(false);
    }

    configureBtn_->setVisible(kuiper2);

    // Adaptive primary: Write leads when an image is ready; Configure leads when
    // the card is a Kuiper 2 and no image is queued.
    const bool canWrite = hasImage && hasDrive && !busy;
    const bool canConfigure = kuiper2 && hasProjects && !busy && !probing_;
    writeBtn_->setEnabled(canWrite);
    configureBtn_->setEnabled(canConfigure);

    if (hasImage) {
        writeBtn_->setKind("primary");
        configureBtn_->setKind("secondary");
    } else if (kuiper2) {
        configureBtn_->setKind("primary");
        writeBtn_->setKind("secondary");
    } else {
        writeBtn_->setKind("primary");
        configureBtn_->setKind("secondary");
    }
}

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
    source_->setEnabled(!busy);
    imageBtn_->setEnabled(!busy);
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
    result_->setVisible(true);
}

void ConsoleWindow::closeEvent(QCloseEvent* event) {
    if (!task_->busy()) {
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
        if (!task_->busy()) {  // it finished while we were asking
            event->accept();
            return;
        }
    }
    task_->cancel();
    cancelBtn_->setEnabled(false);
}
