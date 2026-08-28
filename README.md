# Kuiper Imager 2

Cross-platform (Linux / Windows / macOS) tool to **fetch, flash, and configure**
Kuiper Linux images for Analog Devices hardware. CLI-first, GUI on top.

> Early development. This branch (`v2.0`) is a ground-up rewrite and will become
> `main` when ready to ship. See the architecture plan for the full design.

## Layout

```
src/
  core/     libkuiper — UI-agnostic business logic (Qt Core only)
            The single source of truth both front-ends use (Option A).
  cli/      kli           — CLI front-end (thin)
  gui/      kuiper-imager — Qt Widgets GUI front-end (thin)
docker/     Dockerfile — dev + Linux-build environment
```

Both `kli` and the GUI call the **same** `kuiper::DriveService` in-process — so
they behave identically by construction. Adding Windows/macOS later means adding
a drive backend behind `IDriveBackend` + one factory branch; nothing else
changes.

## Develop & build (Docker, Linux)

Development and the Linux build happen in Docker. The Windows and macOS
executables are produced by native CI runners (not from this container).

```sh
# Build the dev image (Ubuntu + Qt 6.8 + toolchain)
docker compose build

# Get a dev shell
docker compose run --rm dev

# Inside the container:
cmake --preset dev
cmake --build build
./build/src/cli/kli version
./build/src/cli/kli list-drives
```

### CLI-only build (no Qt Widgets)

```sh
cmake --preset cli-only && cmake --build build-cli
```

### Running the GUI over X11

From Linux/WSLg, allow the container to reach your display once per session:

```sh
xhost +local:
docker compose run --rm dev ./build/src/gui/kuiper-imager
```

## Build without Docker

Requires Qt 6.8+, CMake 3.22+, a C++23 compiler:

```sh
cmake --preset dev && cmake --build build
```
