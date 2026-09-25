// DriveService::capabilities / isSupported — the surface a front-end gates on.

#include "doctest_kuiper.hpp"

#include <memory>
#include <utility>

#include "kuiper/DriveService.hpp"

#include "fakes/FakeDriveBackend.hpp"

using namespace kuiper;
using kuiper::test::FakeDriveBackend;

TEST_CASE("no backend reports nothing supported") {
    DriveService svc(std::unique_ptr<IDriveBackend>{});
    CHECK_FALSE(svc.isSupported());
    CHECK_FALSE(svc.capabilities().enumerate);
    CHECK_FALSE(svc.capabilities().flash);
    CHECK_FALSE(svc.capabilities().mount);
}

TEST_CASE("an all-false backend is unsupported like a platform stub") {
    auto backend = std::make_unique<FakeDriveBackend>();
    backend->setCapabilities({});  // enumerate/flash/mount all false
    DriveService svc(std::move(backend));

    CHECK_FALSE(svc.isSupported());
    CHECK_FALSE(svc.capabilities().flash);
}

TEST_CASE("a fully-capable backend is supported") {
    auto backend = std::make_unique<FakeDriveBackend>();
    backend->setCapabilities({.enumerate = true, .flash = true, .mount = true});
    DriveService svc(std::move(backend));

    CHECK(svc.isSupported());
    CHECK(svc.capabilities().enumerate);
    CHECK(svc.capabilities().flash);
    CHECK(svc.capabilities().mount);
}

TEST_CASE("isSupported keys off enumerate, not flash") {
    auto backend = std::make_unique<FakeDriveBackend>();
    backend->setCapabilities({.enumerate = true, .flash = false, .mount = false});
    DriveService svc(std::move(backend));

    CHECK(svc.isSupported());
    CHECK_FALSE(svc.capabilities().flash);
}
