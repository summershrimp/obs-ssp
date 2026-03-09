/*
 * ssp.h — Public API for the native SSP (Simple Stream Protocol) client
 *
 * Clean-room implementation for Z CAM cameras.
 * Protocol reverse-engineered from live wire captures, libssp-py, and
 * symbol analysis of the closed-source libssp.so.
 *
 * Copyright (c) 2026, Hedonistic, LLC
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef SSP_SSP_H
#define SSP_SSP_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ═══════════════════════════════════════════════════════════════════════════
 * Protocol Constants
 * ═══════════════════════════════════════════════════════════════════════════ */

#define SSP_DEFAULT_PORT       9999
#define SSP_RECV_BUF_SIZE      (4 * 1024 * 1024)

/* Stream selection */
#define SSP_STREAM_DEFAULT     0
#define SSP_STREAM_MAIN        1
#define SSP_STREAM_SECONDARY   2

/* Video encoder IDs */
#define SSP_ENCODER_H264       96
#define SSP_ENCODER_H265       265

/* Audio encoder IDs */
#define SSP_ENCODER_AAC        37
#define SSP_ENCODER_PCM        23

/* ═══════════════════════════════════════════════════════════════════════════
 * Data Structures
 * ═══════════════════════════════════════════════════════════════════════════ */

/** Video stream metadata */
typedef struct ssp_video_meta {
	uint32_t width;
	uint32_t height;
	uint32_t timescale;    /* e.g. 30000 */
	uint32_t unit;         /* e.g. 1001  → 29.97 fps */
	uint32_t gop;          /* GOP size in frames */
	uint32_t encoder;      /* SSP_ENCODER_H264 or SSP_ENCODER_H265 */
} ssp_video_meta_t;

/** Audio stream metadata */
typedef struct ssp_audio_meta {
	uint32_t sample_rate;  /* e.g. 48000 */
	uint32_t unit;         /* e.g. 1024 (samples per frame) */
	uint32_t timescale;    /* e.g. 48000 */
	uint32_t sample_size;  /* e.g. 2048 */
	uint32_t channel;      /* e.g. 2 (stereo) */
	uint32_t bitrate;      /* e.g. 128000 bps */
	uint32_t encoder;      /* SSP_ENCODER_AAC or SSP_ENCODER_PCM */
} ssp_audio_meta_t;

/** Base stream metadata */
typedef struct ssp_base_meta {
	uint16_t pts_is_wall_clock;  /* 0 or 1 */
	uint16_t tc_drop_frame;      /* 0 or 1 */
	uint32_t timecode;           /* SMPTE BCD-packed */
} ssp_base_meta_t;

/** Complete metadata */
typedef struct ssp_metadata {
	ssp_base_meta_t  base;
	ssp_video_meta_t video;
	ssp_audio_meta_t audio;
} ssp_metadata_t;

/** Video frame */
typedef struct ssp_video_frame {
	uint64_t pts;
	uint32_t frame_type;   /* 5 = IDR/I-frame, 1 = P-frame */
	uint32_t frame_number;
	const uint8_t *data;   /* H.264/H.265 NAL units (starts with 00 00 00 01) */
	size_t   data_len;
} ssp_video_frame_t;

/** Audio frame */
typedef struct ssp_audio_frame {
	uint64_t pts;
	const uint8_t *data;   /* AAC ADTS (starts with 0xFFF sync) or PCM */
	size_t   data_len;
} ssp_audio_frame_t;

/* ═══════════════════════════════════════════════════════════════════════════
 * Callbacks
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef struct ssp_callbacks {
	void (*on_metadata)(const ssp_metadata_t *meta, void *userdata);
	void (*on_video)(const ssp_video_frame_t *frame, void *userdata);
	void (*on_audio)(const ssp_audio_frame_t *frame, void *userdata);
	void (*on_connected)(void *userdata);
	void (*on_disconnected)(void *userdata);
	void *userdata;
} ssp_callbacks_t;

/* ═══════════════════════════════════════════════════════════════════════════
 * Client Handle
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef struct ssp_client ssp_client_t;

ssp_client_t *ssp_client_create(void);
void ssp_client_destroy(ssp_client_t *client);

int ssp_client_set_target(ssp_client_t *client, const char *host, unsigned int port);
int ssp_client_set_stream(ssp_client_t *client, uint32_t stream_style);
int ssp_client_set_callbacks(ssp_client_t *client, const ssp_callbacks_t *callbacks);

/**
 * Connect, authenticate, and enter the streaming loop.
 * Blocks until ssp_client_stop() is called or connection drops.
 */
int ssp_client_connect(ssp_client_t *client);

/** Signal the client to stop. Thread-safe. */
void ssp_client_stop(ssp_client_t *client);

const char *ssp_client_device_name(const ssp_client_t *client);

/* ═══════════════════════════════════════════════════════════════════════════
 * Version
 * ═══════════════════════════════════════════════════════════════════════════ */

#define SSP_VERSION_MAJOR  1
#define SSP_VERSION_MINOR  0
#define SSP_VERSION_PATCH  0

const char *ssp_version_string(void);

#ifdef __cplusplus
}
#endif

#endif /* SSP_SSP_H */
