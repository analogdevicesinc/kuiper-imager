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
