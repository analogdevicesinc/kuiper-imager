// ImageService::fetch with a canned HTTP client — resolve metadata, stream the
// artifact to a .part file, rename on success. No network. See docs: releases.

#include "doctest_kuiper.hpp"

#include <fstream>
#include <memory>
#include <optional>
#include <string>

#include "kuiper/ImageService.hpp"

#include "fakes/FakeHttpClient.hpp"
#include "support.hpp"

using namespace kuiper;
using kuiper::test::FakeHttpClient;

namespace {

// An ImageService plus the fake it owns (kept as a raw pointer so the test can
// still tweak/inspect the canned responses after construction).
struct HttpRig {
    FakeHttpClient* http;
    ImageService service;
};

HttpRig makeRig(std::optional<std::string> token = std::string("test-token")) {
    auto http = std::make_unique<FakeHttpClient>();
    auto* raw = http.get();
    raw->getBody = R"({"name":"kuiper-2.0-artifact","expired":false})";
    raw->downloadBody = "the-image-bytes";
    return HttpRig{raw, ImageService(std::move(http), std::move(token))};
}

}  // namespace

TEST_CASE("fetch downloads the artifact and reports bytes written") {
    kuiper::test::TempDir tmp;
    const std::string out = (tmp.path() / "image.zip").string();

    auto rig = makeRig();
    auto r = rig.service.fetch("gh:9607528144", out, /*force=*/true);

    REQUIRE_MESSAGE(r, (r ? "" : r.error().message));
    CHECK(r->bytesWritten == rig.http->downloadBody.size());
    CHECK(r->sha256.size() == 64);

    std::ifstream in(r->outputPath, std::ios::binary);
    REQUIRE(in);
    std::string content((std::istreambuf_iterator<char>(in)), {});
    CHECK(content == rig.http->downloadBody);
    // The download endpoint is the artifact zip, derived from the metadata URL.
    CHECK(rig.http->lastDownloadUrl.ends_with("/zip"));
}

TEST_CASE("fetch rejects a non-gh identifier before any I/O") {
    kuiper::test::TempDir tmp;
    auto rig = makeRig();
    auto r = rig.service.fetch("s3:whatever", (tmp.path() / "x").string(), true);
    REQUIRE_FALSE(r);
    CHECK_ERRCODE(r.error().code, ErrorCode::NotFound);
}

TEST_CASE("fetch requires a token for the authenticated download endpoint") {
    kuiper::test::TempDir tmp;
    auto rig = makeRig(std::nullopt);  // no token
    auto r = rig.service.fetch("gh:123", (tmp.path() / "x.zip").string(), true);
    REQUIRE_FALSE(r);
    CHECK_ERRCODE(r.error().code, ErrorCode::PermissionDenied);
}

TEST_CASE("fetch reports an expired artifact as NotFound") {
    kuiper::test::TempDir tmp;
    auto rig = makeRig();
    rig.http->getBody = R"({"name":"old","expired":true})";
    auto r = rig.service.fetch("gh:123", (tmp.path() / "x.zip").string(), true);
    REQUIRE_FALSE(r);
    CHECK_ERRCODE(r.error().code, ErrorCode::NotFound);
}

TEST_CASE("fetch refuses to overwrite an existing file without force") {
    kuiper::test::TempDir tmp;
    const std::string out = (tmp.path() / "image.zip").string();
    kuiper::test::writeText(out, "already here");

    auto rig = makeRig();
    auto r = rig.service.fetch("gh:123", out, /*force=*/false);
    REQUIRE_FALSE(r);
    // Left the existing file in place.
    std::ifstream in(out, std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(in)), {});
    CHECK(content == "already here");
}

TEST_CASE("list-releases --unstable lists the CI's image artifacts") {
    auto rig = makeRig();
    rig.http->routes = {
        {"/runs?",
         R"({"workflow_runs":[
             {"id":42,"head_branch":"main","head_sha":"abcdef1234567890"}]})"},
        {"/artifacts",
         R"({"artifacts":[
             {"id":100,"name":"kuiper_full_64_image.zip","size_in_bytes":2000000000,
              "expired":false,"expires_at":"2026-12-01T00:00:00Z",
              "archive_download_url":"https://x/100/zip"},
             {"id":101,"name":"kuiper_basic_32_image.zip","size_in_bytes":1000000000,
              "expired":false,"archive_download_url":"https://x/101/zip"},
             {"id":102,"name":"kuiper_full_64_meta.zip","size_in_bytes":1024,
              "expired":false}]})"},
    };

    ReleaseQuery q;
    q.channel = "unstable";
    auto r = rig.service.listReleases(q);
    REQUIRE_MESSAGE(r, (r ? "" : r.error().message));

    REQUIRE(r->size() == 2);  // the *_meta companion is dropped
    const Release* full = nullptr;
    const Release* basic = nullptr;
    for (const auto& rel : *r) {
        if (rel.id == "gh:100") full = &rel;
        if (rel.id == "gh:101") basic = &rel;
    }
    REQUIRE(full);
    REQUIRE(basic);

    CHECK(full->variant == "full");
    CHECK(full->arch == "arm64");
    CHECK(full->channel == "unstable");
    CHECK(full->branch == "main");
    CHECK(full->commit == "abcdef1");
    CHECK(full->available);
    REQUIRE(full->expiresAt.has_value());
    CHECK(*full->expiresAt == "2026-12-01T00:00:00Z");

    CHECK(basic->variant == "basic");
    CHECK(basic->arch == "arm32");
}
