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

#include <optional>
#include <string>
#include <vector>

#include "kuiper/Error.hpp"
#include "kuiper/Release.hpp"
#include "kuiper/http/HttpClient.hpp"

namespace kuiper {

// A ready-to-download location: the URL plus any headers (auth, etc.) the source
// needs, and the artifact's canonical filename so callers can save it under its
// own name. Handed to IHttpClient::download() by ImageService::fetch().
struct DownloadSource {
    std::string url;
    std::vector<HttpHeader> headers;
    std::string filename;  // e.g. "kuiper_full_64_image.zip"
    std::optional<std::string> expectedSha256;  // verify after download if set (stable)
};

// One release source: GitHub Actions CI artifacts (unstable) or published
// releases (stable). ImageService dispatches to these by channel/identifier
// scheme. See docs: releases-and-sources.
class ReleaseProvider {
public:
    virtual Result<std::vector<Release>> list(const ReleaseQuery& query) = 0;

    // Turn a Release::id (e.g. "gh:<artifactId>") into a downloadable source,
    // including the artifact's canonical filename. May make one lightweight
    // metadata request (e.g. to read the name and check it hasn't expired).
    virtual Result<DownloadSource> resolve(const std::string& id) = 0;

    virtual ~ReleaseProvider() = default;
};

}  // namespace kuiper
