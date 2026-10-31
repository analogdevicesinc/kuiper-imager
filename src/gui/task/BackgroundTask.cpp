#include "BackgroundTask.hpp"

#include <QtConcurrent>

BackgroundTask::BackgroundTask(QObject* parent)
    : QObject(parent), cancel_(std::make_shared<std::atomic<bool>>(false)) {
    // kuiper::Progress travels across a thread boundary via a queued signal, so
    // it must be a registered metatype. Idempotent — safe to call more than once.
    qRegisterMetaType<kuiper::Progress>("kuiper::Progress");
    connect(&watcher_, &QFutureWatcher<void>::finished, this,
            &BackgroundTask::finished);
}

void BackgroundTask::run(Job job) {
    cancel_->store(false);
    auto cancelFlag = cancel_;

    // Emitting from the worker thread auto-queues onto the GUI thread (this
    // object lives there). Capturing `this` relies on the owner keeping the task
    // alive until finished(): QFutureWatcher does not wait for the job on
    // destruction. ConsoleWindow and ConfigureDialog refuse to close while
    // busy() for exactly this reason.
    QFuture<void> fut = QtConcurrent::run([this, job = std::move(job),
                                           cancelFlag]() {
        kuiper::ProgressFn onProgress = [this](const kuiper::Progress& p) {
            emit progress(p);
        };
        kuiper::CancelToken cancel;
        cancel.cancelled = [cancelFlag] { return cancelFlag->load(); };
        job(std::move(onProgress), std::move(cancel));
    });
    watcher_.setFuture(fut);
}

void BackgroundTask::cancel() { cancel_->store(true); }

bool BackgroundTask::busy() const { return watcher_.isRunning(); }
