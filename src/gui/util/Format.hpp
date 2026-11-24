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

#include <QString>
#include <cstdint>

// Small shared formatting helpers for the GUI (byte sizes, etc.).
namespace kuiper::gui {

// Human-readable size, matching the imager's "8.0 GB" / "1.0 TB" style.
inline QString formatBytes(std::uint64_t bytes) {
    constexpr double kib = 1024.0;
    double v = static_cast<double>(bytes);
    const char* units[] = {"B", "KB", "MB", "GB", "TB", "PB"};
    int i = 0;
    while (v >= kib && i < 5) {
        v /= kib;
        ++i;
    }
    return QString::number(v, 'f', (i == 0) ? 0 : 1) + " " + units[i];
}

}  // namespace kuiper::gui
