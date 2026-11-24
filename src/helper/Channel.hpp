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

#include <atomic>
#include <memory>
#include <string>

// The helper's transport-neutral I/O surface. The helper writes NDJSON lines
// toward the GUI and watches an inbound stream for a "cancel" line — the same
// protocol over different pipes:
//   - stdio          (Linux/macOS, inherited across pkexec)
//   - a named pipe   (Windows, since stdio can't cross the UAC boundary)
//
// Keeping this behind one interface means runFlash/runWritePreloader and the whole
// of main() are identical on every OS; only which Channel is constructed differs.
namespace kuiper::helper {

class Channel {
public:
    virtual ~Channel() = default;

    // Write one line (a '\n' is appended) and flush, so the GUI parses it at once.
    virtual void writeLine(const std::string& s) = 0;

    // Block reading the inbound stream until a "cancel" line arrives or the
    // stream ends, and set `flag` either way: the stream ending means the GUI is
    // gone, and an elevated write nobody can watch or cancel should stop at the
    // next cancellation point rather than run on unattended. Runs on a detached
    // thread for the process's life.
    virtual void watchCancel(std::atomic<bool>& flag) = 0;
};

// `pipeName` empty -> stdio; non-empty -> connect to that named pipe (Windows).
// Returns nullptr if a pipe was requested but could not be connected: the caller
// must then not write (there is no one to report to — see ipc::kExitNoChannel).
std::unique_ptr<Channel> makeChannel(const std::string& pipeName);

}  // namespace kuiper::helper
