/*
 * ssp.c — SSP (Simple Stream Protocol) client library
 *
 * Clean-room implementation for Z CAM cameras.
 * Protocol reverse-engineered from live wire captures, libssp-py,
 * and symbol analysis of the closed-source libssp.so.
 *
 * Copyright (c) 2026, Hedonistic, LLC
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "ssp_platform.h"
#include "include/ssp/ssp.h"

#include <openssl/evp.h>

/* ═══════════════════════════════════════════════════════════════════════════
 * Internal logging
 * ═══════════════════════════════════════════════════════════════════════════ */

#ifndef SSP_LOG_FILE
#define SSP_LOG_FILE stderr
#endif

#define ssp_log(fmt, ...) \
	fprintf(SSP_LOG_FILE, "[ssp] %s:%d " fmt "\n", \
	        __FILE__, __LINE__, ##__VA_ARGS__)

/* ═══════════════════════════════════════════════════════════════════════════
 * Wire Protocol Constants
 * ═══════════════════════════════════════════════════════════════════════════ */

#define SSP_PKT_INIT           0x64
#define SSP_PKT_METADATA       0x6E
#define SSP_PKT_VIDEO          0x6F
#define SSP_PKT_AUDIO          0x70
#define SSP_PKT_HANDSHAKE      0xC8
#define SSP_PKT_STREAM_START   0xCA
#define SSP_PKT_OK             0x01
#define SSP_PKT_ERROR          0x02
#define SSP_PKT_HEARTBEAT      0xCB

#define HEARTBEAT_INTERVAL_MS  3000

/* Authentication constants (from libssp-py reverse engineering) */
static const char SSP_USERNAME[] = "zcam-live-user";

/* Pre-computed: SHA1("zcam-live-password") */
static const uint8_t PASSWORD_HASH[20] = {
	0x41, 0x67, 0x7f, 0x4a, 0x21, 0x55, 0xc6, 0x05,
	0x61, 0xed, 0xe4, 0xa1, 0x68, 0x42, 0xd0, 0x1a,
	0xd1, 0x0e, 0x73, 0xf5
};

/* Pre-computed: SHA1(SHA1("zcam-live-password")) */
static const uint8_t PASSWORD_HASH_HASH[20] = {
	0x0d, 0xc5, 0x27, 0x59, 0x89, 0x82, 0xf9, 0x5b,
	0x58, 0x1e, 0x61, 0x64, 0xd3, 0xe3, 0x7b, 0x65,
	0x69, 0x52, 0x1e, 0xef
};

/* ═══════════════════════════════════════════════════════════════════════════
 * Client State
 * ═══════════════════════════════════════════════════════════════════════════ */

struct ssp_client {
	/* Connection */
	ssp_socket_t     sock;
	char             host[256];
	unsigned int     port;
	uint32_t         stream_style;
	volatile bool    running;

	/* Callbacks */
	ssp_callbacks_t  callbacks;

	/* Device info */
	char             device_name[128];

	/* Metadata (cached for queries) */
	ssp_metadata_t   metadata;
	bool             has_metadata;
};

/* ═══════════════════════════════════════════════════════════════════════════
 * Byte Helpers
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

/* Swap endianness in 4-byte groups (from libssp-py auth algorithm) */
static void swap_endian_4(uint8_t *buf, size_t len)
{
	for (size_t i = 0; i + 3 < len; i += 4) {
		uint8_t t;
		t = buf[i]; buf[i] = buf[i+3]; buf[i+3] = t;
		t = buf[i+1]; buf[i+1] = buf[i+2]; buf[i+2] = t;
	}
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Network I/O
 * ═══════════════════════════════════════════════════════════════════════════ */

static int recv_exact(ssp_client_t *c, void *buf, size_t n)
{
	uint8_t *p = (uint8_t *)buf;
	size_t remaining = n;

	while (remaining > 0 && c->running) {
		ssize_t r = recv(c->sock, (char *)p, remaining, 0);
		if (r <= 0) {
			if (r == 0) {
				ssp_log("connection closed by peer");
			} else if (ssp_errno_intr()) {
				continue;
			} else {
				ssp_log("recv error: %d", ssp_errno());
			}
			return -1;
		}
		p += r;
		remaining -= (size_t)r;
	}
	return c->running ? 0 : -1;
}

static int send_exact(ssp_client_t *c, const void *buf, size_t n)
{
	const uint8_t *p = (const uint8_t *)buf;
	size_t remaining = n;

	while (remaining > 0) {
		ssize_t w = send(c->sock, (const char *)p, remaining,
		                 SSP_MSG_NOSIGNAL);
		if (w <= 0) {
			if (w < 0 && ssp_errno_intr())
				continue;
			ssp_log("send error: %d", ssp_errno());
			return -1;
		}
		p += w;
		remaining -= (size_t)w;
	}
	return 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * SSP Packet I/O — [4-byte BE32 length][payload]
 * ═══════════════════════════════════════════════════════════════════════════ */

static uint8_t *ssp_recv_packet(ssp_client_t *c, uint32_t *out_len)
{
	uint8_t len_buf[4];
	if (recv_exact(c, len_buf, 4) < 0)
		return NULL;

	uint32_t pkt_len = read_be32(len_buf);
	if (pkt_len == 0 || pkt_len > SSP_RECV_BUF_SIZE) {
		ssp_log("invalid packet length: %u", pkt_len);
		return NULL;
	}

	uint8_t *data = (uint8_t *)malloc(pkt_len);
	if (!data) {
		ssp_log("malloc failed for %u bytes", pkt_len);
		return NULL;
	}

	if (recv_exact(c, data, pkt_len) < 0) {
		free(data);
		return NULL;
	}

	*out_len = pkt_len;
	return data;
}

static int ssp_send_packet(ssp_client_t *c, const void *data, uint32_t len)
{
	uint8_t len_buf[4];
	write_be32(len_buf, len);
	if (send_exact(c, len_buf, 4) < 0)
		return -1;
	if (len > 0 && send_exact(c, data, len) < 0)
		return -1;
	return 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Authentication
 *
 * token = SHA1(password) XOR swap_endian(SHA1(challenge || SHA1(SHA1(password))))
 * ═══════════════════════════════════════════════════════════════════════════ */

static void compute_auth_token(const uint8_t *challenge, size_t challenge_len,
                               uint8_t *token_out)
{
	uint8_t digest[20];
	unsigned int digest_len = 0;

	EVP_MD_CTX *ctx = EVP_MD_CTX_new();
	EVP_DigestInit_ex(ctx, EVP_sha1(), NULL);
	EVP_DigestUpdate(ctx, challenge, challenge_len);
	EVP_DigestUpdate(ctx, PASSWORD_HASH_HASH, sizeof(PASSWORD_HASH_HASH));
	EVP_DigestFinal_ex(ctx, digest, &digest_len);
	EVP_MD_CTX_free(ctx);

	swap_endian_4(digest, 20);

	for (int i = 0; i < 20; i++)
		token_out[i] = PASSWORD_HASH[i] ^ digest[i];
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Protocol Logic
 * ═══════════════════════════════════════════════════════════════════════════ */

static int do_connect(ssp_client_t *c)
{
	ssp_platform_init();

	ssp_socket_t sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock == SSP_INVALID_SOCKET) {
		ssp_log("socket() failed: %d", ssp_errno());
		return -1;
	}

	/* TCP_NODELAY for minimum latency */
	int flag = 1;
	setsockopt(sock, IPPROTO_TCP, TCP_NODELAY,
	           (const char *)&flag, sizeof(flag));

	/* Increase receive buffer */
	int rcvbuf = SSP_RECV_BUF_SIZE;
	setsockopt(sock, SOL_SOCKET, SO_RCVBUF,
	           (const char *)&rcvbuf, sizeof(rcvbuf));

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons((uint16_t)c->port);

	if (inet_pton(AF_INET, c->host, &addr.sin_addr) != 1) {
		ssp_log("invalid address: %s", c->host);
		ssp_close(sock);
		return -1;
	}

	/* Connect with 5-second timeout */
#ifdef _WIN32
	DWORD tv = 5000;
#else
	struct timeval tv = {.tv_sec = 5, .tv_usec = 0};
#endif
	setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tv, sizeof(tv));

	if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		ssp_log("connect to %s:%u failed: %d", c->host, c->port, ssp_errno());
		ssp_close(sock);
		return -1;
	}

	/* Clear the send timeout */
#ifdef _WIN32
	tv = 0;
#else
	tv.tv_sec = 0;
	tv.tv_usec = 0;
#endif
	setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tv, sizeof(tv));

	c->sock = sock;
	ssp_log("connected to %s:%u", c->host, c->port);
	return 0;
}

static int do_handshake(ssp_client_t *c)
{
	uint32_t pkt_len;
	uint8_t *pkt;

	/* Receive INIT packet */
	pkt = ssp_recv_packet(c, &pkt_len);
	if (!pkt) {
		ssp_log("failed to receive INIT packet");
		return -1;
	}

	if (pkt[0] != SSP_PKT_INIT) {
		ssp_log("expected INIT (0x%02x), got 0x%02x", SSP_PKT_INIT, pkt[0]);
		free(pkt);
		return -1;
	}

	/* Extract device name (null-terminated at offset 8) */
	size_t name_start = 8;
	if (name_start < pkt_len) {
		size_t name_len = strnlen((char *)(pkt + name_start),
		                          pkt_len - name_start);
		if (name_len > sizeof(c->device_name) - 1)
			name_len = sizeof(c->device_name) - 1;
		memcpy(c->device_name, pkt + name_start, name_len);
		c->device_name[name_len] = '\0';
	}
	ssp_log("device: %s", c->device_name);

	/* Extract challenge (20 bytes at offset 43) */
	if (pkt_len < 43 + 20) {
		ssp_log("INIT packet too short for challenge: %u bytes", pkt_len);
		free(pkt);
		return -1;
	}

	uint8_t challenge[20];
	memcpy(challenge, pkt + 43, 20);
	free(pkt);

	/* Compute auth token and send handshake */
	uint8_t token[20];
	compute_auth_token(challenge, 20, token);

	uint8_t handshake[44];
	memset(handshake, 0, sizeof(handshake));
	handshake[0] = 0x00;
	handshake[1] = 0x00;
	handshake[2] = 0x00;
	handshake[3] = 0x28; /* payload length = 40 */
	handshake[4] = SSP_PKT_HANDSHAKE;
	handshake[5] = 0x00;
	handshake[6] = 0x00;
	memcpy(handshake + 7, SSP_USERNAME, strlen(SSP_USERNAME) + 1);
	handshake[22] = 0x00;
	handshake[23] = 0x14; /* token length = 20 */
	memcpy(handshake + 24, token, 20);

	if (send_exact(c, handshake, sizeof(handshake)) < 0) {
		ssp_log("failed to send HANDSHAKE");
		return -1;
	}

	ssp_log("sent handshake");

	/* Receive handshake response */
	pkt = ssp_recv_packet(c, &pkt_len);
	if (!pkt) {
		ssp_log("failed to receive handshake response");
		return -1;
	}

	ssp_log("handshake response: type=0x%02x len=%u", pkt[0], pkt_len);

	if (pkt_len > 0 && pkt[0] == SSP_PKT_ERROR) {
		ssp_log("handshake rejected by camera");
		free(pkt);
		return -1;
	}

	free(pkt);
	ssp_log("handshake complete");
	return 0;
}

static int do_start_stream(ssp_client_t *c)
{
	uint8_t start_pkt[9];
	start_pkt[0] = SSP_PKT_STREAM_START;
	write_be32(start_pkt + 1, c->stream_style);
	write_be32(start_pkt + 5, 0);

	if (ssp_send_packet(c, start_pkt, sizeof(start_pkt)) < 0) {
		ssp_log("failed to send STREAM_START");
		return -1;
	}

	ssp_log("sent stream start (style=%u)", c->stream_style);
	return 0;
}

static int do_send_heartbeat(ssp_client_t *c)
{
	uint8_t hb[1] = { SSP_PKT_HEARTBEAT };
	return ssp_send_packet(c, hb, sizeof(hb));
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Packet Handlers — dispatch to user callbacks
 * ═══════════════════════════════════════════════════════════════════════════ */

static void handle_metadata(ssp_client_t *c, const uint8_t *pkt,
                            uint32_t pkt_len)
{
	if (pkt_len < 77) {
		ssp_log("metadata packet too short: %u bytes (need >= 77)", pkt_len);
		return;
	}

	uint32_t field_count = read_be32(pkt + 5);
	if (field_count < 17)
		ssp_log("metadata: unexpected field count %u (expected >= 17)",
		        field_count);

	/* Read 17 BE32 fields starting at offset 9 */
	size_t off = 9;
	uint32_t f[17];
	for (int i = 0; i < 17 && off + 4 <= pkt_len; i++, off += 4)
		f[i] = read_be32(pkt + off);

	ssp_metadata_t *md = &c->metadata;
	memset(md, 0, sizeof(*md));

	md->video.timescale = f[0];
	md->video.unit      = f[1];
	md->video.width     = f[2];
	md->video.height    = f[3];
	md->video.gop       = f[4];
	md->video.encoder   = f[13];

	md->audio.sample_rate = f[6];
	md->audio.unit        = f[7];
	md->audio.timescale   = f[8];
	md->audio.sample_size = f[9];
	md->audio.channel     = f[10];
	md->audio.bitrate     = f[11];
	md->audio.encoder     = f[14];

	md->base.pts_is_wall_clock = (uint16_t)f[12];
	md->base.timecode          = f[15];
	md->base.tc_drop_frame     = (uint16_t)f[16];

	c->has_metadata = true;

	ssp_log("metadata: video=%ux%u ts=%u/%u gop=%u enc=%u, "
	        "audio=%uhz ch=%u enc=%u br=%u, wall_clock=%u",
	        md->video.width, md->video.height,
	        md->video.timescale, md->video.unit,
	        md->video.gop, md->video.encoder,
	        md->audio.sample_rate, md->audio.channel,
	        md->audio.encoder, md->audio.bitrate,
	        md->base.pts_is_wall_clock);

	if (c->callbacks.on_metadata)
		c->callbacks.on_metadata(md, c->callbacks.userdata);
}

static void handle_video(ssp_client_t *c, const uint8_t *pkt,
                         uint32_t pkt_len)
{
	if (pkt_len < 17) {
		ssp_log("video packet too short: %u", pkt_len);
		return;
	}

	ssp_video_frame_t frame;
	frame.pts          = read_be64(pkt + 1);
	frame.frame_type   = read_be32(pkt + 9);
	frame.frame_number = read_be32(pkt + 13);
	frame.data         = pkt + 17;
	frame.data_len     = pkt_len - 17;

	if (c->callbacks.on_video)
		c->callbacks.on_video(&frame, c->callbacks.userdata);
}

static void handle_audio(ssp_client_t *c, const uint8_t *pkt,
                         uint32_t pkt_len)
{
	if (pkt_len < 9) {
		ssp_log("audio packet too short: %u", pkt_len);
		return;
	}

	ssp_audio_frame_t frame;
	frame.pts      = read_be64(pkt + 1);
	frame.data     = pkt + 9;
	frame.data_len = pkt_len - 9;

	if (c->callbacks.on_audio)
		c->callbacks.on_audio(&frame, c->callbacks.userdata);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Streaming Loop
 * ═══════════════════════════════════════════════════════════════════════════ */

static int stream_loop(ssp_client_t *c)
{
	int64_t last_hb = ssp_clock_ms();

	while (c->running) {
		ssp_pollfd_t pfd;
		memset(&pfd, 0, sizeof(pfd));
		pfd.fd = c->sock;
		pfd.events = SSP_POLLIN;

		int ret = ssp_poll(&pfd, 1, HEARTBEAT_INTERVAL_MS);

		if (ret < 0) {
			if (ssp_errno_intr())
				continue;
			ssp_log("poll error: %d", ssp_errno());
			return -1;
		}

		/* Send heartbeat if interval elapsed */
		int64_t now = ssp_clock_ms();
		if (now - last_hb >= HEARTBEAT_INTERVAL_MS) {
			do_send_heartbeat(c);
			last_hb = now;
		}

		if (ret == 0)
			continue;

		if (pfd.revents & (SSP_POLLERR | SSP_POLLHUP)) {
			ssp_log("connection error/hangup");
			return -1;
		}

		if (!(pfd.revents & SSP_POLLIN))
			continue;

		uint32_t pkt_len;
		uint8_t *pkt = ssp_recv_packet(c, &pkt_len);
		if (!pkt)
			return -1;

		if (pkt_len == 0) {
			free(pkt);
			continue;
		}

		switch (pkt[0]) {
		case SSP_PKT_METADATA:
			handle_metadata(c, pkt, pkt_len);
			break;
		case SSP_PKT_VIDEO:
			handle_video(c, pkt, pkt_len);
			break;
		case SSP_PKT_AUDIO:
			handle_audio(c, pkt, pkt_len);
			break;
		default:
			ssp_log("unknown packet type: 0x%02x len=%u",
			        pkt[0], pkt_len);
			break;
		}

		free(pkt);
	}

	return 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Public API
 * ═══════════════════════════════════════════════════════════════════════════ */

ssp_client_t *ssp_client_create(void)
{
	ssp_client_t *c = (ssp_client_t *)calloc(1, sizeof(ssp_client_t));
	if (!c)
		return NULL;

	c->sock = SSP_INVALID_SOCKET;
	c->port = SSP_DEFAULT_PORT;
	c->stream_style = SSP_STREAM_DEFAULT;
	c->running = false;
	c->has_metadata = false;

	return c;
}

void ssp_client_destroy(ssp_client_t *c)
{
	if (!c)
		return;

	if (c->sock != SSP_INVALID_SOCKET) {
		ssp_close(c->sock);
		c->sock = SSP_INVALID_SOCKET;
	}

	ssp_platform_cleanup();
	free(c);
}

int ssp_client_set_target(ssp_client_t *c, const char *host,
                          unsigned int port)
{
	if (!c || !host || port == 0)
		return -1;

	strncpy(c->host, host, sizeof(c->host) - 1);
	c->host[sizeof(c->host) - 1] = '\0';
	c->port = port;
	return 0;
}

int ssp_client_set_stream(ssp_client_t *c, uint32_t stream_style)
{
	if (!c)
		return -1;
	c->stream_style = stream_style;
	return 0;
}

int ssp_client_set_callbacks(ssp_client_t *c, const ssp_callbacks_t *cb)
{
	if (!c || !cb)
		return -1;
	c->callbacks = *cb;
	return 0;
}

int ssp_client_connect(ssp_client_t *c)
{
	if (!c)
		return -1;

	c->running = true;

	if (do_connect(c) < 0)
		return -1;

	if (c->callbacks.on_connected)
		c->callbacks.on_connected(c->callbacks.userdata);

	if (do_handshake(c) < 0) {
		ssp_close(c->sock);
		c->sock = SSP_INVALID_SOCKET;
		return -1;
	}

	if (do_start_stream(c) < 0) {
		ssp_close(c->sock);
		c->sock = SSP_INVALID_SOCKET;
		return -1;
	}

	int ret = stream_loop(c);

	if (c->callbacks.on_disconnected)
		c->callbacks.on_disconnected(c->callbacks.userdata);

	ssp_close(c->sock);
	c->sock = SSP_INVALID_SOCKET;

	return ret;
}

void ssp_client_stop(ssp_client_t *c)
{
	if (c)
		c->running = false;
}

const char *ssp_client_device_name(const ssp_client_t *c)
{
	return c ? c->device_name : "";
}

/* Stringify helper for version */
#define SSP_STRINGIFY_(x) #x
#define SSP_STRINGIFY(x)  SSP_STRINGIFY_(x)

const char *ssp_version_string(void)
{
	return "ssp " SSP_STRINGIFY(SSP_VERSION_MAJOR) "."
	       SSP_STRINGIFY(SSP_VERSION_MINOR) "."
	       SSP_STRINGIFY(SSP_VERSION_PATCH);
}
