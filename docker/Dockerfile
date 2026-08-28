# Kuiper Imager — development & Linux-build environment.
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
ENV LANG=C.UTF-8 LC_ALL=C.UTF-8

# Toolchain + build tools. GCC 13 (default on 24.04) has C++23 std::expected.
# libgl1-mesa-dev / xkb / fontconfig are Qt Widgets runtime/link deps so the GUI
# can be launched from the container over X11 during development.
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        ninja-build \
        git \
        ca-certificates \
        python3-pip python3-venv \
        libgl1-mesa-dev libxkbcommon-dev libfontconfig1 libdbus-1-3 \
        libglib2.0-0 \
        libarchive-dev \
        libssl-dev \
    && rm -rf /var/lib/apt/lists/*

# Qt 6.8 via aqtinstall — same tool CI uses (jurplel/install-qt-action wraps it),
# so the dev Qt and CI Qt are identical. Pinned version for reproducibility.
ENV QT_VERSION=6.8.2
ENV QT_ROOT=/opt/qt
RUN python3 -m venv /opt/aqt-venv \
    && /opt/aqt-venv/bin/pip install --no-cache-dir aqtinstall \
    && /opt/aqt-venv/bin/aqt install-qt linux desktop ${QT_VERSION} linux_gcc_64 \
        --outputdir ${QT_ROOT} \
    && ln -s ${QT_ROOT}/${QT_VERSION}/gcc_64 ${QT_ROOT}/current

# Let CMake's find_package(Qt6) locate this Qt.
ENV CMAKE_PREFIX_PATH=${QT_ROOT}/current
ENV PATH=${QT_ROOT}/current/bin:${PATH}

WORKDIR /workspace
CMD ["/bin/bash"]
