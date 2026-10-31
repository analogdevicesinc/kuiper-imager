#pragma once

#include <cstdint>
#include <string>

#include "kuiper/DriveService.hpp"
#include "kuiper/Error.hpp"
#include "kuiper/Progress.hpp"

// Wire protocol between the unprivileged GUI and the privileged helper process
// (kuiper-imager-helper, launched via pkexec). One JSON object per line on the
// helper's stdout — newline-delimited JSON (NDJSON). The GUI parses each line as
// it arrives; the final line is always a `result` or `error`.
//
// Keeping this in the core means both sides (helper links the core, GUI links the
// core) share one definition and can never drift. See docs: platform-backends
// (Privilege).
namespace kuiper::ipc {

// One decoded message from the helper's stream.
struct HelperMessage {
    enum class Type { Progress, Result, Error, Unknown };

    Type type = Type::Unknown;

    Progress progress;  // valid iff type == Progress

    // valid iff type == Result
    std::uint64_t bytesWritten = 0;
    std::string sha256;

    Error error;  // valid iff type == Error
};

// --- Encoders (helper side): produce a single NDJSON line, no trailing '\n'. ---
std::string encodeProgress(const Progress& p);
std::string encodeResult(const FlashSummary& summary);
std::string encodeError(const Error& e);

// --- Decoder (GUI side): parse one line. Non-JSON / unknown shapes yield a
// HelperMessage with type == Unknown (callers skip those — e.g. stray pkexec
// chatter). ---
HelperMessage parseLine(const std::string& line);

}  // namespace kuiper::ipc
