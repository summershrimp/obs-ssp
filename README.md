# obs-ssp

OBS Studio plugin for streaming from Z CAM cameras via **SSP (Simple Stream Protocol)**.

obs-ssp includes a **native SSP client** — a clean-room, open-source replacement for the
closed-source `libssp.so`. The protocol was reverse-engineered from live Z CAM E2-M4
wire captures, [libssp-py](https://pypi.org/project/libssp-py/), and symbol analysis.

[简体中文](./README-zh.md)

[![Build Status](https://github.com/summershrimp/obs-ssp/actions/workflows/push.yaml/badge.svg?branch=master)](https://github.com/summershrimp/obs-ssp/actions/workflows/push.yaml)
[![FOSSA Status](https://app.fossa.com/api/projects/git%2Bgithub.com%2Fsummershrimp%2Fobs-ssp.svg?type=shield)](https://app.fossa.com/projects/git%2Bgithub.com%2Fsummershrimp%2Fobs-ssp?ref=badge_shield)

## Native SSP Implementation

- **Native SSP connector** — no closed-source `libssp` dependency
- **Linux aarch64 support** — builds on ARM64 (Asahi Linux, Raspberry Pi, etc.)
- **Clean-room library** (`ssp.c` + `ssp.h`) — embeddable in other projects
- **Wire protocol documentation** — complete packet specifications in `ssp_connector/SSP_PROTOCOL.md`
- **Cross-platform socket abstraction** — POSIX and Windows via `ssp_platform.h`
- **Unit tests** — protocol parsing and byte helper verification

## Features

- **SSP Source** in OBS — receive live video and audio from Z CAM cameras
- H.264 and H.265 video, AAC and PCM audio
- Multiple stream styles (default, main, secondary)
- Automatic SHA1 challenge-response authentication
- Heartbeat keep-alive with 3-second interval

![obs-ssp](./images/obs-ssp.png)

## Supported Cameras

Any Z CAM camera with SSP streaming enabled:

| Camera | Device Name | Verified |
|--------|-------------|----------|
| Z CAM E2-M4 | elephant | Yes (live captures) |
| Z CAM E2 | — | Expected compatible |
| Z CAM E2-S6 | — | Expected compatible |
| Z CAM E2-F6 | — | Expected compatible |
| Z CAM E2-F8 | — | Expected compatible |

## Downloads

Binaries for Windows and macOS are available in the [Releases](https://github.com/summershrimp/obs-ssp/releases) section.

## Community

Discord (English only): https://discord.gg/uFpTbh3AVC

QQ (Chinese only):

![QQ](./images/qq-group.png)

## Native SSP Development

### Prerequisites

- **OBS Studio** 28+ development headers
- **CMake** 3.16+
- **OpenSSL** development libraries (for native SSP)

### Linux (native SSP)

```bash
# Install dependencies (Fedora)
sudo dnf install obs-studio-devel openssl-devel cmake gcc

# Configure and build
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The native SSP connector is selected automatically on Linux for both x86_64
and aarch64, with no closed-source `libssp` dependency.

### Windows

```powershell
cmake -B build -S . -DUSE_NATIVE_SSP=ON
cmake --build build --config Release
```

### macOS

```bash
cmake -B build -S . -DUSE_NATIVE_SSP=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Building Tests

```bash
cmake -B build -S . -DUSE_NATIVE_SSP=ON -DSSP_BUILD_TESTS=ON
cmake --build build
./build/ssp_connector/test-ssp
```

## Architecture

```
obs-ssp/
  src/                        # OBS plugin (source, properties, output)
  ssp_connector/
    include/ssp/ssp.h         # Public library API
    ssp.c                     # SSP client library (no globals, callback-driven)
    ssp_platform.h            # POSIX / Windows socket abstraction
    ssp_connector_main.c      # CLI bridge (library -> IPC -> OBS plugin)
    ssp_connector_proto.h     # IPC protocol structs (packed Message format)
    ssp_native.c              # Original monolithic implementation (preserved)
    test_ssp.c                # Unit tests
    SSP_PROTOCOL.md           # Complete wire protocol specification
    main.cpp                  # Original libssp-based connector
```

### Library vs CLI

The SSP client is split into two layers:

1. **`ssp.c` / `ssp.h`** — Embeddable C library with a clean callback API:
   ```c
   ssp_client_t *client = ssp_client_create();
   ssp_client_set_target(client, "192.168.1.100", 9999);
   ssp_client_set_callbacks(client, &callbacks);
   ssp_client_connect(client);  // blocks until stop or disconnect
   ssp_client_destroy(client);
   ```

2. **`ssp_connector_main.c`** — Thin CLI that bridges library callbacks to the
   OBS plugin's packed IPC format over stdout.

## Protocol Overview

SSP uses TCP (default port 9999) with big-endian length-prefixed packets.

```
Client                          Camera
  |                               |
  |<-------- INIT (0x64) ---------|  challenge + device name
  |-------- HANDSHAKE (0xC8) ---->|  username + auth token
  |<------ HANDSHAKE RESP --------|  OK / error
  |---- STREAM_START (0xCA) ----->|  stream style
  |<------ METADATA (0x6E) -------|  17 x BE32 fields
  |<-------- VIDEO (0x6F) --------|  pts + NAL data
  |<-------- AUDIO (0x70) --------|  pts + ADTS data
  |---- HEARTBEAT (0xCB) -------->|  every ~3 seconds
```

Full protocol documentation: [`ssp_connector/SSP_PROTOCOL.md`](ssp_connector/SSP_PROTOCOL.md)

## Building obs-ssp

The build configuration tracks [obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate/commit/3e7d7ac3b5342cd7d9b88890b9c70b472d1520fc).
Use CMake 3.30+ and initialize submodules with `git submodule update --init --recursive`.

- Windows: Visual Studio 2022 and Windows SDK 10.0.22621. Run `cmake --preset windows-x64`, then `cmake --build --preset windows-x64`.
- macOS: Xcode 16+ with the macOS 15 SDK. Run `cmake --preset macos`, then `cmake --build --preset macos`. The universal plugin targets macOS 12+.
- Stage Windows files with `cmake --install build_x64 --config RelWithDebInfo --prefix release/RelWithDebInfo`; copy the resulting `obs-ssp` directory to `%PROGRAMDATA%/obs-studio/plugins/`. The connector and libssp DLL must remain beside the plugin DLL.

CMake fetches the OBS 31.1.1 SDK, prebuilt dependencies, and Qt 6. Windows and macOS use libssp by default, while Linux uses the native SSP connector. Qt is required for camera controls and discovery. Scripts under `.github/scripts/` are intended for CI.

## License

BSD-3-Clause

[![FOSSA Status](https://app.fossa.com/api/projects/git%2Bgithub.com%2Fsummershrimp%2Fobs-ssp.svg?type=large)](https://app.fossa.com/projects/git%2Bgithub.com%2Fsummershrimp%2Fobs-ssp?ref=badge_large)

Native SSP implementation: Copyright (c) 2026, Hedonistic, LLC
Original obs-ssp plugin: Copyright (c) 2015-2021, Yibai Zhang

## Credits

- **Yibai Zhang** ([@summershrimp](https://github.com/summershrimp)) — original obs-ssp plugin
- **Hedonistic, LLC** — native SSP client, protocol reverse engineering, Linux aarch64 support
- [libssp-py](https://pypi.org/project/libssp-py/) — reference for authentication algorithm
- [Z CAM](https://www.z-cam.com/) — camera manufacturer
