// ConfigurationService::listProjects / findProject over a filesystem temp dir.
// Portable (path-based, no device); manifests are just *.json files in the tree.

#include "doctest_kuiper.hpp"

#include <string>

#include "kuiper/ConfigurationService.hpp"

#include "support.hpp"

using namespace kuiper;

namespace {

const char* kManifest = R"({
  "projects": [
    {
      "name": "ad9081",
      "board": "vck190",
      "platform": "xilinx",
      "architecture": "versal",
      "kernel": "/boot/ad9081/vck190/Image",
      "files": [ { "path": "/boot/ad9081/vck190/system.dtb" } ]
    }
  ]
})";

}  // namespace

TEST_CASE("listProjects errors on a missing directory") {
    ConfigurationService cfg;
    auto r = cfg.listProjects("/nonexistent/boot/path");
    CHECK_FALSE(r);
}

TEST_CASE("listProjects returns an empty list for a boot dir with no manifests") {
    kuiper::test::TempDir tmp;
    ConfigurationService cfg;
    auto r = cfg.listProjects(tmp.path().string());
    REQUIRE(r);
    CHECK(r->empty());
}

TEST_CASE("listProjects collects a project from a manifest") {
    kuiper::test::TempDir tmp;
    kuiper::test::writeText(tmp.file("projects.json"), kManifest);

    ConfigurationService cfg;
    auto r = cfg.listProjects(tmp.path().string());
    REQUIRE(r);
    REQUIRE(r->size() == 1);
    CHECK((*r)[0].name == "ad9081");
    CHECK((*r)[0].board == "vck190");
    CHECK((*r)[0].platform == "xilinx");
    CHECK((*r)[0].files.size() == 1);
}

TEST_CASE("malformed and nameless manifest entries are skipped silently") {
    kuiper::test::TempDir tmp;
    kuiper::test::writeText(tmp.file("bad.json"), "{ this is not json ]");
    kuiper::test::writeText(
        tmp.file("nameless.json"),
        R"({"projects":[{"board":"vck190"}]})");  // no name -> dropped

    ConfigurationService cfg;
    auto r = cfg.listProjects(tmp.path().string());
    REQUIRE(r);
    CHECK(r->empty());
}

TEST_CASE("findProject matches on name and board, else NotFound") {
    kuiper::test::TempDir tmp;
    kuiper::test::writeText(tmp.file("projects.json"), kManifest);

    ConfigurationService cfg;
    auto found = cfg.findProject(tmp.path().string(), "ad9081", "vck190");
    REQUIRE(found);
    CHECK(found->name == "ad9081");

    auto missing = cfg.findProject(tmp.path().string(), "ad9081", "zcu102");
    REQUIRE_FALSE(missing);
    CHECK_ERRCODE(missing.error().code, ErrorCode::NotFound);
}
