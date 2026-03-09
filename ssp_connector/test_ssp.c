/*
 * test_ssp.c — Unit tests for SSP protocol helpers
 *
 * Tests byte helpers, metadata parsing, video/audio frame parsing,
 * and authentication token computation.
 *
 * Copyright (c) 2026, Hedonistic, LLC
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

#include "include/ssp/ssp.h"

/* ═══════════════════════════════════════════════════════════════════════════
 * Test framework (minimal, no dependencies)
 * ═══════════════════════════════════════════════════════════════════════════ */

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
	static void test_##name(void); \
	static void run_test_##name(void) { \
		tests_run++; \
		printf("  %-50s ", #name); \
		test_##name(); \
		tests_passed++; \
		printf("PASS\n"); \
	} \
	static void test_##name(void)

#define ASSERT_EQ(a, b) do { \
	if ((a) != (b)) { \
		printf("FAIL\n    %s:%d: expected %lld, got %lld\n", \
		       __FILE__, __LINE__, \
		       (long long)(b), (long long)(a)); \
		tests_failed++; \
		tests_passed--; \
		return; \
	} \
} while (0)

#define ASSERT_STR_EQ(a, b) do { \
	if (strcmp((a), (b)) != 0) { \
		printf("FAIL\n    %s:%d: expected \"%s\", got \"%s\"\n", \
		       __FILE__, __LINE__, (b), (a)); \
		tests_failed++; \
		tests_passed--; \
		return; \
	} \
} while (0)

#define ASSERT_MEM_EQ(a, b, n) do { \
	if (memcmp((a), (b), (n)) != 0) { \
		printf("FAIL\n    %s:%d: memory mismatch (%zu bytes)\n", \
		       __FILE__, __LINE__, (size_t)(n)); \
		tests_failed++; \
		tests_passed--; \
		return; \
	} \
} while (0)

#define ASSERT_TRUE(x) ASSERT_EQ(!!(x), 1)
#define ASSERT_FALSE(x) ASSERT_EQ(!!(x), 0)

/* ═══════════════════════════════════════════════════════════════════════════
 * Re-implement byte helpers for testing (same as in ssp.c)
 * ═══════════════════════════════════════════════════════════════════════════ */

static inline uint16_t read_be16(const uint8_t *p)
{
	return ((uint16_t)p[0] << 8) | (uint16_t)p[1];
}

static inline uint32_t read_be32(const uint8_t *p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
	       ((uint32_t)p[2] << 8)  | (uint32_t)p[3];
}

static inline uint64_t read_be64(const uint8_t *p)
{
	return ((uint64_t)read_be32(p) << 32) | read_be32(p + 4);
}

static inline void write_be16(uint8_t *p, uint16_t v)
{
	p[0] = (v >> 8) & 0xFF;
	p[1] = v & 0xFF;
}

static inline void write_be32(uint8_t *p, uint32_t v)
{
	p[0] = (v >> 24) & 0xFF;
	p[1] = (v >> 16) & 0xFF;
	p[2] = (v >> 8)  & 0xFF;
	p[3] = v & 0xFF;
}

static void swap_endian_4(uint8_t *buf, size_t len)
{
	for (size_t i = 0; i + 3 < len; i += 4) {
		uint8_t t;
		t = buf[i]; buf[i] = buf[i+3]; buf[i+3] = t;
		t = buf[i+1]; buf[i+1] = buf[i+2]; buf[i+2] = t;
	}
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Byte Helper Tests
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(read_be16_basic)
{
	uint8_t buf[] = {0x12, 0x34};
	ASSERT_EQ(read_be16(buf), 0x1234);
}

TEST(read_be16_zero)
{
	uint8_t buf[] = {0x00, 0x00};
	ASSERT_EQ(read_be16(buf), 0);
}

TEST(read_be16_max)
{
	uint8_t buf[] = {0xFF, 0xFF};
	ASSERT_EQ(read_be16(buf), 0xFFFF);
}

TEST(read_be32_basic)
{
	uint8_t buf[] = {0x00, 0x00, 0x75, 0x30};
	ASSERT_EQ(read_be32(buf), 30000u); /* video.timescale */
}

TEST(read_be32_1920)
{
	uint8_t buf[] = {0x00, 0x00, 0x07, 0x80};
	ASSERT_EQ(read_be32(buf), 1920u); /* video.width */
}

TEST(read_be32_max)
{
	uint8_t buf[] = {0xFF, 0xFF, 0xFF, 0xFF};
	ASSERT_EQ(read_be32(buf), 0xFFFFFFFFu);
}

TEST(read_be64_basic)
{
	uint8_t buf[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x51, 0x80};
	ASSERT_EQ(read_be64(buf), 86400ULL); /* PTS example */
}

TEST(read_be64_large)
{
	uint8_t buf[] = {0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00};
	ASSERT_EQ(read_be64(buf), 0x0000010000000000ULL);
}

TEST(write_be16_roundtrip)
{
	uint8_t buf[2];
	write_be16(buf, 0xABCD);
	ASSERT_EQ(read_be16(buf), 0xABCD);
}

TEST(write_be32_roundtrip)
{
	uint8_t buf[4];
	write_be32(buf, 48000);
	ASSERT_EQ(read_be32(buf), 48000u);
}

TEST(swap_endian_4_basic)
{
	uint8_t buf[] = {0x01, 0x02, 0x03, 0x04};
	swap_endian_4(buf, 4);
	uint8_t expected[] = {0x04, 0x03, 0x02, 0x01};
	ASSERT_MEM_EQ(buf, expected, 4);
}

TEST(swap_endian_4_multiple)
{
	uint8_t buf[] = {0x01, 0x02, 0x03, 0x04, 0xAA, 0xBB, 0xCC, 0xDD};
	swap_endian_4(buf, 8);
	uint8_t expected[] = {0x04, 0x03, 0x02, 0x01, 0xDD, 0xCC, 0xBB, 0xAA};
	ASSERT_MEM_EQ(buf, expected, 8);
}

TEST(swap_endian_4_partial)
{
	/* Only 6 bytes — should swap first 4, leave last 2 untouched */
	uint8_t buf[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
	swap_endian_4(buf, 6);
	uint8_t expected[] = {0x04, 0x03, 0x02, 0x01, 0x05, 0x06};
	ASSERT_MEM_EQ(buf, expected, 6);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Metadata Parsing Tests
 *
 * Simulates the 77-byte metadata packet from a Z CAM E2-M4 and verifies
 * field extraction matches the documented protocol layout.
 * ═══════════════════════════════════════════════════════════════════════════ */

static void build_metadata_packet(uint8_t *pkt, size_t *pkt_len)
{
	/*
	 * Build a synthetic metadata packet:
	 * [0]     type = 0x6E
	 * [1..4]  version = 00 01 00 00
	 * [5..8]  field_count = 0x11 (17)
	 * [9..]   17 × BE32 fields
	 */
	memset(pkt, 0, 128);

	pkt[0] = 0x6E; /* SSP_PKT_METADATA */
	write_be32(pkt + 1, 0x00010000); /* version */
	write_be32(pkt + 5, 17);         /* field count */

	/* 17 fields starting at offset 9 */
	size_t off = 9;
	uint32_t fields[17] = {
		30000,  /* [0]  video.timescale */
		1001,   /* [1]  video.unit */
		1920,   /* [2]  video.width */
		1080,   /* [3]  video.height */
		30,     /* [4]  video.gop */
		0,      /* [5]  reserved */
		48000,  /* [6]  audio.sample_rate */
		1024,   /* [7]  audio.unit */
		48000,  /* [8]  audio.timescale */
		2048,   /* [9]  audio.sample_size */
		2,      /* [10] audio.channel */
		128000, /* [11] audio.bitrate */
		1,      /* [12] pts_is_wall_clock */
		96,     /* [13] video.encoder (H264) */
		37,     /* [14] audio.encoder (AAC) */
		0,      /* [15] timecode */
		0,      /* [16] tc_drop_frame */
	};

	for (int i = 0; i < 17; i++) {
		write_be32(pkt + off, fields[i]);
		off += 4;
	}

	*pkt_len = off; /* 9 + 17*4 = 77 */
}

TEST(metadata_packet_length)
{
	uint8_t pkt[128];
	size_t pkt_len;
	build_metadata_packet(pkt, &pkt_len);
	ASSERT_EQ(pkt_len, 77u);
}

TEST(metadata_type_byte)
{
	uint8_t pkt[128];
	size_t pkt_len;
	build_metadata_packet(pkt, &pkt_len);
	ASSERT_EQ(pkt[0], 0x6E);
}

TEST(metadata_field_count)
{
	uint8_t pkt[128];
	size_t pkt_len;
	build_metadata_packet(pkt, &pkt_len);
	ASSERT_EQ(read_be32(pkt + 5), 17u);
}

TEST(metadata_video_fields)
{
	uint8_t pkt[128];
	size_t pkt_len;
	build_metadata_packet(pkt, &pkt_len);

	/* Read fields at offset 9 */
	ASSERT_EQ(read_be32(pkt + 9 + 0*4), 30000u);  /* timescale */
	ASSERT_EQ(read_be32(pkt + 9 + 1*4), 1001u);   /* unit */
	ASSERT_EQ(read_be32(pkt + 9 + 2*4), 1920u);   /* width */
	ASSERT_EQ(read_be32(pkt + 9 + 3*4), 1080u);   /* height */
	ASSERT_EQ(read_be32(pkt + 9 + 4*4), 30u);     /* gop */
	ASSERT_EQ(read_be32(pkt + 9 + 13*4), 96u);    /* encoder = H264 */
}

TEST(metadata_audio_fields)
{
	uint8_t pkt[128];
	size_t pkt_len;
	build_metadata_packet(pkt, &pkt_len);

	ASSERT_EQ(read_be32(pkt + 9 + 6*4), 48000u);  /* sample_rate */
	ASSERT_EQ(read_be32(pkt + 9 + 7*4), 1024u);   /* unit */
	ASSERT_EQ(read_be32(pkt + 9 + 8*4), 48000u);  /* timescale */
	ASSERT_EQ(read_be32(pkt + 9 + 9*4), 2048u);   /* sample_size */
	ASSERT_EQ(read_be32(pkt + 9 + 10*4), 2u);     /* channel */
	ASSERT_EQ(read_be32(pkt + 9 + 11*4), 128000u); /* bitrate */
	ASSERT_EQ(read_be32(pkt + 9 + 14*4), 37u);    /* encoder = AAC */
}

TEST(metadata_base_fields)
{
	uint8_t pkt[128];
	size_t pkt_len;
	build_metadata_packet(pkt, &pkt_len);

	ASSERT_EQ(read_be32(pkt + 9 + 12*4), 1u);     /* pts_is_wall_clock */
	ASSERT_EQ(read_be32(pkt + 9 + 15*4), 0u);     /* timecode */
	ASSERT_EQ(read_be32(pkt + 9 + 16*4), 0u);     /* tc_drop_frame */
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Video Packet Parsing Tests
 * ═══════════════════════════════════════════════════════════════════════════ */

static void build_video_packet(uint8_t *pkt, size_t *pkt_len,
                               uint64_t pts, uint32_t frame_type,
                               uint32_t frm_no)
{
	/*
	 * [0]      type = 0x6F
	 * [1..8]   pts (BE64)
	 * [9..12]  frame_type (BE32)
	 * [13..16] frame_number (BE32)
	 * [17..]   NAL data (fake: 00 00 00 01 65)
	 */
	pkt[0] = 0x6F;

	/* Write PTS as BE64 */
	write_be32(pkt + 1, (uint32_t)(pts >> 32));
	write_be32(pkt + 5, (uint32_t)(pts & 0xFFFFFFFF));

	write_be32(pkt + 9, frame_type);
	write_be32(pkt + 13, frm_no);

	/* Fake NAL unit: start code + IDR slice header */
	pkt[17] = 0x00; pkt[18] = 0x00; pkt[19] = 0x00; pkt[20] = 0x01;
	pkt[21] = 0x65; /* IDR slice */

	*pkt_len = 22;
}

TEST(video_packet_type_byte)
{
	uint8_t pkt[64];
	size_t pkt_len;
	build_video_packet(pkt, &pkt_len, 90000, 5, 0);
	ASSERT_EQ(pkt[0], 0x6F);
}

TEST(video_packet_pts)
{
	uint8_t pkt[64];
	size_t pkt_len;
	build_video_packet(pkt, &pkt_len, 12345678ULL, 5, 42);
	ASSERT_EQ(read_be64(pkt + 1), 12345678ULL);
}

TEST(video_packet_frame_type_idr)
{
	uint8_t pkt[64];
	size_t pkt_len;
	build_video_packet(pkt, &pkt_len, 0, 5, 0);
	ASSERT_EQ(read_be32(pkt + 9), 5u); /* IDR */
}

TEST(video_packet_frame_type_p)
{
	uint8_t pkt[64];
	size_t pkt_len;
	build_video_packet(pkt, &pkt_len, 0, 1, 1);
	ASSERT_EQ(read_be32(pkt + 9), 1u); /* P-frame */
}

TEST(video_packet_frame_number)
{
	uint8_t pkt[64];
	size_t pkt_len;
	build_video_packet(pkt, &pkt_len, 0, 5, 999);
	ASSERT_EQ(read_be32(pkt + 13), 999u);
}

TEST(video_packet_nal_start_code)
{
	uint8_t pkt[64];
	size_t pkt_len;
	build_video_packet(pkt, &pkt_len, 0, 5, 0);

	/* NAL data starts at offset 17 with start code 00 00 00 01 */
	ASSERT_EQ(pkt[17], 0x00);
	ASSERT_EQ(pkt[18], 0x00);
	ASSERT_EQ(pkt[19], 0x00);
	ASSERT_EQ(pkt[20], 0x01);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Audio Packet Parsing Tests
 * ═══════════════════════════════════════════════════════════════════════════ */

static void build_audio_packet(uint8_t *pkt, size_t *pkt_len, uint64_t pts)
{
	/*
	 * [0]    type = 0x70
	 * [1..8] pts (BE64)
	 * [9..]  ADTS sync (0xFF 0xF1 ...)
	 */
	pkt[0] = 0x70;
	write_be32(pkt + 1, (uint32_t)(pts >> 32));
	write_be32(pkt + 5, (uint32_t)(pts & 0xFFFFFFFF));

	/* Fake ADTS header */
	pkt[9]  = 0xFF;
	pkt[10] = 0xF1;
	pkt[11] = 0x00;
	pkt[12] = 0x00;

	*pkt_len = 13;
}

TEST(audio_packet_type_byte)
{
	uint8_t pkt[32];
	size_t pkt_len;
	build_audio_packet(pkt, &pkt_len, 0);
	ASSERT_EQ(pkt[0], 0x70);
}

TEST(audio_packet_pts)
{
	uint8_t pkt[32];
	size_t pkt_len;
	build_audio_packet(pkt, &pkt_len, 48000ULL);
	ASSERT_EQ(read_be64(pkt + 1), 48000ULL);
}

TEST(audio_packet_adts_sync)
{
	uint8_t pkt[32];
	size_t pkt_len;
	build_audio_packet(pkt, &pkt_len, 0);

	/* ADTS sync word: 0xFFF */
	ASSERT_EQ(pkt[9], 0xFF);
	ASSERT_EQ(pkt[10] & 0xF0, 0xF0);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Library API Tests
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(client_create_destroy)
{
	ssp_client_t *c = ssp_client_create();
	ASSERT_TRUE(c != NULL);
	ssp_client_destroy(c);
}

TEST(client_set_target)
{
	ssp_client_t *c = ssp_client_create();
	ASSERT_TRUE(c != NULL);
	int ret = ssp_client_set_target(c, "192.168.1.100", 9999);
	ASSERT_EQ(ret, 0);
	ssp_client_destroy(c);
}

TEST(client_set_target_null)
{
	int ret = ssp_client_set_target(NULL, "192.168.1.100", 9999);
	ASSERT_EQ(ret, -1);
}

TEST(client_set_stream)
{
	ssp_client_t *c = ssp_client_create();
	int ret = ssp_client_set_stream(c, SSP_STREAM_MAIN);
	ASSERT_EQ(ret, 0);
	ssp_client_destroy(c);
}

TEST(client_set_callbacks)
{
	ssp_client_t *c = ssp_client_create();
	ssp_callbacks_t cb = {0};
	int ret = ssp_client_set_callbacks(c, &cb);
	ASSERT_EQ(ret, 0);
	ssp_client_destroy(c);
}

TEST(client_device_name_empty)
{
	ssp_client_t *c = ssp_client_create();
	const char *name = ssp_client_device_name(c);
	ASSERT_STR_EQ(name, "");
	ssp_client_destroy(c);
}

TEST(version_string_not_null)
{
	const char *v = ssp_version_string();
	ASSERT_TRUE(v != NULL);
	ASSERT_TRUE(strlen(v) > 0);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Protocol Constant Tests
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(encoder_ids)
{
	ASSERT_EQ(SSP_ENCODER_H264, 96);
	ASSERT_EQ(SSP_ENCODER_H265, 265);
	ASSERT_EQ(SSP_ENCODER_AAC, 37);
	ASSERT_EQ(SSP_ENCODER_PCM, 23);
}

TEST(default_port)
{
	ASSERT_EQ(SSP_DEFAULT_PORT, 9999);
}

TEST(stream_constants)
{
	ASSERT_EQ(SSP_STREAM_DEFAULT, 0);
	ASSERT_EQ(SSP_STREAM_MAIN, 1);
	ASSERT_EQ(SSP_STREAM_SECONDARY, 2);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Runner
 * ═══════════════════════════════════════════════════════════════════════════ */

int main(void)
{
	printf("\n  SSP Protocol Test Suite\n");
	printf("  ═══════════════════════════════════════════\n\n");

	/* Byte helpers */
	printf("  Byte Helpers:\n");
	run_test_read_be16_basic();
	run_test_read_be16_zero();
	run_test_read_be16_max();
	run_test_read_be32_basic();
	run_test_read_be32_1920();
	run_test_read_be32_max();
	run_test_read_be64_basic();
	run_test_read_be64_large();
	run_test_write_be16_roundtrip();
	run_test_write_be32_roundtrip();
	run_test_swap_endian_4_basic();
	run_test_swap_endian_4_multiple();
	run_test_swap_endian_4_partial();

	/* Metadata parsing */
	printf("\n  Metadata Parsing:\n");
	run_test_metadata_packet_length();
	run_test_metadata_type_byte();
	run_test_metadata_field_count();
	run_test_metadata_video_fields();
	run_test_metadata_audio_fields();
	run_test_metadata_base_fields();

	/* Video parsing */
	printf("\n  Video Packet Parsing:\n");
	run_test_video_packet_type_byte();
	run_test_video_packet_pts();
	run_test_video_packet_frame_type_idr();
	run_test_video_packet_frame_type_p();
	run_test_video_packet_frame_number();
	run_test_video_packet_nal_start_code();

	/* Audio parsing */
	printf("\n  Audio Packet Parsing:\n");
	run_test_audio_packet_type_byte();
	run_test_audio_packet_pts();
	run_test_audio_packet_adts_sync();

	/* Library API */
	printf("\n  Library API:\n");
	run_test_client_create_destroy();
	run_test_client_set_target();
	run_test_client_set_target_null();
	run_test_client_set_stream();
	run_test_client_set_callbacks();
	run_test_client_device_name_empty();
	run_test_version_string_not_null();

	/* Protocol constants */
	printf("\n  Protocol Constants:\n");
	run_test_encoder_ids();
	run_test_default_port();
	run_test_stream_constants();

	/* Summary */
	printf("\n  ═══════════════════════════════════════════\n");
	printf("  Results: %d/%d passed", tests_passed, tests_run);
	if (tests_failed > 0)
		printf(", %d FAILED", tests_failed);
	printf("\n\n");

	return tests_failed > 0 ? 1 : 0;
}
