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

#include <string>
#include <vector>

#include "kuiper/Error.hpp"
#include "kuiper/Release.hpp"
#include "kuiper/ReleaseProvider.hpp"
#include "kuiper/http/HttpClient.hpp"

namespace kuiper {

// Serves the stable channel from a hosted JSON manifest, cached on disk with a
// compiled-in seed as the offline fallback. See docs: releases-and-sources.
class StableReleaseProvider : public ReleaseProvider {
public:
    StableReleaseProvider(IHttpClient& http, std::string cacheDir);

    Result<std::vector<Release>> list(const ReleaseQuery& query) override;
    Result<DownloadSource> resolve(const std::string& id) override;

private:
    Result<std::vector<Release>> catalog();

    IHttpClient& http_;
    std::string cacheDir_;
};

}  // namespace kuiper
