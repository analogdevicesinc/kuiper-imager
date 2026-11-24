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

#include <QDialog>
#include <QString>

#include <functional>
#include <vector>

#include "kuiper/Release.hpp"

class QTableWidget;

// Picks one release from a pre-fetched list (the window does the async
// listReleases()). exec() returns Accepted; selectedId() is the Release::id to
// hand to ImageService::fetch(), or empty if nothing was chosen. `cachedPathOf`
// reports where a release already lives on disk (empty if not downloaded): cached
// rows are badged and deletable. See docs: gui-console (download cache).
class ReleasePickerDialog : public QDialog {
    Q_OBJECT
public:
    using CachedPathFn = std::function<QString(const kuiper::Release&)>;

    explicit ReleasePickerDialog(const std::vector<kuiper::Release>& releases,
                                 CachedPathFn cachedPathOf,
                                 QWidget* parent = nullptr);

    QString selectedId() const { return selectedId_; }
    QString selectedLabel() const { return selectedLabel_; }

private:
    void accept() override;
    // (Re)build the Status cell for a row from its current on-disk state.
    void refreshStatusCell(int row);
    void deleteCached(int row);

    const std::vector<kuiper::Release>& releases_;
    CachedPathFn cachedPathOf_;
    QTableWidget* table_;
    QString selectedId_;
    QString selectedLabel_;
};
