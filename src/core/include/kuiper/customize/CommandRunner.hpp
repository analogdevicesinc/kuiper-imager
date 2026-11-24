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

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "kuiper/Error.hpp"

namespace kuiper {

// Result of running one external command: its exit code and its captured output
// (stdout and stderr merged, as a terminal would show).
struct CommandResult {
    int exitCode = 0;
    std::string output;
};

// The one seam through which ImageCustomizer touches the outside world, so the
// orchestration is fake-testable. See docs: customize-pipeline.
//
// argv[0] is the program (resolved on PATH); argv[1..] its arguments. No shell is
// involved, so arguments are passed verbatim. `onLine`, when set, fires once per
// complete output line (live streaming); the full output is also returned. A
// failure to *launch* is an Err; a program that exits non-zero is not — it returns
// a non-zero `exitCode` and the caller decides what that means.
class CommandRunner {
public:
    using LineFn = std::function<void(const std::string&)>;

    virtual Result<CommandResult> run(const std::vector<std::string>& argv,
                                      LineFn onLine = {}) = 0;

    virtual ~CommandRunner() = default;
};

// The production runner, backed by QProcess (merged channels, line streaming).
std::unique_ptr<CommandRunner> makeCommandRunner();

}  // namespace kuiper
