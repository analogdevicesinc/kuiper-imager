#pragma once

#include <QDialog>
#include <QString>
#include <memory>
#include <optional>
#include <vector>

#include "kuiper/ConfigurationService.hpp"
#include "kuiper/Project.hpp"

class QTableWidget;
class HmcSegmentedControl;
class HmcBanner;
class HmcButton;
class HmcProgressBar;
class BackgroundTask;

// Modal project picker + applier for a flashed Kuiper 2 card. Given the mounted
// BOOT path, the projects already read off it, and a pre-wired PreloaderSink
// (Intel-only device write, built by the window as the composition root), it can
// preview the copy plan (dry-run) and apply a project's boot files.
class ConfigureDialog : public QDialog {
    Q_OBJECT
public:
    ConfigureDialog(const QString& bootPath,
                    std::vector<kuiper::Project> projects,
                    kuiper::PreloaderSink preloaderSink,
                    QWidget* parent = nullptr);

    // Esc and the title-bar close both route here. Refused while a configure
    // job runs: the worker holds `this`, so the dialog must outlive it, and
    // stopping half-way would leave the card partially configured anyway.
    void reject() override;

private:
    void rebuildTable();                 // apply the current platform filter
    const kuiper::Project* selected() const;
    void onSelectionChanged();
    void runConfigure(bool dryRun);
    void setRunning(bool running);

    QString bootPath_;
    std::vector<kuiper::Project> projects_;
    kuiper::PreloaderSink sink_;
    kuiper::ConfigurationService config_;
    BackgroundTask* task_;

    QString platformFilter_ = "all";
    HmcSegmentedControl* filter_;
    QTableWidget* table_;
    HmcBanner* intelWarning_;
    HmcBanner* result_;
    HmcProgressBar* progress_;
    HmcButton* previewBtn_;
    HmcButton* applyBtn_;
    HmcButton* closeBtn_;

    // Result holder shared with the worker job.
    std::shared_ptr<std::optional<kuiper::Result<kuiper::ConfigureSummary>>> out_;
    bool lastWasDryRun_ = false;
};
