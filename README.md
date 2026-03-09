# obs-ssp

OBS Studio plugin for streaming from Z CAM cameras via **SSP (Simple Stream Protocol)**.

This fork adds a **native SSP client** — a clean-room, open-source replacement for the
closed-source `libssp.so`. The protocol was reverse-engineered from live Z CAM E2-M4
wire captures, [libssp-py](https://pypi.org/project/libssp-py/), and symbol analysis.

## What's New (This Fork)

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

## Supported Cameras

Any Z CAM camera with SSP streaming enabled:

| Camera | Device Name | Verified |
|--------|-------------|----------|
| Z CAM E2-M4 | elephant | Yes (live captures) |
| Z CAM E2 | — | Expected compatible |
| Z CAM E2-S6 | — | Expected compatible |
| Z CAM E2-F6 | — | Expected compatible |
| Z CAM E2-F8 | — | Expected compatible |

## Building

### Prerequisites

- **OBS Studio** 28+ development headers
- **CMake** 3.16+
- **OpenSSL** development libraries (for native SSP)

### Linux (aarch64 — automatic native SSP)

```bash
# Install dependencies (Fedora)
sudo dnf install obs-studio-devel openssl-devel cmake gcc

# Configure and build
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

On Linux aarch64, the native SSP connector is selected automatically (no
closed-source libssp binary exists for this architecture).

### Linux (x86_64 — opt-in native SSP)

```bash
cmake -B build -S . -DUSE_NATIVE_SSP=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

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

## License

BSD-3-Clause

Native SSP implementation: Copyright (c) 2026, Hedonistic, LLC
Original obs-ssp plugin: Copyright (c) 2015-2021, Yibai Zhang

## Credits

- **Yibai Zhang** ([@summershrimp](https://github.com/summershrimp)) — original obs-ssp plugin
- **Hedonistic, LLC** — native SSP client, protocol reverse engineering, Linux aarch64 support
- [libssp-py](https://pypi.org/project/libssp-py/) — reference for authentication algorithm
- [Z CAM](https://www.z-cam.com/) — camera manufacturer
