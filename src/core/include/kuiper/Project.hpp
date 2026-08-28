#pragma once

#include <string>
#include <vector>

namespace kuiper {

// A configurable hardware project, parsed from a BOOT-partition manifest (*.json).
// `(name, board)` identifies it: the same `name` (eval board) recurs across
// carriers.
struct Project {
    std::string name;          // eval board, e.g. "ad9081"
    std::string platform;      // xilinx | intel | rpi
    std::string architecture;  // zynqmp | versal | zynq | arria10 | ...
    std::string board;         // carrier, e.g. "vck190"
    std::string kernel;        // manifest path string, e.g. "/boot/.../Image"
    std::vector<std::string> files;  // files[].path entries
    std::string preloader;     // intel-only bootloader image; empty otherwise
    std::string sourceFile;    // manifest this project came from (diagnostics)
};

using ProjectList = std::vector<Project>;

}  // namespace kuiper
