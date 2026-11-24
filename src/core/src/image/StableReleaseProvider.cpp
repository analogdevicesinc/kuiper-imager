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

#include "StableReleaseProvider.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include <QByteArray>
#include <QFile>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLatin1String>
#include <QString>

#include "kuiper/Version.hpp"

// Global namespace so the Q_INIT_RESOURCE symbol resolves (resource in a static
// lib); idempotent.
static void kuiperInitReleaseResources() { Q_INIT_RESOURCE(kuiper_assets); }

namespace kuiper {
namespace {
namespace fs = std::filesystem;

// See docs: releases-and-sources
constexpr const char* kManifestUrl =
    "https://packages.analog.com/public/raw/files/kuiper_releases.json";
constexpr const char* kSeedResource = ":/kuiper/releases/seed.json";
constexpr int kSchemaVersion = 1;
constexpr auto kCacheTtl = std::chrono::minutes(10);

std::string field(const QJsonObject& o, const char* key) {
    const QJsonValue v = o.value(QLatin1String(key));
    return v.isString() ? v.toString().toStdString() : std::string{};
}

// Releases in document order
std::optional<std::vector<Release>> parseManifest(const std::string& body) {
    const QJsonDocument doc =
        QJsonDocument::fromJson(QByteArray::fromStdString(body));
    if (!doc.isObject()) return std::nullopt;
    const QJsonObject root = doc.object();

    if (root.value(QLatin1String("schemaVersion")).toInt(-1) != kSchemaVersion)
        return std::nullopt;
    const std::string baseUrl = field(root, "baseUrl");
    if (baseUrl.empty()) return std::nullopt;
    const QJsonValue entries = root.value(QLatin1String("releases"));
    if (!entries.isArray()) return std::nullopt;

    std::vector<Release> releases;
    for (const QJsonValue& ev : entries.toArray()) {
        if (!ev.isObject()) continue;
        const QJsonObject e = ev.toObject();
        const std::string version = field(e, "version");
        const std::string variant = field(e, "variant");
        const std::string arch = field(e, "arch");
        const std::string file = field(e, "file");
        if (version.empty() || variant.empty() || arch.empty() || file.empty())
            continue;  // can form neither an id nor a URL

        Release r;
        r.id = "adi:" + version + "/" + variant + "/" + arch;
        r.channel = "stable";
        r.version = version;
        r.variant = variant;
        r.arch = arch;
        r.sourceUri = baseUrl + "/" + file;
        r.recommended = e.value(QLatin1String("recommended")).toBool();
        r.sizeBytes = static_cast<std::uint64_t>(
            e.value(QLatin1String("sizeBytes")).toDouble());
        if (std::string sha = field(e, "sha256"); !sha.empty())
            r.sha256 = std::move(sha);
        releases.push_back(std::move(r));
    }
    if (releases.empty()) return std::nullopt;
    return releases;
}

std::optional<std::string> readFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return std::nullopt;
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

std::optional<std::string> readSeed() {
    kuiperInitReleaseResources();
    QFile f(QString::fromLatin1(kSeedResource));
    if (!f.open(QIODevice::ReadOnly)) return std::nullopt;
    return f.readAll().toStdString();
}

bool isFresh(const fs::path& cacheFile) {
    std::error_code ec;
    const auto mtime = fs::last_write_time(cacheFile, ec);
    if (ec) return false;
    return (fs::file_time_type::clock::now() - mtime) < kCacheTtl;
}

// Atomic (.part + rename); best-effort, a failure here is non-fatal.
void writeCache(const fs::path& dir, const fs::path& cacheFile,
                const std::string& body) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    fs::path part = cacheFile;
    part += ".part";
    {
        std::ofstream out(part, std::ios::binary | std::ios::trunc);
        if (!out) return;
        out.write(body.data(), static_cast<std::streamsize>(body.size()));
        if (!out.flush()) { fs::remove(part, ec); return; }
    }
    fs::rename(part, cacheFile, ec);
    if (ec) fs::remove(part, ec);
}

}  // namespace

StableReleaseProvider::StableReleaseProvider(IHttpClient& http,
                                             std::string cacheDir)
    : http_(http), cacheDir_(std::move(cacheDir)) {}

// Resolution chain: fresh cache -> live -> stale cache -> seed. See docs:
// releases-and-sources.
Result<std::vector<Release>> StableReleaseProvider::catalog() {
    const bool useCache = !cacheDir_.empty();
    const fs::path cacheFile =
        useCache ? fs::path(cacheDir_) / "kuiper_releases.json" : fs::path{};

    if (useCache && isFresh(cacheFile)) {
        if (auto body = readFile(cacheFile))
            if (auto parsed = parseManifest(*body)) return std::move(*parsed);
    }

    const std::vector<HttpHeader> headers = {
        {"User-Agent", std::string("kli/") + version()},
        {"Accept", "application/json"},
    };
    if (auto resp = http_.get(kManifestUrl, headers);
        resp && resp->status >= 200 && resp->status < 300) {
        if (auto parsed = parseManifest(resp->body)) {
            if (useCache) writeCache(cacheDir_, cacheFile, resp->body);
            return std::move(*parsed);
        }
    }

    if (useCache) {
        if (auto body = readFile(cacheFile))
            if (auto parsed = parseManifest(*body)) return std::move(*parsed);
    }

    if (auto seed = readSeed())
        if (auto parsed = parseManifest(*seed)) return std::move(*parsed);

    return Err(ErrorCode::Unknown, "No stable release index is available",
               "The release manifest could not be fetched, read from cache, or "
               "loaded from the built-in fallback.");
}

Result<std::vector<Release>> StableReleaseProvider::list(const ReleaseQuery&) {
    return catalog();
}

Result<DownloadSource> StableReleaseProvider::resolve(const std::string& id) {
    auto releases = catalog();
    if (!releases) return std::unexpected(releases.error());

    for (const Release& r : *releases) {
        if (r.id != id) continue;
        DownloadSource src;
        src.url = r.sourceUri;
        src.filename = fs::path(r.sourceUri).filename().string();
        src.expectedSha256 = r.sha256;
        return src;
    }
    return Err(ErrorCode::NotFound, "Unknown image identifier: " + id,
               "Run 'kli list-releases' for valid stable identifiers.");
}

}  // namespace kuiper
