#pragma once

#include <QFutureWatcher>
#include <QObject>

#include <atomic>
#include <functional>
#include <memory>

#include "kuiper/Progress.hpp"

// Runs one blocking libkuiper call on a worker thread so the UI stays responsive.
//
// The core services (DriveService/ImageService/ConfigurationService) are all
// synchronous and report via a `ProgressFn` + cooperative `CancelToken` — the
// same plumbing the CLI backs with a SIGINT atomic. This wraps that for Qt:
//   - `run(job)` executes `job` on QtConcurrent's thread pool;
//   - the `ProgressFn` handed to `job` emits progress() marshaled to the GUI
//     thread (a queued cross-thread signal);
//   - the `CancelToken` is backed by an atomic that cancel() flips;
//   - finished() fires on the GUI thread when the job returns.
//
// Lifetime: the owner must outlive any running job (the job typically captures
// the owner, and destroying the task does not wait for the worker), so owners
// refuse to close while busy().
//
// One job at a time: callers gate new work on busy(). The job captures its own
// Result<T> holder (see ConsoleWindow) and the owner reads it in the finished
// slot, mirroring the CLI's truthiness + .error() pattern.
class BackgroundTask : public QObject {
    Q_OBJECT
public:
    explicit BackgroundTask(QObject* parent = nullptr);

    // A unit of work: given the progress sink and cancel token, do the blocking
    // call(s) and stash the result somewhere the caller owns.
    using Job = std::function<void(kuiper::ProgressFn, kuiper::CancelToken)>;

    void run(Job job);
    void cancel();
    bool busy() const;

signals:
    void progress(kuiper::Progress p);
    void finished();

private:
    // Shared so the worker's CancelToken stays valid for the job's lifetime.
    std::shared_ptr<std::atomic<bool>> cancel_;
    QFutureWatcher<void> watcher_;
};
