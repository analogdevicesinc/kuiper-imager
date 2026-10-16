// Windows drive backend. Enumeration via SetupAPI + storage IOCTLs; raw I/O and
// the configure/mount path land in later commits. See docs: platform-backends.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
//
#include <setupapi.h>
#include <winioctl.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "kuiper/platform/IDriveBackend.hpp"
#include "platform/common/AlignedRawDevice.hpp"
#include "platform/common/BlockIo.hpp"

namespace kuiper {
namespace {

// GUID_DEVINTERFACE_DISK {53F56307-B6BF-11D0-94F2-00A0C91EFB8B}. Defined inline so
// the TU needs no <initguid.h> ordering dance.
const GUID kDiskInterfaceGuid = {
    0x53f56307,
    0xb6bf,
    0x11d0,
    {0x94, 0xf2, 0x00, 0xa0, 0xc9, 0x1e, 0xfb, 0x8b}};

// UTF-16 -> UTF-8.
std::string narrow(const wchar_t* w) {
    if (!w || !*w) return {};
    const int n =
        ::WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return {};
    std::string s(static_cast<std::size_t>(n - 1), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

// UTF-8 -> UTF-16.
std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n =
        ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (n <= 1) return {};
    std::wstring w(static_cast<std::size_t>(n - 1), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}

std::string trim(std::string s) {
    const auto notspace = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notspace));
    s.erase(std::find_if(s.rbegin(), s.rend(), notspace).base(), s.end());
    return s;
}

// Map a Win32 error onto the domain vocabulary. Shared by every Windows op.
std::unexpected<Error> winError(DWORD err, const std::string& what) {
    switch (err) {
        case ERROR_ACCESS_DENIED:
            return Err(ErrorCode::PermissionDenied, what,
                       "Run from an elevated prompt (Administrator)");
        case ERROR_SHARING_VIOLATION:
        case ERROR_LOCK_VIOLATION:
        case ERROR_BUSY:
            return Err(ErrorCode::DeviceBusy, what,
                       "Close programs using the disk, then retry");
        case ERROR_DISK_FULL:
            return Err(ErrorCode::DiskFull, what);
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:
        case ERROR_DEV_NOT_EXIST:
        case ERROR_NO_MEDIA_IN_DRIVE:
            return Err(ErrorCode::DeviceRemoved, what);
        default: {
            char msg[256] = {0};
            ::FormatMessageA(
                FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                nullptr, err, 0, msg, sizeof(msg) - 1, nullptr);
            return Err(ErrorCode::Unknown, what, trim(msg));
        }
    }
}

std::string physicalDriveNode(int n) {
    return "\\\\.\\PhysicalDrive" + std::to_string(n);
}

// RAII HANDLE.
struct Handle {
    HANDLE h = INVALID_HANDLE_VALUE;
    Handle() = default;
    explicit Handle(HANDLE v) : h(v) {}
    ~Handle() {
        if (h != INVALID_HANDLE_VALUE) ::CloseHandle(h);
    }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& o) noexcept : h(o.h) { o.h = INVALID_HANDLE_VALUE; }
    bool valid() const { return h != INVALID_HANDLE_VALUE; }
    HANDLE get() const { return h; }
};

// One mounted volume mapped to its backing disk, for partition enrichment.
struct VolumeInfo {
    std::wstring guidPath;        // "\\?\Volume{...}\"  (trailing backslash)
    DWORD diskNumber = 0;         // physical drive it lives on
    std::int64_t startingOffset = 0;
    std::string driveLetter;     // "E:\"  (mountpoint), empty if none
    std::string fsType;          // "vfat" | "ntfs" | ...  ("" if unreadable)
    std::string label;
};

// IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS on a volume handle -> (disk, offset).
// Only simple single-extent volumes are mapped (the Kuiper card case).
bool volumeExtent(HANDLE h, DWORD& disk, std::int64_t& offset) {
    VOLUME_DISK_EXTENTS vde{};
    DWORD br = 0;
    if (!::DeviceIoControl(h, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, nullptr, 0,
                           &vde, sizeof(vde), &br, nullptr)) {
        return false;  // ERROR_MORE_DATA => spanned volume, skip (not a card)
    }
    if (vde.NumberOfDiskExtents < 1) return false;
    disk = vde.Extents[0].DiskNumber;
    offset = vde.Extents[0].StartingOffset.QuadPart;
    return true;
}

std::string normalizeFs(const std::string& fs) {
    std::string low = fs;
    for (char& c : low) c = static_cast<char>(std::tolower((unsigned char)c));
    if (low == "fat" || low == "fat32" || low == "fat16") return "vfat";
    return low;
}

// Enumerate every volume and map each to its backing disk + offset.
std::vector<VolumeInfo> enumerateVolumes() {
    std::vector<VolumeInfo> out;
    wchar_t name[MAX_PATH];
    HANDLE find = ::FindFirstVolumeW(name, MAX_PATH);
    if (find == INVALID_HANDLE_VALUE) return out;
    do {
        std::wstring guid = name;  // "\\?\Volume{...}\"
        // Open without the trailing backslash for the extents IOCTL.
        std::wstring dev = guid;
        if (!dev.empty() && dev.back() == L'\\') dev.pop_back();
        Handle vh(::CreateFileW(dev.c_str(), 0,
                                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                OPEN_EXISTING, 0, nullptr));
        if (!vh.valid()) continue;

        VolumeInfo vi;
        vi.guidPath = guid;
        if (!volumeExtent(vh.get(), vi.diskNumber, vi.startingOffset)) continue;

        // Drive letter / mountpoint.
        DWORD needed = 0;
        ::GetVolumePathNamesForVolumeNameW(guid.c_str(), nullptr, 0, &needed);
        if (needed) {
            std::vector<wchar_t> paths(needed);
            if (::GetVolumePathNamesForVolumeNameW(guid.c_str(), paths.data(),
                                                   needed, &needed)) {
                vi.driveLetter = narrow(paths.data());  // first path
            }
        }

        // Filesystem type + label (fails for RAW/ext volumes -> left empty).
        wchar_t fsName[MAX_PATH] = {0};
        wchar_t label[MAX_PATH] = {0};
        if (::GetVolumeInformationW(guid.c_str(), label, MAX_PATH, nullptr,
                                    nullptr, nullptr, fsName, MAX_PATH)) {
            vi.fsType = normalizeFs(narrow(fsName));
            vi.label = narrow(label);
        }
        out.push_back(std::move(vi));
    } while (::FindNextVolumeW(find, name, MAX_PATH));
    ::FindVolumeClose(find);
    return out;
}

// Physical-drive number backing the system volume (%SystemDrive%), or -1.
int systemDiskNumber() {
    wchar_t win[MAX_PATH] = {0};
    if (::GetWindowsDirectoryW(win, MAX_PATH) == 0) return -1;
    std::wstring vol = L"\\\\.\\X:";
    vol[4] = win[0];  // drive letter
    Handle h(::CreateFileW(vol.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, 0, nullptr));
    if (!h.valid()) return -1;
    DWORD disk = 0;
    std::int64_t off = 0;
    if (!volumeExtent(h.get(), disk, off)) return -1;
    return static_cast<int>(disk);
}

bool queryDescriptor(HANDLE h, std::string& model, STORAGE_BUS_TYPE& bus,
                     bool& removableMedia) {
    STORAGE_PROPERTY_QUERY q{};
    q.PropertyId = StorageDeviceProperty;
    q.QueryType = PropertyStandardQuery;
    std::vector<BYTE> buf(1024);
    DWORD br = 0;
    if (!::DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, &q, sizeof(q),
                           buf.data(), static_cast<DWORD>(buf.size()), &br,
                           nullptr)) {
        return false;
    }
    auto* d = reinterpret_cast<STORAGE_DEVICE_DESCRIPTOR*>(buf.data());
    bus = static_cast<STORAGE_BUS_TYPE>(d->BusType);
    removableMedia = d->RemovableMedia != 0;
    std::string vendor, product;
    if (d->VendorIdOffset && d->VendorIdOffset < buf.size()) {
        vendor = reinterpret_cast<const char*>(buf.data() + d->VendorIdOffset);
    }
    if (d->ProductIdOffset && d->ProductIdOffset < buf.size()) {
        product = reinterpret_cast<const char*>(buf.data() + d->ProductIdOffset);
    }
    model = trim(trim(vendor) + " " + trim(product));
    return true;
}

// Exact capacity. IOCTL_DISK_GET_LENGTH_INFO needs FILE_READ_ACCESS, so this is
// only usable on a handle opened with read access (the write/verify session).
std::uint64_t queryLength(HANDLE h) {
    GET_LENGTH_INFORMATION li{};
    DWORD br = 0;
    if (::DeviceIoControl(h, IOCTL_DISK_GET_LENGTH_INFO, nullptr, 0, &li,
                          sizeof(li), &br, nullptr)) {
        return static_cast<std::uint64_t>(li.Length.QuadPart);
    }
    return 0;
}

// Capacity via the drive geometry. IOCTL_DISK_GET_DRIVE_GEOMETRY_EX is
// FILE_ANY_ACCESS, so unlike GET_LENGTH_INFO it works on the zero-access query
// handle used by listDrives (no admin needed just to enumerate).
std::uint64_t queryGeometrySize(HANDLE h) {
    DISK_GEOMETRY_EX geo{};
    DWORD br = 0;
    if (::DeviceIoControl(h, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, nullptr, 0, &geo,
                          sizeof(geo), &br, nullptr)) {
        return static_cast<std::uint64_t>(geo.DiskSize.QuadPart);
    }
    return 0;
}

struct RawPartition {
    int number = 0;
    std::int64_t offset = 0;
    std::uint64_t size = 0;
};

std::vector<RawPartition> queryLayout(HANDLE h) {
    std::vector<RawPartition> parts;
    std::vector<BYTE> buf(sizeof(DRIVE_LAYOUT_INFORMATION_EX) +
                          128 * sizeof(PARTITION_INFORMATION_EX));
    DWORD br = 0;
    if (!::DeviceIoControl(h, IOCTL_DISK_GET_DRIVE_LAYOUT_EX, nullptr, 0,
                           buf.data(), static_cast<DWORD>(buf.size()), &br,
                           nullptr)) {
        return parts;
    }
    auto* layout = reinterpret_cast<DRIVE_LAYOUT_INFORMATION_EX*>(buf.data());
    for (DWORD i = 0; i < layout->PartitionCount; ++i) {
        const PARTITION_INFORMATION_EX& pe = layout->PartitionEntry[i];
        if (pe.PartitionNumber == 0) continue;  // not a real entry
        if (pe.PartitionStyle == PARTITION_STYLE_MBR &&
            pe.Mbr.PartitionType == PARTITION_ENTRY_UNUSED) {
            continue;
        }
        RawPartition rp;
        rp.number = static_cast<int>(pe.PartitionNumber);
        rp.offset = pe.StartingOffset.QuadPart;
        rp.size = static_cast<std::uint64_t>(pe.PartitionLength.QuadPart);
        parts.push_back(rp);
    }
    return parts;
}

// Trailing integer of a "\\.\PhysicalDriveN" node, or -1.
int driveNumberOf(const std::string& node) {
    std::size_t i = node.size();
    while (i > 0 && std::isdigit(static_cast<unsigned char>(node[i - 1]))) --i;
    if (i == node.size()) return -1;
    return std::stoi(node.substr(i));
}

constexpr int kLockRetries = 20;    // ~2 s total
constexpr DWORD kLockSleepMs = 100;

// One open raw-device session on Windows: owns the physical-drive handle and the
// locked volume handles (closing a volume handle releases its lock), and does
// positioned aligned I/O. The thin BlockIo primitive beneath AlignedRawDevice.
class WindowsBlockIo final : public detail::BlockIo {
public:
    WindowsBlockIo(Handle disk, std::vector<Handle> locks, unsigned blockSize,
                   std::uint64_t size)
        : disk_(std::move(disk)),
          locks_(std::move(locks)),
          blockSize_(blockSize),
          devSize_(size) {}

    unsigned blockSize() const noexcept override { return blockSize_; }
    std::uint64_t deviceSize() const noexcept override { return devSize_; }

    Result<std::size_t> pread(std::byte* buf, std::size_t len,
                              std::uint64_t off) override {
        std::size_t done = 0;
        while (done < len) {
            OVERLAPPED ov{};
            const std::uint64_t pos = off + done;
            ov.Offset = static_cast<DWORD>(pos & 0xFFFFFFFFu);
            ov.OffsetHigh = static_cast<DWORD>(pos >> 32);
            const DWORD chunk = static_cast<DWORD>(
                std::min<std::size_t>(len - done, 0x7FFF0000u));
            DWORD got = 0;
            if (!::ReadFile(disk_.get(), buf + done, chunk, &got, &ov)) {
                const DWORD e = ::GetLastError();
                if (e == ERROR_HANDLE_EOF) break;
                return winError(e, "Read from device failed");
            }
            if (got == 0) break;  // EOF
            done += got;
        }
        return done;
    }

    Result<void> pwrite(const std::byte* buf, std::size_t len,
                        std::uint64_t off) override {
        std::size_t done = 0;
        while (done < len) {
            OVERLAPPED ov{};
            const std::uint64_t pos = off + done;
            ov.Offset = static_cast<DWORD>(pos & 0xFFFFFFFFu);
            ov.OffsetHigh = static_cast<DWORD>(pos >> 32);
            const DWORD chunk = static_cast<DWORD>(
                std::min<std::size_t>(len - done, 0x7FFF0000u));
            DWORD wrote = 0;
            if (!::WriteFile(disk_.get(), buf + done, chunk, &wrote, &ov)) {
                return winError(::GetLastError(), "Write to device failed");
            }
            if (wrote == 0) {
                return Err(ErrorCode::Unknown, "Write returned zero bytes");
            }
            done += wrote;
        }
        return {};
    }

    Result<void> sync() override {
        if (!::FlushFileBuffers(disk_.get())) {
            return winError(::GetLastError(), "FlushFileBuffers failed");
        }
        return {};
    }

    // FILE_FLAG_NO_BUFFERING already bypasses the cache, so nothing to drop.
    void dropCache() override {}

    void rereadPartTable() override {
        DWORD br = 0;
        ::DeviceIoControl(disk_.get(), IOCTL_DISK_UPDATE_PROPERTIES, nullptr, 0,
                          nullptr, 0, &br, nullptr);  // best effort
    }

private:
    Handle disk_;
    std::vector<Handle> locks_;  // held for the session; close releases the lock
    unsigned blockSize_ = 512;
    std::uint64_t devSize_ = 0;
};

unsigned queryBlockSize(HANDLE h) {
    STORAGE_PROPERTY_QUERY q{};
    q.PropertyId = StorageAccessAlignmentProperty;
    q.QueryType = PropertyStandardQuery;
    STORAGE_ACCESS_ALIGNMENT_DESCRIPTOR ad{};
    DWORD br = 0;
    if (::DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, &q, sizeof(q), &ad,
                          sizeof(ad), &br, nullptr) &&
        ad.BytesPerLogicalSector > 0) {
        return ad.BytesPerLogicalSector;
    }
    DISK_GEOMETRY geo{};
    if (::DeviceIoControl(h, IOCTL_DISK_GET_DRIVE_GEOMETRY, nullptr, 0, &geo,
                          sizeof(geo), &br, nullptr) &&
        geo.BytesPerSector > 0) {
        return geo.BytesPerSector;
    }
    return 512;
}

// Lock + dismount every volume on disk `driveNo`, holding the lock handles. A
// volume that cannot be locked after retries => DeviceBusy.
Result<std::vector<Handle>> lockVolumes(int driveNo) {
    std::vector<Handle> locks;
    for (const VolumeInfo& v : enumerateVolumes()) {
        if (static_cast<int>(v.diskNumber) != driveNo) continue;
        std::wstring dev = v.guidPath;
        if (!dev.empty() && dev.back() == L'\\') dev.pop_back();
        Handle vh(::CreateFileW(dev.c_str(), GENERIC_READ | GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                OPEN_EXISTING, 0, nullptr));
        if (!vh.valid()) continue;  // vanished between enumerate and open

        bool locked = false;
        for (int i = 0; i < kLockRetries; ++i) {
            DWORD br = 0;
            if (::DeviceIoControl(vh.get(), FSCTL_LOCK_VOLUME, nullptr, 0,
                                  nullptr, 0, &br, nullptr)) {
                locked = true;
                break;
            }
            ::Sleep(kLockSleepMs);
        }
        if (!locked) {
            return Err(ErrorCode::DeviceBusy,
                       "A volume on the disk is in use: " + v.driveLetter,
                       "Close programs using it, then retry");
        }
        DWORD br = 0;
        ::DeviceIoControl(vh.get(), FSCTL_DISMOUNT_VOLUME, nullptr, 0, nullptr,
                          0, &br, nullptr);  // best effort
        locks.push_back(std::move(vh));
    }
    return locks;
}

// Open a raw-device session on `node` (a \\.\PhysicalDriveN), locking its
// volumes so the write/verify has exclusive access. Wraps the native handle in
// the shared AlignedRawDevice engine.
Result<std::unique_ptr<IRawDevice>> openSession(const std::string& node,
                                                bool write) {
    const int driveNo = driveNumberOf(node);
    if (driveNo < 0) {
        return Err(ErrorCode::InvalidImage,
                   "Not a physical drive: " + node,
                   "--drive must be a whole drive like \\\\.\\PhysicalDrive1");
    }

    auto locks = lockVolumes(driveNo);
    if (!locks) return std::unexpected(locks.error());

    const DWORD access = write ? (GENERIC_READ | GENERIC_WRITE) : GENERIC_READ;
    Handle disk(::CreateFileW(widen(node).c_str(), access,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_EXISTING,
                              FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH,
                              nullptr));
    if (!disk.valid()) {
        return winError(::GetLastError(),
                        "Cannot open device for " +
                            std::string(write ? "writing" : "reading") + ": " +
                            node);
    }

    DWORD br = 0;
    ::DeviceIoControl(disk.get(), FSCTL_ALLOW_EXTENDED_DASD_IO, nullptr, 0,
                      nullptr, 0, &br, nullptr);  // best effort

    const unsigned bs = queryBlockSize(disk.get());
    const std::uint64_t size = queryLength(disk.get());

    auto io = std::make_unique<WindowsBlockIo>(std::move(disk), std::move(*locks),
                                               bs, size);
    auto dev = std::make_unique<detail::AlignedRawDevice>(std::move(io));
    if (auto r = dev->init(); !r) return std::unexpected(r.error());
    return std::move(dev);
}

}  // namespace

namespace {

class WindowsDriveBackend final : public IDriveBackend {
public:
    const char* name() const noexcept override { return "windows"; }

    DriveCapabilities capabilities() const noexcept override {
        // Enumerate + flash work; mount (configure) is out of scope on Windows.
        return {.enumerate = true, .flash = true, .mount = false};
    }

    Result<DriveList> listDrives() override {
        HDEVINFO di = ::SetupDiGetClassDevsW(
            &kDiskInterfaceGuid, nullptr, nullptr,
            DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
        if (di == INVALID_HANDLE_VALUE) {
            return winError(::GetLastError(),
                            "Cannot enumerate disks (SetupDiGetClassDevs)");
        }

        const std::vector<VolumeInfo> volumes = enumerateVolumes();
        const int sysDisk = systemDiskNumber();

        DriveList drives;
        SP_DEVICE_INTERFACE_DATA ifd{};
        ifd.cbSize = sizeof(ifd);
        for (DWORD idx = 0; ::SetupDiEnumDeviceInterfaces(
                 di, nullptr, &kDiskInterfaceGuid, idx, &ifd);
             ++idx) {
            DWORD needed = 0;
            ::SetupDiGetDeviceInterfaceDetailW(di, &ifd, nullptr, 0, &needed,
                                               nullptr);
            if (needed == 0) continue;
            std::vector<BYTE> detailBuf(needed);
            auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(
                detailBuf.data());
            detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
            if (!::SetupDiGetDeviceInterfaceDetailW(di, &ifd, detail, needed,
                                                    nullptr, nullptr)) {
                continue;
            }

            // Open the interface (query-only access; no admin needed to list).
            Handle h(::CreateFileW(detail->DevicePath, 0,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                   OPEN_EXISTING, 0, nullptr));
            if (!h.valid()) continue;

            STORAGE_DEVICE_NUMBER sdn{};
            DWORD br = 0;
            if (!::DeviceIoControl(h.get(), IOCTL_STORAGE_GET_DEVICE_NUMBER,
                                   nullptr, 0, &sdn, sizeof(sdn), &br,
                                   nullptr)) {
                continue;
            }
            const int diskNo = static_cast<int>(sdn.DeviceNumber);

            Drive drive;
            drive.node = physicalDriveNode(diskNo);

            std::string model;
            STORAGE_BUS_TYPE bus = BusTypeUnknown;
            bool removableMedia = false;
            if (queryDescriptor(h.get(), model, bus, removableMedia)) {
                drive.description = model;
                drive.isRemovable = removableMedia || bus == BusTypeUsb ||
                                    bus == BusTypeSd || bus == BusTypeMmc;
            }
            // Zero-access query handle: must use the FILE_ANY_ACCESS geometry
            // IOCTL, not GET_LENGTH_INFO (which needs read access).
            drive.sizeBytes = queryGeometrySize(h.get());
            drive.isSystem = (sysDisk >= 0 && diskNo == sysDisk);

            for (const RawPartition& rp : queryLayout(h.get())) {
                Partition p;
                p.number = rp.number;
                p.sizeBytes = rp.size;
                // Enrich from a matching volume (same disk + starting offset).
                const VolumeInfo* match = nullptr;
                for (const VolumeInfo& v : volumes) {
                    if (static_cast<int>(v.diskNumber) == diskNo &&
                        v.startingOffset == rp.offset) {
                        match = &v;
                        break;
                    }
                }
                if (match) {
                    std::string guid = narrow(match->guidPath.c_str());
                    if (!guid.empty() && guid.back() == '\\') guid.pop_back();
                    p.node = guid;  // opaque, backend-owned
                    p.fsType = match->fsType;
                    p.label = match->label;
                    p.mountpoint = match->driveLetter;
                } else {
                    p.node = drive.node + "#" + std::to_string(rp.number);
                }
                drive.partitions.push_back(std::move(p));
            }
            drives.push_back(std::move(drive));
        }
        ::SetupDiDestroyDeviceInfoList(di);
        return drives;
    }

    Result<MountedPartition> mount(const Partition&) override {
        return std::unexpected(unsupported("Configure").error());
    }

    Result<void> unmountAll(const std::string&) override {
        // No-op on Windows: openForWrite does the authoritative lock + dismount
        // and holds the volume locks for the whole session (the equivalent of
        // Linux's unmount + O_EXCL). Doing it here too would only race the
        // auto-remount. The write path is where exclusivity is enforced.
        return {};
    }

    Result<std::unique_ptr<IRawDevice>> openForWrite(
        const std::string& node) override {
        return openSession(node, /*write=*/true);
    }
    Result<std::unique_ptr<IRawDevice>> openForRead(
        const std::string& node) override {
        return openSession(node, /*write=*/false);
    }

private:
    static Result<void> unsupported(const char* what) {
        return Err(ErrorCode::UnsupportedPlatform,
                   std::string(what) + " is not yet implemented on Windows");
    }
};

}  // namespace

std::unique_ptr<IDriveBackend> makeDriveBackend() {
    return std::make_unique<WindowsDriveBackend>();
}

}  // namespace kuiper
