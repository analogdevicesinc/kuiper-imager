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

#pragma once

#include <cstdint>
#include <string>

#include "kuiper/DriveService.hpp"
#include "kuiper/Error.hpp"
#include "kuiper/ImageCustomizer.hpp"
#include "kuiper/Progress.hpp"

// Wire protocol between the unprivileged GUI and the privileged helper process
// (kuiper-imager-helper, elevated via pkexec or UAC). One JSON object per line —
// newline-delimited JSON (NDJSON) — over whichever transport the platform uses
// (stdio under pkexec, a named pipe under UAC). The GUI parses each line as it
// arrives; the final line is always a `result` or `error`. In the other
// direction, a "cancel" line requests cooperative cancellation.
//
// Keeping this in the core means both sides (helper links the core, GUI links the
// core) share one definition and can never drift. See docs: platform-backends
// (Privilege).
namespace kuiper::ipc {

// Helper exit code when it was told to use a transport (`--pipe`) but could not
// reach the GUI over it. The helper then refuses to write at all — running an
// elevated write nobody can observe or cancel is worse than not running it —
// and, having no channel for an `error` line, reports through this code.
inline constexpr int kExitNoChannel = 3;

// One decoded message from the helper's stream.
struct HelperMessage {
    enum class Type { Progress, Result, CustomizeResult, Error, Unknown };

    Type type = Type::Unknown;

    Progress progress;  // valid iff type == Progress

    // valid iff type == Result (a flash / preloader write)
    std::uint64_t bytesWritten = 0;
    std::string sha256;

    CustomizeSummary customize;  // valid iff type == CustomizeResult

    Error error;  // valid iff type == Error
};

// --- Encoders (helper side): produce a single NDJSON line, no trailing '\n'. ---
std::string encodeProgress(const Progress& p);
std::string encodeResult(const FlashSummary& summary);
std::string encodeCustomizeResult(const CustomizeSummary& summary);
std::string encodeError(const Error& e);

// --- Decoder (GUI side): parse one line. Non-JSON / unknown shapes yield a
// HelperMessage with type == Unknown (callers skip those — e.g. stray pkexec
// chatter). ---
HelperMessage parseLine(const std::string& line);

}  // namespace kuiper::ipc
