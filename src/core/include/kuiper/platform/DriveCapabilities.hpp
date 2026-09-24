#pragma once

namespace kuiper {

// What a drive backend can actually do on the host platform. A front-end reads
// this to gate commands up front rather than failing deep in a pipeline: the
// Linux backend reports every capability, while the macOS/Windows stubs report
// none until their Phase 4 ports land. See docs: platform-backends.
struct DriveCapabilities {
    bool enumerate = false;  // listDrives returns real data
    bool flash = false;      // openForWrite + the raw write path work
    bool mount = false;      // mount / the configure path work
};

}  // namespace kuiper
