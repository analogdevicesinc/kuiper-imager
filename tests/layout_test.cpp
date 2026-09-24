// identifyLayout — pure role classification by inspection, never by index (the
// fix for the two-card bug). See docs: core-library (layout identification).

#include "doctest_kuiper.hpp"

#include "kuiper/Drive.hpp"
#include "kuiper/Layout.hpp"

using namespace kuiper;

namespace {

Partition part(const std::string& node, const std::string& fsType,
               const std::string& label, std::uint64_t size = 0) {
    Partition p;
    p.node = node;
    p.fsType = fsType;
    p.label = label;
    p.sizeBytes = size;
    return p;
}

}  // namespace

TEST_CASE("a blank card is not a Kuiper card") {
    Drive d;
    d.node = "/dev/sda";
    auto r = identifyLayout(d);
    REQUIRE_FALSE(r);
    CHECK_ERRCODE(r.error().code, ErrorCode::NotKuiper2);
}

TEST_CASE("boot/root/bootloader are classified by filesystem and label") {
    Drive d;
    d.node = "/dev/mmcblk0";
    d.partitions.push_back(part("/dev/mmcblk0p1", "vfat", "BOOT", 256u << 20));
    d.partitions.push_back(part("/dev/mmcblk0p2", "ext4", "rootfs", 2u << 30));
    d.partitions.push_back(part("/dev/mmcblk0p3", "", "", 4u << 20));

    auto r = identifyLayout(d);
    REQUIRE(r);
    REQUIRE(r->boot);
    CHECK(r->boot->node == "/dev/mmcblk0p1");
    REQUIRE(r->root);
    CHECK(r->root->node == "/dev/mmcblk0p2");
    REQUIRE(r->bootloader);
    CHECK(r->bootloader->node == "/dev/mmcblk0p3");
}

TEST_CASE("classification is case-insensitive and ignores partition order") {
    Drive d;
    d.node = "/dev/sdb";
    // Deliberately out of order, mixed case: role must follow inspection.
    d.partitions.push_back(part("/dev/sdb1", "ext4", "ROOTFS"));
    d.partitions.push_back(part("/dev/sdb2", "vfat", "boot"));

    auto r = identifyLayout(d);
    REQUIRE(r);
    REQUIRE(r->boot);
    CHECK(r->boot->node == "/dev/sdb2");
    REQUIRE(r->root);
    CHECK(r->root->node == "/dev/sdb1");
}

TEST_CASE("the smallest unformatted partition is the preloader slot") {
    Drive d;
    d.node = "/dev/sdc";
    d.partitions.push_back(part("/dev/sdc1", "vfat", "BOOT"));
    d.partitions.push_back(part("/dev/sdc2", "", "", 64u << 20));  // data area
    d.partitions.push_back(part("/dev/sdc3", "", "", 1u << 20));   // preloader

    auto r = identifyLayout(d);
    REQUIRE(r);
    REQUIRE(r->bootloader);
    CHECK(r->bootloader->node == "/dev/sdc3");
}
