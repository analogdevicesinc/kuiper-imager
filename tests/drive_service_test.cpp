// DriveService::flash driven end to end against an in-memory device: the real
// decompress/write/verify pipeline, no hardware. Covers the happy path, the size
// guard, verify-mismatch, defer-head ordering, cancellation, and --no-verify.

#include "doctest_kuiper.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "kuiper/DriveService.hpp"

#include "fakes/FakeDriveBackend.hpp"
#include "support.hpp"

using namespace kuiper;
using kuiper::test::FakeDeviceState;
using kuiper::test::FakeDriveBackend;

namespace {

constexpr std::uint64_t kDeferHead = 1u << 20;  // must track DriveService.cpp

// A removable, non-system drive, so the target guard clears.
Drive card(const std::string& node) {
    Drive d;
    d.node = node;
    d.isRemovable = true;
    d.isSystem = false;
    return d;
}

// The service plus the device's shared state, so a test can inspect the media
// after DriveService has closed the handle.
struct Rig {
    DriveService service;
    std::shared_ptr<FakeDeviceState> state;
};

Rig makeRig(std::size_t deviceSize) {
    auto state = std::make_shared<FakeDeviceState>();
    state->store.assign(deviceSize, std::byte{0});

    auto backend = std::make_unique<FakeDriveBackend>();
    backend->setDrives({card("/dev/fake0")});
    backend->setDevice(state);
    return Rig{DriveService(std::move(backend)), std::move(state)};
}

}  // namespace

TEST_CASE("flash writes every byte and reports them") {
    kuiper::test::TempDir tmp;
    const auto image = kuiper::test::makeImageBytes(2u << 20);
    const std::string imgPath =
        kuiper::test::writeBytes(tmp.file("kuiper.img"), image);

    auto rig = makeRig(4u << 20);
    auto r = rig.service.flash("/dev/fake0", imgPath, {.force = false});

    REQUIRE_MESSAGE(r, (r ? "" : r.error().message));
    CHECK(r->bytesWritten == image.size());
    CHECK(r->sha256.size() == 64);

    // Image written verbatim; the tail past it is left untouched.
    const auto& store = rig.state->store;
    REQUIRE(store.size() >= image.size());
    bool prefixMatches = std::equal(image.begin(), image.end(), store.begin());
    CHECK(prefixMatches);
    CHECK(rig.state->wiped);
}

TEST_CASE("flash commits the partition table last (defer-head ordering)") {
    kuiper::test::TempDir tmp;
    const auto image = kuiper::test::makeImageBytes(2u << 20);
    const std::string imgPath =
        kuiper::test::writeBytes(tmp.file("kuiper.img"), image);

    auto rig = makeRig(4u << 20);
    REQUIRE(rig.service.flash("/dev/fake0", imgPath, {.force = false}));

    const auto& log = rig.state->writeLog;
    REQUIRE_FALSE(log.empty());
    // The head (offset 0) is the final write; everything before it is body,
    // written at or beyond the deferred head offset.
    CHECK(log.back().first == 0);
    CHECK(log.back().second == kDeferHead);
    for (std::size_t i = 0; i + 1 < log.size(); ++i) {
        CHECK(log[i].first >= kDeferHead);
    }
}

TEST_CASE("flash rejects an image larger than the device") {
    kuiper::test::TempDir tmp;
    const auto image = kuiper::test::makeImageBytes(2u << 20);
    const std::string imgPath =
        kuiper::test::writeBytes(tmp.file("big.img"), image);

    auto rig = makeRig(1u << 20);  // device smaller than the image
    auto r = rig.service.flash("/dev/fake0", imgPath, {.force = false});

    REQUIRE_FALSE(r);
    CHECK_ERRCODE(r.error().code, ErrorCode::DiskFull);
    CHECK(r.error().message.find("larger than the target") != std::string::npos);
}

TEST_CASE("flash fails verification when the device body is corrupted") {
    kuiper::test::TempDir tmp;
    const auto image = kuiper::test::makeImageBytes(2u << 20);
    const std::string imgPath =
        kuiper::test::writeBytes(tmp.file("kuiper.img"), image);

    auto rig = makeRig(4u << 20);
    rig.state->corruptOffset = kDeferHead + 4096;  // flip a byte in the body

    auto r = rig.service.flash("/dev/fake0", imgPath, {.force = false});
    REQUIRE_FALSE(r);
    CHECK_ERRCODE(r.error().code, ErrorCode::HashMismatch);
    CHECK(r.error().message.find("body differs") != std::string::npos);
}

TEST_CASE("flash with verify=false skips the body read-back") {
    kuiper::test::TempDir tmp;
    const auto image = kuiper::test::makeImageBytes(2u << 20);
    const std::string imgPath =
        kuiper::test::writeBytes(tmp.file("kuiper.img"), image);

    auto rig = makeRig(4u << 20);
    rig.state->corruptOffset = kDeferHead + 4096;  // would fail a verify

    auto r = rig.service.flash("/dev/fake0", imgPath, {.verify = false});
    REQUIRE_MESSAGE(r, (r ? "" : r.error().message));
    CHECK(r->bytesWritten == image.size());
}

TEST_CASE("flash aborts on a cancel token") {
    kuiper::test::TempDir tmp;
    const auto image = kuiper::test::makeImageBytes(2u << 20);
    const std::string imgPath =
        kuiper::test::writeBytes(tmp.file("kuiper.img"), image);

    auto rig = makeRig(4u << 20);
    CancelToken cancel{[] { return true; }};
    auto r = rig.service.flash("/dev/fake0", imgPath, {}, {}, cancel);

    REQUIRE_FALSE(r);
    CHECK_ERRCODE(r.error().code, ErrorCode::UserCancelled);
}
