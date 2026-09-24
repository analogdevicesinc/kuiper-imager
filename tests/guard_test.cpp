// The two-tier write-target guard, exercised through DriveService::flash and
// ::writePreloader (its only entry points). classifyWriteTarget itself is
// TU-local; these cases pin its exact behaviour and messages. See docs: flash.

#include "doctest_kuiper.hpp"

#include <memory>
#include <string>
#include <utility>

#include "kuiper/DriveService.hpp"

#include "fakes/FakeDriveBackend.hpp"
#include "support.hpp"

using namespace kuiper;
using kuiper::test::FakeDeviceState;
using kuiper::test::FakeDriveBackend;

namespace {

// A removable, non-system drive with one (unmounted) vfat partition.
Drive makeCard(const std::string& node, const std::string& partNode) {
    Drive d;
    d.node = node;
    d.isRemovable = true;
    d.isSystem = false;
    Partition p;
    p.node = partNode;
    p.fsType = "vfat";
    d.partitions.push_back(p);
    return d;
}

DriveService serviceWith(DriveList drives) {
    auto backend = std::make_unique<FakeDriveBackend>();
    backend->setDrives(std::move(drives));
    // No device: any open fails, so the guard is what these tests observe first.
    return DriveService(std::move(backend));
}

}  // namespace

TEST_CASE("flash refuses the system disk even under --force") {
    Drive sys = makeCard("/dev/sda", "/dev/sda1");
    sys.isSystem = true;
    auto svc = serviceWith({sys});

    for (bool force : {false, true}) {
        auto r = svc.flash("/dev/sda", "/nonexistent.img", {.force = force});
        REQUIRE_FALSE(r);
        CHECK_ERRCODE(r.error().code, ErrorCode::PermissionDenied);
        CHECK(r.error().message.find("system disk") != std::string::npos);
    }
}

TEST_CASE("flash refuses a non-removable drive without --force") {
    Drive fixed = makeCard("/dev/sdb", "/dev/sdb1");
    fixed.isRemovable = false;
    auto svc = serviceWith({fixed});

    auto blocked = svc.flash("/dev/sdb", "/nonexistent.img", {.force = false});
    REQUIRE_FALSE(blocked);
    CHECK_ERRCODE(blocked.error().code, ErrorCode::PermissionDenied);
    CHECK(blocked.error().message.find("non-removable") != std::string::npos);

    // With --force the guard clears; the failure now comes from the missing image.
    auto forced = svc.flash("/dev/sdb", "/nonexistent.img", {.force = true});
    REQUIRE_FALSE(forced);
    CHECK_ERRCODE(forced.error().code, ErrorCode::InvalidImage);
}

TEST_CASE("flash reports a not-found drive as DeviceRemoved") {
    auto svc = serviceWith({makeCard("/dev/sda", "/dev/sda1")});

    auto missing = svc.flash("/dev/zzz", "/nonexistent.img", {.force = false});
    REQUIRE_FALSE(missing);
    CHECK_ERRCODE(missing.error().code, ErrorCode::DeviceRemoved);
    CHECK(missing.error().message.find("Drive not found") != std::string::npos);

    // --force lets an unknown node through (loop devices); it then fails on the
    // image, not the guard.
    auto forced = svc.flash("/dev/zzz", "/nonexistent.img", {.force = true});
    REQUIRE_FALSE(forced);
    CHECK_ERRCODE(forced.error().code, ErrorCode::InvalidImage);
}

TEST_CASE("writePreloader refuses a mounted partition without --force") {
    kuiper::test::TempDir tmp;
    const std::string blob =
        kuiper::test::writeText(tmp.file("preloader.bin"), "PRELOADER");

    Drive card = makeCard("/dev/sdb", "/dev/sdb1");
    card.partitions[0].mountpoint = "/media/boot";
    auto svc = serviceWith({card});

    auto r = svc.writePreloader("/dev/sdb1", blob, {.force = false});
    REQUIRE_FALSE(r);
    CHECK_ERRCODE(r.error().code, ErrorCode::PermissionDenied);
    CHECK(r.error().message.find("mounted") != std::string::npos);
}

TEST_CASE("writePreloader refuses the system disk even under --force") {
    kuiper::test::TempDir tmp;
    const std::string blob =
        kuiper::test::writeText(tmp.file("preloader.bin"), "PRELOADER");

    Drive sys = makeCard("/dev/sda", "/dev/sda1");
    sys.isSystem = true;
    auto svc = serviceWith({sys});

    auto r = svc.writePreloader("/dev/sda1", blob, {.force = true});
    REQUIRE_FALSE(r);
    CHECK_ERRCODE(r.error().code, ErrorCode::PermissionDenied);
    CHECK(r.error().message.find("system disk") != std::string::npos);
}

TEST_CASE("writePreloader reports an unknown partition as PermissionDenied") {
    kuiper::test::TempDir tmp;
    const std::string blob =
        kuiper::test::writeText(tmp.file("preloader.bin"), "PRELOADER");
    auto svc = serviceWith({makeCard("/dev/sdb", "/dev/sdb1")});

    auto r = svc.writePreloader("/dev/zzz1", blob, {.force = false});
    REQUIRE_FALSE(r);
    CHECK_ERRCODE(r.error().code, ErrorCode::PermissionDenied);
    CHECK(r.error().message.find("unknown disk") != std::string::npos);
}
