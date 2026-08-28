FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
ENV LANG=C.UTF-8 LC_ALL=C.UTF-8

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
        libcurl4-openssl-dev \
    && rm -rf /var/lib/apt/lists/*

ENV QT_VERSION=6.8.2
ENV QT_ROOT=/opt/qt
RUN python3 -m venv /opt/aqt-venv \
    && /opt/aqt-venv/bin/pip install --no-cache-dir aqtinstall \
    && /opt/aqt-venv/bin/aqt install-qt linux desktop ${QT_VERSION} linux_gcc_64 \
        --outputdir ${QT_ROOT} \
    && ln -s ${QT_ROOT}/${QT_VERSION}/gcc_64 ${QT_ROOT}/current

ENV CMAKE_PREFIX_PATH=${QT_ROOT}/current
ENV PATH=${QT_ROOT}/current/bin:${PATH}

WORKDIR /workspace
CMD ["/bin/bash"]
