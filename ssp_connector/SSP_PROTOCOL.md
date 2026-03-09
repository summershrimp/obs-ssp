# SSP Wire Protocol Specification

Reverse-engineered from live Z CAM E2-M4 captures. Clean-room — no proprietary
code was referenced.

## Transport

- TCP, default port **9999**
- All multi-byte integers are **big-endian**
- Every packet is length-prefixed: `[4-byte BE32 length][payload]`

## Connection Flow

```
Client                          Camera
  │                               │
  │◄──────── INIT (0x64) ─────────│  challenge + device name
  │                               │
  │──── HANDSHAKE (0xC8) ────────►│  username + auth token
  │                               │
  │◄──── HANDSHAKE RESP ──────────│  0x01=OK, 0x02=error
  │                               │
  │── STREAM_START (0xCA) ───────►│  stream_style (0/1/2)
  │                               │
  │◄──── METADATA (0x6E) ─────────│  17 × BE32 fields
  │◄────── VIDEO (0x6F) ──────────│  pts + NAL data
  │◄────── AUDIO (0x70) ──────────│  pts + ADTS data
  │◄────── VIDEO (0x6F) ──────────│  ...
  │                               │
  │── HEARTBEAT (0xCB) ─────────►│  every ~3 seconds
  │                               │
```

## Packet Types

| Type | Hex  | Direction | Description          |
|------|------|-----------|----------------------|
| INIT | 0x64 | S → C     | Challenge + device   |
| HANDSHAKE | 0xC8 | C → S | Auth response |
| STREAM_START | 0xCA | C → S | Begin streaming |
| HEARTBEAT | 0xCB | C → S | Keep-alive |
| METADATA | 0x6E | S → C | Stream parameters |
| VIDEO | 0x6F | S → C | Video frame |
| AUDIO | 0x70 | S → C | Audio frame |
| OK | 0x01 | S → C | Success response |
| ERROR | 0x02 | S → C | Error response |

## Authentication

```
password = "zcam-live-password"
username = "zcam-live-user"

H1 = SHA1(password)
H2 = SHA1(H1)
digest = SHA1(challenge || H2)
swap_endian_4(digest)           # swap each 4-byte group
token = H1 XOR digest
```

### INIT Packet (0x64)

```
Offset  Size  Description
──────  ────  ───────────────────
0       1     Type (0x64)
1-7     7     Version/flags
8       var   Device name (null-terminated, e.g. "elephant")
43      20    Challenge bytes
```

Total: 63 bytes (typical)

### HANDSHAKE Packet (0xC8)

```
Offset  Size  Description
──────  ────  ───────────────────
0-1     2     Padding (0x00 0x00)
2-3     2     Payload length (0x00 0x28 = 40)
4       1     Type (0xC8)
5-6     2     Flags (0x00 0x00)
7-21    15    Username "zcam-live-user\0"
22-23   2     Token length (0x00 0x14 = 20)
24-43   20    Auth token
```

Total: 44 bytes (sent raw, NOT length-prefixed)

### STREAM_START Packet (0xCA)

```
Offset  Size  Description
──────  ────  ───────────────────
0       1     Type (0xCA)
1-4     4     Stream style (BE32): 0=default, 1=main, 2=secondary
5-8     4     Reserved (0x00000000)
```

Total: 9 bytes (length-prefixed)

## Metadata Packet (0x6E)

```
Offset  Size  Description
──────  ────  ───────────────────
0       1     Type (0x6E)
1-4     4     Version/flags (e.g. 00 01 00 00)
5-8     4     Field count (BE32, 0x11 = 17)
9-76    68    17 × BE32 fields (see table below)
```

Total: 77 bytes

### Metadata Fields (17 × BE32, starting at offset 9)

| Index | Field              | Example Value |
|-------|--------------------|---------------|
| 0     | video.timescale    | 30000         |
| 1     | video.unit         | 1001          |
| 2     | video.width        | 1920          |
| 3     | video.height       | 1080          |
| 4     | video.gop          | 30            |
| 5     | (reserved)         | 0             |
| 6     | audio.sample_rate  | 48000         |
| 7     | audio.unit         | 1024          |
| 8     | audio.timescale    | 48000         |
| 9     | audio.sample_size  | 2048          |
| 10    | audio.channel      | 2             |
| 11    | audio.bitrate      | 128000        |
| 12    | pts_is_wall_clock  | 1             |
| 13    | video.encoder      | 96 (H264)     |
| 14    | audio.encoder      | 37 (AAC)      |
| 15    | timecode           | (SMPTE BCD)   |
| 16    | tc_drop_frame      | 0 or 1        |

### Encoder IDs

| ID  | Codec |
|-----|-------|
| 96  | H.264 |
| 265 | H.265 |
| 37  | AAC   |
| 23  | PCM   |

## Video Packet (0x6F)

```
Offset  Size  Description
──────  ────  ───────────────────
0       1     Type (0x6F)
1-8     8     PTS (BE64)
9-12    4     Frame type (BE32): 5=IDR, 1=P
13-16   4     Frame number (BE32)
17+     var   NAL data (starts with 00 00 00 01 start code)
```

Frame type values:
- `5` = IDR / I-frame (keyframe)
- `1` = P-frame (predicted)

## Audio Packet (0x70)

```
Offset  Size  Description
──────  ────  ───────────────────
0       1     Type (0x70)
1-8     8     PTS (BE64)
9+      var   Audio data (AAC ADTS: starts with 0xFFF1 sync)
```

## Heartbeat

- Client sends every ~3 seconds
- Single byte `0xCB` (length-prefixed as a 1-byte packet)
- Camera does not respond; absence of data causes timeout disconnect

## Known Camera Models

| Device Name | Camera Model |
|-------------|--------------|
| elephant    | Z CAM E2-M4  |

## References

- [libssp-py](https://pypi.org/project/libssp-py/) — Python SSP client (auth algorithm)
- [obs-ssp](https://github.com/summershrimp/obs-ssp) — OBS plugin (IPC protocol)
- Z CAM firmware RTSP/SSP implementation
