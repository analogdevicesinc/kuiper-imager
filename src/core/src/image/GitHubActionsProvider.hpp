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
#include "kuiper/ReleaseProvider.hpp"
#include "kuiper/http/HttpClient.hpp"

namespace kuiper {

// Lists Kuiper 2.0 CI images from the kuiper2_0-build.yml GitHub Actions workflow.
// Listing needs no auth (a token only raises the rate limit), but the artifact
// download endpoint is auth-gated, so resolve() requires a token. See docs:
// releases-and-sources.
class GitHubActionsProvider : public ReleaseProvider {
public:
    GitHubActionsProvider(IHttpClient& http, std::optional<std::string> token);

    Result<std::vector<Release>> list(const ReleaseQuery& query) override;
    Result<DownloadSource> resolve(const std::string& id) override;

private:
    // The headers every GitHub API request needs (User-Agent, Accept, API
    // version, and the bearer token when one is set).
    std::vector<HttpHeader> apiHeaders() const;

    // GET a GitHub API URL with apiHeaders(), mapping the HTTP status to a typed
    // Error (401 -> PermissionDenied, 403/429 -> rate-limit hint, ...).
    Result<HttpResponse> apiGet(const std::string& url);

    IHttpClient& http_;
    std::optional<std::string> token_;
};

}  // namespace kuiper
