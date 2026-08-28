#include "GitHubActionsProvider.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>
#include <QUrl>

#include "kuiper/Version.hpp"

namespace kuiper {
namespace {

constexpr const char* kApiBase =
    "https://api.github.com/repos/analogdevicesinc/kuiper";
constexpr const char* kWorkflowFile = "kuiper2_0-build.yml";
constexpr std::string_view kImageSuffix = "_image";

// Parsed pieces of an artifact name like "kuiper_full_64_image".
struct Variant {
    std::string type;  // "full" | "basic" (or the raw token if unrecognised)
    std::string arch;  // "arm32" | "arm64" (empty if unrecognised)
};

// The build encodes the ARM word size as "32"/"64"; surface it as arm32/arm64.
Variant variantOf(const std::string& artifactName) {
    const QStringList parts = QString::fromStdString(artifactName).split('_');
    if (parts.size() == 4 && parts.back() == QStringLiteral("image")) {
        const QString bits = parts[2];  // "32" | "64"
        std::string arch;
        if (bits == QStringLiteral("64")) arch = "arm64";
        else if (bits == QStringLiteral("32")) arch = "arm32";
        return {parts[1].toStdString(), std::move(arch)};
    }
    std::string v = artifactName;
    if (v.ends_with(kImageSuffix)) v.erase(v.size() - kImageSuffix.size());
    return {std::move(v), ""};
}

}  // namespace

GitHubActionsProvider::GitHubActionsProvider(IHttpClient& http,
                                             std::optional<std::string> token)
    : http_(http), token_(std::move(token)) {}

std::vector<HttpHeader> GitHubActionsProvider::apiHeaders() const {
    std::vector<HttpHeader> headers = {
        {"User-Agent", std::string("kli/") + version()},  // GitHub requires a UA
        {"Accept", "application/vnd.github+json"},
        {"X-GitHub-Api-Version", "2022-11-28"},
    };
    if (token_) headers.push_back({"Authorization", "Bearer " + *token_});
    return headers;
}

Result<HttpResponse> GitHubActionsProvider::apiGet(const std::string& url) {
    auto resp = http_.get(url, apiHeaders());
    if (!resp) return std::unexpected(resp.error());

    const long s = resp->status;
    if (s >= 200 && s < 300) return resp;
    if (s == 401) {
        return Err(ErrorCode::PermissionDenied, "GitHub rejected the API token",
                   url, "Check $GH_TOKEN / $GITHUB_TOKEN, or run 'gh auth token'.");
    }
    if (s == 403 || s == 429) {
        return Err(ErrorCode::NetworkFailure,
                   "GitHub API rate limit reached or access denied",
                   "HTTP " + std::to_string(s) + " for " + url,
                   "Set $GH_TOKEN / $GITHUB_TOKEN (or run 'gh auth token') to "
                   "raise the rate limit.");
    }
    if (s == 404) {
        return Err(ErrorCode::NotFound, "GitHub resource not found", url);
    }
    return Err(ErrorCode::NetworkFailure, "GitHub API request failed",
               "HTTP " + std::to_string(s) + " for " + url);
}

Result<std::vector<Release>> GitHubActionsProvider::list(
    const ReleaseQuery& query) {
    const int limit = std::clamp(query.limit, 1, 100);

    // One request for the recent runs, then one per run for its artifacts (1+N):
    // this spends the unauthenticated rate budget quickly — hence the token.
    std::string runsUrl = std::string(kApiBase) + "/actions/workflows/" +
                          kWorkflowFile +
                          "/runs?status=success&per_page=" + std::to_string(limit);
    if (query.branch && !query.branch->empty()) {
        // Git ref names may contain &, #, =, % — percent-encode for the query.
        runsUrl += "&branch=" +
                   QUrl::toPercentEncoding(QString::fromStdString(*query.branch))
                       .toStdString();
    }

    auto runsResp = apiGet(runsUrl);
    if (!runsResp) return std::unexpected(runsResp.error());

    const QJsonDocument runsDoc =
        QJsonDocument::fromJson(QByteArray::fromStdString(runsResp->body));
    if (!runsDoc.isObject()) {
        return Err(ErrorCode::NetworkFailure, "Unexpected GitHub response",
                   "the runs payload was not a JSON object");
    }
    const QJsonArray runs = runsDoc.object().value("workflow_runs").toArray();

    std::vector<Release> releases;
    for (const QJsonValue& rv : runs) {
        const QJsonObject run = rv.toObject();
        const auto runId = static_cast<std::int64_t>(run.value("id").toInteger());
        const std::string branch =
            run.value("head_branch").toString().toStdString();
        const std::string sha = run.value("head_sha").toString().toStdString();
        const std::string sha7 = sha.substr(0, std::min<std::size_t>(7, sha.size()));

        const std::string artUrl = std::string(kApiBase) + "/actions/runs/" +
                                   std::to_string(runId) + "/artifacts";
        auto artResp = apiGet(artUrl);
        if (!artResp) return std::unexpected(artResp.error());

        const QJsonDocument artDoc =
            QJsonDocument::fromJson(QByteArray::fromStdString(artResp->body));
        const QJsonArray artifacts =
            artDoc.object().value("artifacts").toArray();

        for (const QJsonValue& av : artifacts) {
            const QJsonObject art = av.toObject();
            const std::string name = art.value("name").toString().toStdString();
            if (!name.ends_with(kImageSuffix)) {
                continue;  // skip *_meta and anything that isn't an image
            }

            Release r;
            const auto artId =
                static_cast<std::int64_t>(art.value("id").toInteger());
            r.id = "gh:" + std::to_string(artId);  // unique; see Release.hpp
            r.channel = "unstable";
            r.branch = branch;
            r.commit = sha7;
            Variant variant = variantOf(name);
            r.variant = std::move(variant.type);
            r.arch = std::move(variant.arch);
            r.sizeBytes =
                static_cast<std::uint64_t>(art.value("size_in_bytes").toInteger());
            r.available = !art.value("expired").toBool();
            if (const QJsonValue e = art.value("expires_at"); e.isString()) {
                r.expiresAt = e.toString().toStdString();
            }
            r.sourceUri =
                art.value("archive_download_url").toString().toStdString();
            releases.push_back(std::move(r));
        }
    }

    return releases;
}

Result<DownloadSource> GitHubActionsProvider::resolve(const std::string& id) {
    // Identifiers look like "gh:<numeric artifact id>" (see Release.hpp).
    constexpr std::string_view kScheme = "gh:";
    if (!id.starts_with(kScheme)) {
        return Err(ErrorCode::NotFound, "Unknown image identifier: " + id,
                   "Expected 'gh:<id>' from 'kli list-releases --unstable'.");
    }
    const std::string artId(id.substr(kScheme.size()));
    if (artId.empty() ||
        artId.find_first_not_of("0123456789") != std::string::npos) {
        return Err(ErrorCode::NotFound, "Malformed image identifier: " + id,
                   "The GitHub artifact id must be numeric, e.g. 'gh:9607528144'.");
    }
    if (!token_) {
        return Err(ErrorCode::PermissionDenied,
                   "Downloading Kuiper CI artifacts requires a GitHub token",
                   "The artifact download endpoint is authenticated (unlike "
                   "listing).",
                   "Set $GH_TOKEN / $GITHUB_TOKEN (or run 'gh auth token'); the "
                   "token needs the 'actions:read' scope.");
    }

    // Read the artifact metadata for its canonical name — and to fail early if it
    // has expired or vanished. The bytes URL itself is deterministic from the id.
    const std::string metaUrl =
        std::string(kApiBase) + "/actions/artifacts/" + artId;
    auto meta = apiGet(metaUrl);
    if (!meta) return std::unexpected(meta.error());

    const QJsonObject art =
        QJsonDocument::fromJson(QByteArray::fromStdString(meta->body)).object();
    if (art.value("expired").toBool()) {
        return Err(ErrorCode::NotFound, "Artifact has expired: " + id,
                   "GitHub deletes CI artifacts after ~90 days — re-run "
                   "'kli list-releases --unstable' for a current id.");
    }
    const std::string name = art.value("name").toString().toStdString();

    DownloadSource src;
    src.url = metaUrl + "/zip";
    src.headers = apiHeaders();
    src.filename = (name.empty() ? ("artifact-" + artId) : name) + ".zip";
    return src;
}

}  // namespace kuiper
