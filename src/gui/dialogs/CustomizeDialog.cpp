#include "CustomizeDialog.hpp"

#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QWidget>

#include <optional>

#include "task/HelperProcess.hpp"
#include "util/Format.hpp"
#include "widgets/HmcBanner.hpp"
#include "widgets/HmcButton.hpp"
#include "widgets/HmcCheckBox.hpp"
#include "widgets/HmcContainerHeader.hpp"
#include "widgets/HmcLogView.hpp"
#include "widgets/HmcProgressBar.hpp"

namespace {

constexpr std::uint64_t kMiB = 1024ull * 1024ull;

// Coarse heading shown above the log during a run (apt has no honest fraction).
QString phaseHeading(kuiper::Progress::Phase p) {
    using P = kuiper::Progress::Phase;
    switch (p) {
        case P::Preparing: return "Preparing…";
        case P::Downloading: return "Downloading…";
        case P::Writing: return "Writing…";
        case P::Finalizing: return "Finalizing…";
        case P::Verifying: return "Verifying…";
        case P::Configuring: return "Installing features…";
        case P::Done: return "Done";
    }
    return "Working…";
}

// Size parsing and the auto-grow preview come from kuiper::customize so the GUI
// can't drift from the CLI/helper grammar or the executor's real grow.
std::optional<std::uint64_t> parseGrow(const QString& text) {
    return kuiper::customize::parseSize(text.trimmed().toStdString());
}

QLabel* groupHeading(const QString& text) {
    auto* l = new QLabel(text);
    QFont f = l->font();
    f.setWeight(QFont::DemiBold);
    l->setFont(f);
    l->setProperty("hmcCaption", true);
    return l;
}

}  // namespace

CustomizeDialog::CustomizeDialog(const QString& imagePath, QWidget* parent)
    : QDialog(parent),
      imagePath_(imagePath),
      imageBytes_(static_cast<std::uint64_t>(QFileInfo(imagePath).size())),
      helper_(new HelperProcess(this)) {
    setWindowTitle("Customize image");
    setModal(true);
    resize(640, 640);
    // Cap the width so a long filename or log line can't widen the dialog.
    setMaximumWidth(720);

    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(24, 24, 24, 24);
    col->setSpacing(16);

    col->addWidget(new HmcContainerHeader(
        "Customize image",
        "Add ADI full-image features to " + QFileInfo(imagePath_).fileName()));

    // --- feature list, grouped, in a scroll area ---
    auto* listHost = new QWidget(this);
    auto* listCol = new QVBoxLayout(listHost);
    listCol->setContentsMargins(4, 4, 4, 4);
    listCol->setSpacing(8);

    std::string currentGroup;
    for (const auto& f : kuiper::featureCatalog()) {
        if (f.group != currentGroup) {
            currentGroup = f.group;
            listCol->addWidget(groupHeading(QString::fromStdString(f.group)));
        }
        QString label = QString::fromStdString(f.label);
        if (f.sizeEstimateMB > 0)
            label += QString("  (~%1)").arg(
                kuiper::gui::formatBytes(static_cast<std::uint64_t>(f.sizeEstimateMB) *
                                         kMiB));
        auto* cb = new HmcCheckBox(label, listHost);
        if (!f.description.empty())
            cb->setToolTip(QString::fromStdString(f.description));
        connect(cb, &HmcCheckBox::toggled, this, [this] { updateEstimate(); });
        listCol->addWidget(cb);
        checks_.emplace_back(f.id, cb);
    }
    listCol->addStretch(1);

    scroll_ = new QScrollArea(this);
    scroll_->setWidgetResizable(true);
    scroll_->setWidget(listHost);
    col->addWidget(scroll_, 1);

    // Takes the feature list's place during a run (see setRunning): the streamed
    // helper/apt output, which is the honest readout of an apt-driven install.
    log_ = new HmcLogView(this);
    log_->setVisible(false);
    col->addWidget(log_, 1);

    // --- grow controls ---
    auto* growRow = new QHBoxLayout();
    growRow->setContentsMargins(0, 0, 0, 0);
    growRow->setSpacing(10);
    grow_ = new HmcCheckBox("Grow rootfs to fit", this);
    grow_->setChecked(true);
    grow_->setToolTip(
        "Enlarge the image's rootfs before installing. Off = install without "
        "growing (may fail if the rootfs is full).");
    growSize_ = new QLineEdit(this);
    growSize_->setPlaceholderText("auto (e.g. 4G)");
    growSize_->setMaximumWidth(140);
    connect(grow_, &HmcCheckBox::toggled, this, [this](bool on) {
        growSize_->setEnabled(on);
        updateEstimate();
    });
    connect(growSize_, &QLineEdit::textChanged, this, [this] { updateEstimate(); });
    growRow->addWidget(grow_);
    growRow->addWidget(growSize_);
    growRow->addStretch(1);
    col->addLayout(growRow);

    // --- custom script (optional) ---
    // A host script run as root in the chroot after the features; usable on its
    // own (satisfies "something to do" with no features). See docs.
    auto* scriptRow = new QHBoxLayout();
    scriptRow->setContentsMargins(0, 0, 0, 0);
    scriptRow->setSpacing(10);
    scriptBtn_ = new HmcButton("Custom script…");
    scriptBtn_->setKind("secondary");
    scriptBtn_->setToolTip(
        "Pick a host script to run as root inside the image (like apt, it can "
        "install packages). It runs after the selected features.");
    connect(scriptBtn_, &HmcButton::clicked, this, [this] {
        const QString p = QFileDialog::getOpenFileName(
            this, "Choose a script to run in the image", QString(),
            "Scripts (*.sh *.bash);;All files (*)");
        if (!p.isEmpty()) {
            scriptPath_ = p;
            updateEstimate();
        }
    });
    scriptLabel_ = new QLabel(this);
    scriptLabel_->setProperty("hmcCaption", true);
    scriptClear_ = new HmcButton("✕");
    scriptClear_->setKind("ghost");
    scriptClear_->setToolTip("Remove the custom script");
    connect(scriptClear_, &HmcButton::clicked, this, [this] {
        scriptPath_.clear();
        updateEstimate();
    });
    scriptRow->addWidget(scriptBtn_);
    scriptRow->addWidget(scriptLabel_, 1);
    scriptRow->addWidget(scriptClear_);
    col->addLayout(scriptRow);

    estimate_ = new QLabel(this);
    estimate_->setProperty("hmcCaption", true);
    col->addWidget(estimate_);

    result_ = new HmcBanner("info", QString());
    result_->setVisible(false);
    col->addWidget(result_);

    progress_ = new HmcProgressBar(this);
    progress_->setVisible(false);
    col->addWidget(progress_);

    connect(helper_, &HelperProcess::progress, this, [this](kuiper::Progress p) {
        // The bar stays indeterminate (apt has no honest fraction); the heading
        // gives the coarse phase, and every message line flows into the log.
        progress_->setPhase(phaseHeading(p.phase));
        if (!p.message.empty()) log_->appendLine(QString::fromStdString(p.message));
    });
    connect(helper_, &HelperProcess::finished, this, [this] { onFinished(); });

    // --- buttons ---
    auto* buttons = new QHBoxLayout();
    closeBtn_ = new HmcButton("Close");
    closeBtn_->setKind("ghost");
    connect(closeBtn_, &HmcButton::clicked, this, [this] {
        if (helper_->busy()) {
            helper_->cancel();
            closeBtn_->setEnabled(false);
        } else {
            reject();
        }
    });
    buttons->addWidget(closeBtn_);
    buttons->addStretch(1);
    runBtn_ = new HmcButton("Customize");
    connect(runBtn_, &HmcButton::clicked, this, [this] { run(); });
    buttons->addWidget(runBtn_);
    col->addLayout(buttons);

    updateEstimate();
}

std::vector<std::string> CustomizeDialog::selectedIds() const {
    std::vector<std::string> ids;
    for (const auto& [id, cb] : checks_)
        if (cb->isChecked()) ids.push_back(id);
    return ids;
}

void CustomizeDialog::updateEstimate() {
    std::uint64_t sumMB = 0;
    int selected = 0;
    for (const auto& f : kuiper::featureCatalog())
        for (const auto& [id, cb] : checks_)
            if (id == f.id && cb->isChecked()) {
                sumMB += static_cast<std::uint64_t>(f.sizeEstimateMB);
                ++selected;
            }

    const bool hasScript = !scriptPath_.isEmpty();
    runBtn_->setEnabled((selected > 0 || hasScript) && !helper_->busy());

    // Reflect the chosen script (or the hint) in its row.
    scriptLabel_->setText(hasScript
                              ? QFileInfo(scriptPath_).fileName() + " (runs as root)"
                              : "optional — runs as root inside the image");
    scriptClear_->setVisible(hasScript);

    if (selected == 0) {
        estimate_->setText(hasScript
                               ? "Custom script only — no catalog features selected."
                               : "Select one or more features, or choose a script.");
        return;
    }
    std::uint64_t grow = 0;
    if (grow_->isChecked()) {
        if (auto explicitSize = parseGrow(growSize_->text()))
            grow = *explicitSize;
        else
            grow = kuiper::customize::autoGrowBytes(sumMB);
    }
    const QString addLabel =
        kuiper::gui::formatBytes(sumMB * kMiB) + " of features selected";
    if (grow > 0)
        estimate_->setText(addLabel + "  ·  grow by ~" +
                           kuiper::gui::formatBytes(grow) + "  ·  final ~" +
                           kuiper::gui::formatBytes(imageBytes_ + grow));
    else
        estimate_->setText(addLabel + "  ·  no grow (install into existing free "
                                      "space)");
}

void CustomizeDialog::run() {
    const auto ids = selectedIds();
    if ((ids.empty() && scriptPath_.isEmpty()) || helper_->busy()) return;
    result_->setVisible(false);
    // Swap the feature list for the output log and keep it up for the rest of the
    // dialog's life, so the run's output stays readable after it finishes.
    log_->clear();
    scroll_->setVisible(false);
    log_->setVisible(true);
    setRunning(true);

    QStringList featureIds;
    for (const auto& id : ids) featureIds << QString::fromStdString(id);

    const bool noGrow = !grow_->isChecked();
    std::optional<quint64> growBytes;
    if (!noGrow) {
        if (auto explicitSize = parseGrow(growSize_->text())) growBytes = *explicitSize;
        // else leave nullopt => the executor auto-sizes
    }
    helper_->runCustomize(imagePath_, featureIds, growBytes, noGrow, QString(),
                          scriptPath_);
}

void CustomizeDialog::onFinished() {
    setRunning(false);
    const auto& r = helper_->customizeResult();
    if (!r.has_value()) return;
    if (!*r) {
        if (r->error().code == kuiper::ErrorCode::UserCancelled) {
            result_->setStatus("warning");
            result_->setTitle("Customize cancelled");
            result_->setMessage(QString::fromStdString(r->error().details));
        } else {
            result_->setStatus("danger");
            result_->setTitle(QString::fromStdString(r->error().message));
            QString msg = QString::fromStdString(r->error().details);
            if (r->error().recoverySuggestion)
                msg += (msg.isEmpty() ? "" : "\n") +
                       QString::fromStdString(*r->error().recoverySuggestion);
            result_->setMessage(msg);
        }
        result_->setVisible(true);
        return;
    }
    QStringList names;
    for (const auto& id : (*r)->installed) names << QString::fromStdString(id);
    const QString script = QString::fromStdString((*r)->ranScript);
    result_->setStatus("success");
    if (names.isEmpty() && !script.isEmpty())
        result_->setTitle("Ran " + script);
    else if (!script.isEmpty())
        result_->setTitle(QString("Installed %1 feature(s) · ran %2")
                              .arg(names.size())
                              .arg(script));
    else
        result_->setTitle(QString("Installed %1 feature(s)").arg(names.size()));
    QString detail = names.isEmpty() ? script : names.join(", ");
    result_->setMessage(
        QString("%1\nArchitecture: %2  ·  Image now %3")
            .arg(detail,
                 QString::fromStdString((*r)->detectedArch),
                 kuiper::gui::formatBytes((*r)->finalImageBytes)));
    result_->setVisible(true);
    closeBtn_->setText("Done");
    runBtn_->setEnabled(false);  // the image has been modified; re-run is a new op
}

void CustomizeDialog::setRunning(bool running) {
    progress_->setVisible(running);
    if (running) {
        progress_->setIndeterminate(true);
        progress_->setStatus("info");
        progress_->setPhase("Preparing…");
        progress_->setDetail(QString());
    }
    for (const auto& [id, cb] : checks_) cb->setEnabled(!running);
    grow_->setEnabled(!running);
    growSize_->setEnabled(!running && grow_->isChecked());
    scriptBtn_->setEnabled(!running);
    scriptClear_->setEnabled(!running);
    runBtn_->setEnabled(!running &&
                        !(selectedIds().empty() && scriptPath_.isEmpty()));
    closeBtn_->setText(running ? "Cancel" : "Close");
    closeBtn_->setEnabled(true);
}

void CustomizeDialog::reject() {
    if (helper_->busy()) return;  // use Cancel; never tear down mid-modification
    QDialog::reject();
}
