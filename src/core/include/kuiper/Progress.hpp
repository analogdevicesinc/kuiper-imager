#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace kuiper {

// Progress report for a long-running operation (flash/verify). Emitted from the
// core; the CLI/GUI turn it into a progress bar. Portable — no platform types.
struct Progress {
    enum class Phase {
        Preparing,   // unmount, open, wipe signatures
        Downloading, // stream a remote artifact to a local file (fetch)
        Writing,     // decompress + write image body to device
        Finalizing,  // flush staged tail, commit partition table, sync to media
        Verifying,   // read back + hash compare
        Configuring, // copy a project's boot files into the boot partition
        Done,        // finished successfully
    };

    Phase phase = Phase::Preparing;
    std::uint64_t bytesDone = 0;   // bytes processed in the current phase
    std::uint64_t bytesTotal = 0;  // 0 if unknown (streaming/unknown size)
    double fraction = 0.0;         // [0,1], monotonic within a phase
    std::string message;           // short human-readable status
};

// Callback invoked on progress updates. May be empty (no reporting).
using ProgressFn = std::function<void(const Progress&)>;

// Cooperative cancellation. Polled between chunks; the operation aborts cleanly
// (device left non-bootable) and returns ErrorCode::UserCancelled.
struct CancelToken {
    std::function<bool()> cancelled;  // empty => never cancelled

    bool isCancelled() const { return cancelled && cancelled(); }
};

}  // namespace kuiper
