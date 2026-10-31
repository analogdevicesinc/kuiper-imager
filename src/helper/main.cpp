// kuiper-imager-helper — the privileged half of the GUI.
//
// Launched on demand by the unprivileged GUI (pkexec on Linux, UAC "runas" on
// Windows) to perform exactly one write (flash or raw preloader), then exit. It
// links libkuiper and reuses the same DriveService calls the CLI makes — including
// the removable/system safety guards, which thus run inside the elevated process
// where they matter most.
//
// Protocol: one verb + structured args in; newline-delimited JSON out (progress
// lines, then a final result/error line); a "cancel" line in — or the GUI going
// away (EOF) — requests cooperative cancellation. The transport is stdio when
// launched over pkexec, or a named pipe (--pipe <name>) when stdio cannot cross
// the UAC boundary. See Channel.hpp and kuiper/ipc/HelperProtocol.hpp.

#include <QCoreApplication>
#include <QProcess>
#include <QString>
#include <QStringList>

#include <atomic>
#include <csignal>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "Channel.hpp"
#include "kuiper/DriveService.hpp"
#include "kuiper/Error.hpp"
#include "kuiper/Progress.hpp"
#include "kuiper/ipc/HelperProtocol.hpp"

namespace {

// Set from the cancel watcher; polled by the write's CancelToken (mirrors the
// CLI's SIGINT atomic — but cancellation arrives over the channel, since the GUI
// cannot signal a process running elevated).
std::atomic<bool> g_cancel{false};

// The transport to the GUI (stdio on Linux, a named pipe on Windows). Set in
// main() before any output; every emitLine routes through it.
kuiper::helper::Channel* g_channel = nullptr;

void emitLine(const std::string& s) {
    g_channel->writeLine(s);  // the GUI parses lines as they arrive
}

int fail(kuiper::ErrorCode code, std::string message, std::string details = {}) {
    emitLine(kuiper::ipc::encodeError(
        kuiper::Error{code, std::move(message), std::move(details), std::nullopt}));
    return 1;
}

// Minimal "--name value" taker; advances `i` past the value on a match.
bool takeValue(const std::vector<std::string>& args, std::size_t& i,
               const char* name, std::string& out) {
    if (args[i] != name) return false;
    if (i + 1 < args.size()) out = args[++i];
    return true;
}

kuiper::ProgressFn makeProgressSink() {
    return [](const kuiper::Progress& p) {
        emitLine(kuiper::ipc::encodeProgress(p));
    };
}

kuiper::CancelToken makeCancelToken() {
    kuiper::CancelToken cancel;
    cancel.cancelled = [] { return g_cancel.load(); };
    return cancel;
}

// After a flash rewrites the partition table, force the kernel to re-read it and
// wait for udev to finish processing the change, so the freshly written BOOT
// partition is visible to lsblk and mountable (via udisks2) the instant we exit.
// We are still root here, where the re-read and settle actually complete; the
// unprivileged GUI cannot force them, which is why it otherwise had to guess with
// a timer. Best-effort: a missing tool just leaves the GUI's bounded retry to
// cover the gap. The device fd from flash() is already closed by now.
void settleDevice(const std::string& driveNode) {
#ifdef _WIN32
    // No-op on Windows: the partition-table re-read already happened in-session via
    // IOCTL_DISK_UPDATE_PROPERTIES (WindowsBlockIo::rereadPartTable). There is no
    // blockdev/udev to drive here.
    (void)driveNode;
#else
    const QString node = QString::fromStdString(driveNode);
    QProcess::execute("blockdev", {"--rereadpt", node});
    QProcess::execute("udevadm", {"settle", "--timeout=10"});
#endif
}

int runFlash(const std::vector<std::string>& args) {
    std::string image, drive;
    bool verify = true;
    for (std::size_t i = 1; i < args.size(); ++i) {
        if (takeValue(args, i, "--image", image)) continue;
        if (takeValue(args, i, "--drive", drive)) continue;
        if (args[i] == "--no-verify") {
            verify = false;
            continue;
        }
        return fail(kuiper::ErrorCode::Unknown,
                    "Unexpected argument: " + args[i]);
    }
    if (image.empty() || drive.empty())
        return fail(kuiper::ErrorCode::Unknown,
                    "flash requires --image and --drive");

    kuiper::DriveService service;
    kuiper::WriteOptions opts;
    opts.force = false;  // the GUI never bypasses the removable/system guards
    opts.verify = verify;

    auto result = service.flash(drive, image, opts, makeProgressSink(),
                                makeCancelToken());
    if (!result) return fail(result.error().code, result.error().message,
                             result.error().details);
    settleDevice(drive);  // make the new partitions live before the GUI re-probes
    emitLine(kuiper::ipc::encodeResult(*result));
    return 0;
}

int runWritePreloader(const std::vector<std::string>& args) {
    std::string partition, image;
    for (std::size_t i = 1; i < args.size(); ++i) {
        if (takeValue(args, i, "--partition", partition)) continue;
        if (takeValue(args, i, "--image", image)) continue;
        return fail(kuiper::ErrorCode::Unknown,
                    "Unexpected argument: " + args[i]);
    }
    if (partition.empty() || image.empty())
        return fail(kuiper::ErrorCode::Unknown,
                    "write-preloader requires --partition and --image");

    kuiper::DriveService service;
    kuiper::WriteOptions opts;
    opts.force = false;
    opts.verify = true;

    auto result = service.writePreloader(partition, image, opts,
                                         makeProgressSink(), makeCancelToken());
    if (!result) return fail(result.error().code, result.error().message,
                             result.error().details);
    emitLine(kuiper::ipc::encodeResult(*result));
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
#ifndef _WIN32
    // If the GUI dies, writing to its closed stdout must fail with EPIPE rather
    // than kill us mid-write via SIGPIPE: the cancel watcher sees the same EOF on
    // stdin and stops the write cleanly at the next cancellation point.
    std::signal(SIGPIPE, SIG_IGN);
#endif

    // QCoreApplication: the Linux backend shells out to lsblk via QProcess during
    // the pre-write safety check, which needs a Qt application object to exist.
    QCoreApplication app(argc, argv);

    // Decode arguments through QCoreApplication (GetCommandLineW on Windows) rather
    // than raw char** argv, which is the ANSI code page there and would corrupt a
    // non-ASCII image/drive path. toStdString() yields UTF-8, which the Windows
    // backend's widen() turns back into the correct UTF-16.
    const QStringList qargs = app.arguments();
    std::vector<std::string> args;
    for (int i = 1; i < qargs.size(); ++i)
        args.push_back(qargs.at(i).toStdString());

    // Extract an optional leading "--pipe <name>": its presence selects the named-
    // pipe transport (Windows), its absence stdio (Linux/macOS). Strip it so the
    // verb handlers see only their own arguments.
    std::string pipeName;
    for (std::size_t i = 0; i + 1 < args.size(); ++i) {
        if (args[i] == "--pipe") {
            pipeName = args[i + 1];
            args.erase(args.begin() + i, args.begin() + i + 2);
            break;
        }
    }
    auto channel = kuiper::helper::makeChannel(pipeName);
    if (!channel) {
        // Asked to report over a pipe we can't reach: refuse to write at all.
        // There is no channel for an `error` line, so the exit code carries it.
        std::_Exit(kuiper::ipc::kExitNoChannel);
    }
    g_channel = channel.get();

    int code;
    if (args.empty()) {
        code = fail(kuiper::ErrorCode::Unknown, "no verb given");
    } else {
        kuiper::helper::Channel* ch = channel.get();
        std::thread([ch] { ch->watchCancel(g_cancel); }).detach();
        const std::string& verb = args[0];
        if (verb == "flash")
            code = runFlash(args);
        else if (verb == "write-preloader")
            code = runWritePreloader(args);
        else
            code = fail(kuiper::ErrorCode::Unknown, "unknown verb: " + verb);
    }

    // Exit now, without running static destructors. The detached cancel watcher is
    // (normally) still blocked reading its input, and letting normal teardown run
    // can deadlock against it — which would leave the process alive after the write
    // is already done, so the GUI never sees it finish. All output is line-flushed
    // already (emitLine), so _Exit loses nothing.
    std::fflush(stdout);
    std::fflush(stderr);
    std::_Exit(code);
}
