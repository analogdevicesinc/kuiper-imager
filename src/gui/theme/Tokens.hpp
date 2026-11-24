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
// Kuiper GUI theme colour tokens as QColor constants, for custom-painted
// widgets (QSS covers everything stylable; this is for QPainter code).
// Hand-authored — keep in sync with theme-light.qss / theme-dark.qss.

#include <QColor>

namespace kuiper::theme::light {
inline const QColor content{16, 24, 32, 255};             // content
inline const QColor contentWeak{132, 139, 149, 255};      // content-weak
inline const QColor inputBackground{255, 255, 255, 255};  // input-background
inline const QColor interactivePrimary{0, 103, 185,
                                       255};              // interactive-primary
inline const QColor layoutCanvas{240, 241, 243, 255};     // layout-canvas
inline const QColor layoutContainer{255, 255, 255, 255};  // layout-container
}  // namespace kuiper::theme::light

namespace kuiper::theme::dark {
inline const QColor content{255, 255, 255, 255};       // content
inline const QColor contentWeak{132, 139, 149, 255};   // content-weak
inline const QColor inputBackground{41, 48, 56, 255};  // input-background
inline const QColor interactivePrimary{146, 190, 252,
                                       255};           // interactive-primary
inline const QColor layoutCanvas{16, 24, 32, 255};     // layout-canvas
inline const QColor layoutContainer{41, 48, 56, 255};  // layout-container
}  // namespace kuiper::theme::dark
