#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>

#include <optional>
#include <string>

#include "kuiper/ConfigurationService.hpp"
#include "kuiper/DriveService.hpp"
#include "kuiper/Progress.hpp"
#include "task/IHelperSession.hpp"

// Runs one privileged write (flash / raw preloader) in the kuiper-imager-helper
// process. The GUI stays unprivileged; only this tiny helper is elevated, for the
// duration of a single operation.
//
// This class is the OS-agnostic half: it buffers the helper's NDJSON stream,
// decodes it (kuiper/ipc/HelperProtocol), and surfaces the outcome. The elevation
// and transport mechanism lives behind IHelperSession (pkexec+stdio on Linux,
// UAC+named-pipe on Windows), so this file — and ConsoleWindow above it — are the
// same everywhere.
//
// Deliberately exposes the SAME contract as BackgroundTask — progress(Progress)
// and finished() — so ConsoleWindow's progress sink, Cancel button and busy-state
// UI are reused unchanged. After finished(), result() holds the outcome (mirrors
// the Result<T> holders the thread-based path uses).
class HelperProcess : public QObject {
    Q_OBJECT
public:
    explicit HelperProcess(QObject* parent = nullptr);

    void runFlash(const QString& driveNode, const QString& imagePath, bool verify);
    void runWritePreloader(const QString& partitionNode, const QString& imagePath);

    // Cooperative cancel: asks the session to deliver "cancel" to the helper (the
    // GUI cannot signal a root process). No-op if nothing is running.
    void cancel();
    bool busy() const;

    // Valid once finished() has fired: the outcome of the last run.
    const std::optional<kuiper::Result<kuiper::FlashSummary>>& result() const {
        return result_;
    }

signals:
    void progress(kuiper::Progress p);
    void finished();

private:
    void start(const QStringList& verbArgs);
    void feed(const QByteArray& chunk);  // parse buffered NDJSON lines
    void onFinished(int exitCode, IHelperSession::Outcome outcome);
    static QString resolveHelperPath();

    IHelperSession* session_;
    QByteArray buf_;  // accumulates partial NDJSON lines
    std::optional<kuiper::Result<kuiper::FlashSummary>> result_;
    bool sawTerminal_ = false;  // a result/error line was parsed
};

// A PreloaderSink that performs the raw preloader write in the elevated helper,
// as the flash already does — the unprivileged GUI cannot open the raw partition.
//
// ConfigurationService calls the sink synchronously on a worker thread and expects
// a Result back, while a HelperProcess is event-driven. So the sink posts the run
// to `context`'s (GUI) thread, where a one-shot HelperProcess drives the helper
// and forwards its progress, and blocks the worker until it finishes. `context`
// must outlive the configure job; never call the sink on the GUI thread itself.
kuiper::PreloaderSink makeElevatedPreloaderSink(QObject* context,
                                                std::string partitionNode);
