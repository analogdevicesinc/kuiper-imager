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

#include <QByteArray>
#include <QProcess>
#include <QString>
#include <QStringList>

#include <string>
#include <vector>

#include "kuiper/customize/CommandRunner.hpp"

namespace kuiper {

namespace {

class QProcessCommandRunner : public CommandRunner {
public:
    Result<CommandResult> run(const std::vector<std::string>& argv,
                              LineFn onLine) override {
        if (argv.empty()) return Err(ErrorCode::Unknown, "Empty command");

        const QString program = QString::fromStdString(argv[0]);
        QStringList args;
        for (std::size_t i = 1; i < argv.size(); ++i)
            args << QString::fromStdString(argv[i]);

        QProcess proc;
        proc.setProcessChannelMode(QProcess::MergedChannels);  // stdout+stderr as one
        proc.start(program, args);
        if (!proc.waitForStarted(10000)) {
            return Err(ErrorCode::UnsupportedPlatform,
                       "Could not run '" + argv[0] + "'",
                       "Is it installed and on PATH?");
        }

        std::string output;
        std::string pending;  // partial trailing line between reads
        auto drain = [&]() {
            const QByteArray chunk = proc.readAll();
            if (chunk.isEmpty()) return;
            const std::string s(chunk.constData(),
                                static_cast<std::size_t>(chunk.size()));
            output += s;
            if (!onLine) return;
            pending += s;
            std::size_t nl;
            while ((nl = pending.find('\n')) != std::string::npos) {
                onLine(pending.substr(0, nl));
                pending.erase(0, nl + 1);
            }
        };

        // Stream while running; no overall timeout (apt can run for minutes).
        while (proc.state() != QProcess::NotRunning) {
            proc.waitForReadyRead(200);
            drain();
        }
        drain();
        if (onLine && !pending.empty()) onLine(pending);

        if (proc.exitStatus() != QProcess::NormalExit)
            return Err(ErrorCode::Unknown, "'" + argv[0] + "' crashed", output);
        return CommandResult{proc.exitCode(), std::move(output)};
    }
};

}  // namespace

std::unique_ptr<CommandRunner> makeCommandRunner() {
    return std::make_unique<QProcessCommandRunner>();
}

}  // namespace kuiper
