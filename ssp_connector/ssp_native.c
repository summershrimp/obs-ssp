/*
 * ssp_native.c — Clean-room SSP (Simple Stream Protocol) client
 *
 * Drop-in replacement for the closed-source libssp-based ssp-connector.
 * Protocol reverse-engineered from:
 *   - libssp-py (https://pypi.org/project/libssp-py/)
 *   - Symbol analysis of libssp.so
 *   - obs-ssp connector IPC protocol
 *
 * Copyright (c) 2025, Hedonistic IO
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * With blackjack and hookers.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <openssl/evp.h>

#include "ssp_connector_proto.h"

#ifndef logfile
#define logfile stderr
#endif

#define log_conn(fmt, ...) \
	fprintf(logfile, "[ssp-native] %s:%d " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__)

/* ═══════════════════════════════════════════════════════════════════════════
 * SSP Wire Protocol Constants
 * ═══════════════════════════════════════════════════════════════════════════ */

#define SSP_DEFAULT_PORT       9999
#define SSP_RECV_BUF_SIZE      (4 * 1024 * 1024)  /* 0x400000 */

/* Packet type IDs (from libssp-py + symbol analysis) */
#define SSP_PKT_INIT           0x64
#define SSP_PKT_METADATA       0x6E
#define SSP_PKT_VIDEO          0x6F
#define SSP_PKT_AUDIO          0x70
#define SSP_PKT_HANDSHAKE      0xC8
#define SSP_PKT_STREAM_START   0xCA
#define SSP_PKT_OK             0x01   /* Inferred from handshake response */
#define SSP_PKT_ERROR          0x02
#define SSP_PKT_HEARTBEAT      0xCB   /* Inferred from symbol analysis */

/* Stream styles */
#define STREAM_DEFAULT  0
#define STREAM_MAIN     1
#define STREAM_SEC      2

/* Encoder IDs */
#define VIDEO_ENCODER_H264  96
#define VIDEO_ENCODER_H265  265
#define AUDIO_ENCODER_AAC   37
#define AUDIO_ENCODER_PCM   23

/* Hardcoded auth constants (from libssp-py reverse engineering) */
static const char SSP_USERNAME[] = "zcam-live-user";
static const char SSP_PASSWORD[] = "zcam-live-password";

/* Pre-computed: SHA1(SSP_PASSWORD) */
static const uint8_t PASSWORD_HASH[20] = {
	0x41, 0x67, 0x7f, 0x4a, 0x21, 0x55, 0xc6, 0x05,
	0x61, 0xed, 0xe4, 0xa1, 0x68, 0x42, 0xd0, 0x1a,
	0xd1, 0x0e, 0x73, 0xf5
};

/* Pre-computed: SHA1(SHA1(SSP_PASSWORD)) */
static const uint8_t PASSWORD_HASH_HASH[20] = {
	0x0d, 0xc5, 0x27, 0x59, 0x89, 0x82, 0xf9, 0x5b,
	0x58, 0x1e, 0x61, 0x64, 0xd3, 0xe3, 0x7b, 0x65,
	0x69, 0x52, 0x1e, 0xef
};

/* Heartbeat interval in milliseconds */
#define HEARTBEAT_INTERVAL_MS  3000

/* ═══════════════════════════════════════════════════════════════════════════
 * Globals
 * ═══════════════════════════════════════════════════════════════════════════ */

static int g_sock = -1;
static volatile bool g_running = true;
static char g_host[256] = {0};
static unsigned int g_port = SSP_DEFAULT_PORT;
static uint32_t g_stream_style = STREAM_DEFAULT;

/* ═══════════════════════════════════════════════════════════════════════════
 * Signal handling
 * ═══════════════════════════════════════════════════════════════════════════ */

static void signal_handler(int sig)
{
	(void)sig;
	g_running = false;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Byte helpers
 * ═══════════════════════════════════════════════════════════════════════════ */

static inline uint32_t read_be32(const uint8_t *p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
	       ((uint32_t)p[2] << 8)  | (uint32_t)p[3];
}

static inline uint64_t read_be64(const uint8_t *p)
{
	return ((uint64_t)read_be32(p) << 32) | read_be32(p + 4);
}

static inline void write_be32(uint8_t *p, uint32_t v)
{
	p[0] = (v >> 24) & 0xFF;
	p[1] = (v >> 16) & 0xFF;
	p[2] = (v >> 8)  & 0xFF;
	p[3] = v & 0xFF;
}

static inline void write_be16(uint8_t *p, uint16_t v)
{
	p[0] = (v >> 8) & 0xFF;
	p[1] = v & 0xFF;
}

static inline uint16_t read_be16(const uint8_t *p)
{
	return ((uint16_t)p[0] << 8) | (uint16_t)p[1];
}

/* Swap endianness in 4-byte groups (from libssp-py) */
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

/* Read exactly n bytes from socket. Returns 0 on success, -1 on error/EOF. */
static int recv_exact(int sock, void *buf, size_t n)
{
	uint8_t *p = (uint8_t *)buf;
	size_t remaining = n;

	while (remaining > 0 && g_running) {
		ssize_t r = recv(sock, p, remaining, 0);
		if (r <= 0) {
			if (r == 0) {
				log_conn("connection closed by peer");
			} else if (errno == EINTR) {
				continue;
			} else {
				log_conn("recv error: %s", strerror(errno));
			}
			return -1;
		}
		p += r;
		remaining -= (size_t)r;
	}
	return g_running ? 0 : -1;
}

/* Send exactly n bytes. Returns 0 on success, -1 on error. */
static int send_exact(int sock, const void *buf, size_t n)
{
	const uint8_t *p = (const uint8_t *)buf;
	size_t remaining = n;

	while (remaining > 0) {
		ssize_t w = send(sock, p, remaining, MSG_NOSIGNAL);
		if (w <= 0) {
			if (w < 0 && errno == EINTR)
				continue;
			log_conn("send error: %s", strerror(errno));
			return -1;
		}
		p += w;
		remaining -= (size_t)w;
	}
	return 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * SSP Packet I/O
 * Wire format: [4 bytes big-endian length] [payload]
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Receive one SSP packet. Caller must free() the returned buffer. */
static uint8_t *ssp_recv_packet(int sock, uint32_t *out_len)
{
	uint8_t len_buf[4];
	if (recv_exact(sock, len_buf, 4) < 0)
		return NULL;

	uint32_t pkt_len = read_be32(len_buf);
	if (pkt_len == 0 || pkt_len > SSP_RECV_BUF_SIZE) {
		log_conn("invalid packet length: %u", pkt_len);
		return NULL;
	}

	uint8_t *data = (uint8_t *)malloc(pkt_len);
	if (!data) {
		log_conn("malloc failed for %u bytes", pkt_len);
		return NULL;
	}

	if (recv_exact(sock, data, pkt_len) < 0) {
		free(data);
		return NULL;
	}

	*out_len = pkt_len;
	return data;
}

/* Send an SSP packet with length prefix. */
static int ssp_send_packet(int sock, const void *data, uint32_t len)
{
	uint8_t len_buf[4];
	write_be32(len_buf, len);
	if (send_exact(sock, len_buf, 4) < 0)
		return -1;
	if (len > 0 && send_exact(sock, data, len) < 0)
		return -1;
	return 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * SSP Authentication
 *
 * Flow:
 *   1. Server sends INIT packet with challenge bytes
 *   2. Client computes: token = SHA1(password) XOR swap_endian(SHA1(challenge + SHA1(SHA1(password))))
 *   3. Client sends HANDSHAKE packet with username + token
 * ═══════════════════════════════════════════════════════════════════════════ */

static void compute_auth_token(const uint8_t *challenge, size_t challenge_len,
                               uint8_t *token_out)
{
	/* SHA1(challenge + PASSWORD_HASH_HASH) */
	uint8_t digest[20];
	unsigned int digest_len = 0;

	EVP_MD_CTX *ctx = EVP_MD_CTX_new();
	EVP_DigestInit_ex(ctx, EVP_sha1(), NULL);
	EVP_DigestUpdate(ctx, challenge, challenge_len);
	EVP_DigestUpdate(ctx, PASSWORD_HASH_HASH, sizeof(PASSWORD_HASH_HASH));
	EVP_DigestFinal_ex(ctx, digest, &digest_len);
	EVP_MD_CTX_free(ctx);

	/* Swap endianness in 4-byte groups */
	swap_endian_4(digest, 20);

	/* XOR with PASSWORD_HASH to get token */
	for (int i = 0; i < 20; i++) {
		token_out[i] = PASSWORD_HASH[i] ^ digest[i];
	}
}

/* ═══════════════════════════════════════════════════════════════════════════
 * IPC output to obs-ssp plugin (stdout pipe)
 * Format: Message header + payload
 * ═══════════════════════════════════════════════════════════════════════════ */

static int msg_write(const void *buf, size_t size)
{
	size_t written = fwrite(buf, 1, size, stdout);
	fflush(stdout);
	if (ferror(stdout)) {
		log_conn("ferror on msg_write");
		return -1;
	}
	return (int)written;
}

static int send_simple_msg(enum MessageType type)
{
	struct Message msg;
	msg.type = type;
	msg.length = 0;
	int sz = msg_write(&msg, sizeof(msg));
	if (sz != sizeof(msg)) {
		log_conn("failed to send message type %d", type);
		return -1;
	}
	return 0;
}

static int send_metadata_msg(const uint8_t *pkt, uint32_t pkt_len)
{
	/*
	 * Metadata packet layout (from wire analysis):
	 * [0]    packet type (0x6E)
	 * [1..2] unknown/flags
	 * [3..6] video width (BE32)
	 * [7..10] video height (BE32)
	 * [11..14] video timescale (BE32)
	 * [15..18] video unit (BE32)
	 * [19..22] video gop (BE32)
	 * [23..26] video encoder (BE32)
	 * [27..30] audio timescale (BE32)
	 * [31..34] audio unit (BE32)
	 * [35..38] audio sample_rate (BE32)
	 * [39..42] audio sample_size (BE32)
	 * [43..46] audio channel (BE32)
	 * [47..50] audio bitrate (BE32)
	 * [51..54] audio encoder (BE32)
	 * [55]     pts_is_wall_clock (bool)
	 * [56]     tc_drop_frame (bool)
	 * [57..60] timecode (BE32)
	 *
	 * Note: exact offsets may vary. We parse conservatively and log values.
	 */

	if (pkt_len < 55) {
		log_conn("metadata packet too short: %u bytes", pkt_len);
		return -1;
	}

	size_t off = 3; /* skip type byte + 2 flag bytes */

	struct Metadata md;
	memset(&md, 0, sizeof(md));

	/* Video meta */
	md.vmeta.width     = read_be32(pkt + off); off += 4;
	md.vmeta.height    = read_be32(pkt + off); off += 4;
	md.vmeta.timescale = read_be32(pkt + off); off += 4;
	md.vmeta.unit      = read_be32(pkt + off); off += 4;
	md.vmeta.gop       = read_be32(pkt + off); off += 4;
	md.vmeta.encoder   = read_be32(pkt + off); off += 4;

	/* Audio meta */
	md.ameta.timescale   = read_be32(pkt + off); off += 4;
	md.ameta.unit        = read_be32(pkt + off); off += 4;
	md.ameta.sample_rate = read_be32(pkt + off); off += 4;
	md.ameta.sample_size = read_be32(pkt + off); off += 4;
	md.ameta.channel     = read_be32(pkt + off); off += 4;
	md.ameta.bitrate     = read_be32(pkt + off); off += 4;
	md.ameta.encoder     = read_be32(pkt + off); off += 4;

	/* Base meta */
	if (off + 6 <= pkt_len) {
		md.meta.pts_is_wall_clock = pkt[off++];
		md.meta.tc_drop_frame     = pkt[off++];
		if (off + 4 <= pkt_len)
			md.meta.timecode = read_be32(pkt + off);
	}

	log_conn("metadata: video=%ux%u enc=%u gop=%u, audio=%uhz ch=%u enc=%u",
	         md.vmeta.width, md.vmeta.height, md.vmeta.encoder, md.vmeta.gop,
	         md.ameta.sample_rate, md.ameta.channel, md.ameta.encoder);

	size_t msg_len = sizeof(struct Message) + sizeof(struct Metadata);
	struct Message *msg = (struct Message *)malloc(msg_len);
	msg->type = MetaDataMsg;
	msg->length = sizeof(struct Metadata);
	memcpy(msg->value, &md, sizeof(md));

	int sz = msg_write(msg, msg_len);
	free(msg);
	return (sz == (int)msg_len) ? 0 : -1;
}

static int send_video_msg(const uint8_t *pkt, uint32_t pkt_len)
{
	/*
	 * Video packet layout:
	 * [0]      packet type (0x6F)
	 * [1..2]   flags/unknown
	 * [3..10]  pts (BE64)
	 * [11..18] ntp_timestamp (BE64)
	 * [19..22] frm_no (BE32)
	 * [23..26] type (BE32) — 5 = I-frame
	 * [27..]   H.264/H.265 NAL data
	 */

	if (pkt_len < 27) {
		log_conn("video packet too short: %u", pkt_len);
		return -1;
	}

	size_t off = 3;
	uint64_t pts           = read_be64(pkt + off); off += 8;
	uint64_t ntp_timestamp = read_be64(pkt + off); off += 8;
	uint32_t frm_no        = read_be32(pkt + off); off += 4;
	uint32_t type          = read_be32(pkt + off); off += 4;

	size_t data_len = pkt_len - off;

	size_t msg_len = sizeof(struct Message) + sizeof(struct VideoData) + data_len;
	struct Message *msg = (struct Message *)malloc(msg_len);
	if (!msg) return -1;

	msg->type = VideoDataMsg;
	msg->length = sizeof(struct VideoData) + data_len;

	struct VideoData *vd = (struct VideoData *)msg->value;
	vd->pts = pts;
	vd->ntp_timestamp = ntp_timestamp;
	vd->frm_no = frm_no;
	vd->type = type;
	vd->len = data_len;
	memcpy(vd->data, pkt + off, data_len);

	int sz = msg_write(msg, msg_len);
	free(msg);

	if (sz != (int)msg_len) {
		g_running = false;
		return -1;
	}
	return 0;
}

static int send_audio_msg(const uint8_t *pkt, uint32_t pkt_len)
{
	/*
	 * Audio packet layout:
	 * [0]      packet type (0x70)
	 * [1..2]   flags/unknown
	 * [3..10]  pts (BE64)
	 * [11..18] ntp_timestamp (BE64)
	 * [19..]   audio data
	 */

	if (pkt_len < 19) {
		log_conn("audio packet too short: %u", pkt_len);
		return -1;
	}

	size_t off = 3;
	uint64_t pts           = read_be64(pkt + off); off += 8;
	uint64_t ntp_timestamp = read_be64(pkt + off); off += 8;

	size_t data_len = pkt_len - off;

	size_t msg_len = sizeof(struct Message) + sizeof(struct AudioData) + data_len;
	struct Message *msg = (struct Message *)malloc(msg_len);
	if (!msg) return -1;

	msg->type = AudioDataMsg;
	msg->length = sizeof(struct AudioData) + data_len;

	struct AudioData *ad = (struct AudioData *)msg->value;
	ad->pts = pts;
	ad->ntp_timestamp = ntp_timestamp;
	ad->len = data_len;
	memcpy(ad->data, pkt + off, data_len);

	int sz = msg_write(msg, msg_len);
	free(msg);

	if (sz != (int)msg_len) {
		g_running = false;
		return -1;
	}
	return 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * SSP Connection Logic
 * ═══════════════════════════════════════════════════════════════════════════ */

static int ssp_connect(const char *host, unsigned int port)
{
	int sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0) {
		log_conn("socket() failed: %s", strerror(errno));
		return -1;
	}

	/* TCP_NODELAY for minimum latency */
	int flag = 1;
	setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

	/* Increase receive buffer */
	int rcvbuf = SSP_RECV_BUF_SIZE;
	setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons((uint16_t)port);

	if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
		log_conn("invalid address: %s", host);
		close(sock);
		return -1;
	}

	/* Connect with timeout */
	struct timeval tv = {.tv_sec = 5, .tv_usec = 0};
	setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

	if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		log_conn("connect to %s:%u failed: %s", host, port, strerror(errno));
		close(sock);
		return -1;
	}

	/* Clear the send timeout after connect */
	tv.tv_sec = 0;
	setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

	log_conn("connected to %s:%u", host, port);
	return sock;
}

static int ssp_handshake(int sock)
{
	uint32_t pkt_len;
	uint8_t *pkt;

	/* === Step 1: Receive INIT packet === */
	pkt = ssp_recv_packet(sock, &pkt_len);
	if (!pkt) {
		log_conn("failed to receive INIT packet");
		return -1;
	}

	if (pkt[0] != SSP_PKT_INIT) {
		log_conn("expected INIT (0x%02x), got 0x%02x", SSP_PKT_INIT, pkt[0]);
		free(pkt);
		return -1;
	}

	/* Extract device name (null-terminated string starting at offset 8) */
	char device_name[128] = {0};
	size_t name_start = 8;
	if (name_start < pkt_len) {
		size_t name_len = strnlen((char *)(pkt + name_start), pkt_len - name_start);
		if (name_len > sizeof(device_name) - 1)
			name_len = sizeof(device_name) - 1;
		memcpy(device_name, pkt + name_start, name_len);
	}
	log_conn("device: %s", device_name);

	/* Extract challenge (last 20 bytes of INIT, per libssp-py: init[43:]) */
	if (pkt_len < 43 + 20) {
		log_conn("INIT packet too short for challenge: %u bytes", pkt_len);
		free(pkt);
		return -1;
	}

	uint8_t challenge[20];
	memcpy(challenge, pkt + 43, 20);
	free(pkt);

	/* === Step 2: Compute and send HANDSHAKE === */
	uint8_t token[20];
	compute_auth_token(challenge, 20, token);

	/*
	 * Handshake packet (from libssp-py):
	 * [0..1] 0x00 0x00 (padding)
	 * [2..3] 0x00 0x28 (total payload length = 40)
	 * [4]    0xC8 (HANDSHAKE type)
	 * [5..6] 0x00 0x00 (flags)
	 * [7..21] "zcam-live-user\0" (15 bytes)
	 * [22..23] 0x00 0x14 (token length = 20)
	 * [24..43] token (20 bytes)
	 * Total: 44 bytes
	 */
	uint8_t handshake[44];
	memset(handshake, 0, sizeof(handshake));

	handshake[0] = 0x00;
	handshake[1] = 0x00;
	handshake[2] = 0x00;
	handshake[3] = 0x28; /* 40 bytes of payload */
	handshake[4] = SSP_PKT_HANDSHAKE;
	handshake[5] = 0x00;
	handshake[6] = 0x00;
	memcpy(handshake + 7, SSP_USERNAME, strlen(SSP_USERNAME) + 1); /* includes null */
	handshake[22] = 0x00;
	handshake[23] = 0x14; /* 20 = token length */
	memcpy(handshake + 24, token, 20);

	if (send_exact(sock, handshake, sizeof(handshake)) < 0) {
		log_conn("failed to send HANDSHAKE");
		return -1;
	}

	log_conn("sent handshake");

	/* === Step 3: Receive HANDSHAKE response === */
	pkt = ssp_recv_packet(sock, &pkt_len);
	if (!pkt) {
		log_conn("failed to receive handshake response");
		return -1;
	}

	log_conn("handshake response: type=0x%02x len=%u", pkt[0], pkt_len);

	/* Check for error (type 0x02 = error) */
	if (pkt_len > 0 && pkt[0] == SSP_PKT_ERROR) {
		log_conn("handshake rejected by camera");
		free(pkt);
		return -1;
	}

	free(pkt);
	log_conn("handshake complete");
	return 0;
}

static int ssp_start_stream(int sock, uint32_t stream_style)
{
	/*
	 * Start packet (from libssp-py):
	 * [0]    0xCA (STREAM_START type)
	 * [1..4] stream_style (BE32)
	 * [5..8] 0x00000000 (flags/reserved)
	 * Total: 9 bytes
	 */
	uint8_t start_pkt[9];
	start_pkt[0] = SSP_PKT_STREAM_START;
	write_be32(start_pkt + 1, stream_style);
	write_be32(start_pkt + 5, 0);

	if (ssp_send_packet(sock, start_pkt, sizeof(start_pkt)) < 0) {
		log_conn("failed to send STREAM_START");
		return -1;
	}

	log_conn("sent stream start (style=%u)", stream_style);
	return 0;
}

static int ssp_send_heartbeat(int sock)
{
	/* Heartbeat is an empty packet or minimal packet */
	uint8_t hb[1] = { SSP_PKT_HEARTBEAT };
	return ssp_send_packet(sock, hb, sizeof(hb));
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Main streaming loop
 * ═══════════════════════════════════════════════════════════════════════════ */

static int ssp_stream_loop(int sock)
{
	struct timespec last_heartbeat;
	clock_gettime(CLOCK_MONOTONIC, &last_heartbeat);

	while (g_running) {
		/* Use poll() so we can send heartbeats between receives */
		struct pollfd pfd = {
			.fd = sock,
			.events = POLLIN,
		};

		int ret = poll(&pfd, 1, HEARTBEAT_INTERVAL_MS);

		if (ret < 0) {
			if (errno == EINTR)
				continue;
			log_conn("poll error: %s", strerror(errno));
			return -1;
		}

		/* Send heartbeat if interval elapsed */
		struct timespec now;
		clock_gettime(CLOCK_MONOTONIC, &now);
		long elapsed_ms = (now.tv_sec - last_heartbeat.tv_sec) * 1000 +
		                  (now.tv_nsec - last_heartbeat.tv_nsec) / 1000000;
		if (elapsed_ms >= HEARTBEAT_INTERVAL_MS) {
			ssp_send_heartbeat(sock);
			last_heartbeat = now;
		}

		if (ret == 0)
			continue; /* timeout, just heartbeat */

		if (pfd.revents & (POLLERR | POLLHUP)) {
			log_conn("connection error/hangup");
			return -1;
		}

		if (!(pfd.revents & POLLIN))
			continue;

		/* Receive a packet */
		uint32_t pkt_len;
		uint8_t *pkt = ssp_recv_packet(sock, &pkt_len);
		if (!pkt) {
			return -1;
		}

		if (pkt_len == 0) {
			free(pkt);
			continue;
		}

		uint8_t pkt_type = pkt[0];

		switch (pkt_type) {
		case SSP_PKT_METADATA:
			send_metadata_msg(pkt, pkt_len);
			break;

		case SSP_PKT_VIDEO:
			send_video_msg(pkt, pkt_len);
			break;

		case SSP_PKT_AUDIO:
			send_audio_msg(pkt, pkt_len);
			break;

		default:
			log_conn("unknown packet type: 0x%02x len=%u", pkt_type, pkt_len);
			break;
		}

		free(pkt);
	}

	return 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * CLI
 * ═══════════════════════════════════════════════════════════════════════════ */

static int process_args(int argc, char **argv)
{
	int t = 1;
	while (t < argc) {
		if (t + 1 >= argc)
			return -1;

		if (!strcmp(argv[t], "-h") || !strcmp(argv[t], "--host")) {
			++t;
			strncpy(g_host, argv[t], sizeof(g_host) - 1);
		} else if (!strcmp(argv[t], "-p") || !strcmp(argv[t], "--port")) {
			++t;
			g_port = (unsigned int)strtoul(argv[t], NULL, 0);
		} else if (!strcmp(argv[t], "-s") || !strcmp(argv[t], "--stream")) {
			++t;
			g_stream_style = (uint32_t)strtoul(argv[t], NULL, 0);
		} else if (!strcmp(argv[t], "-u") || !strcmp(argv[t], "--uuid")) {
			++t;
			/* UUID accepted for compat but unused */
		} else {
			return -1;
		}
		++t;
	}

	if (strlen(g_host) == 0 || g_port == 0)
		return -1;

	return 0;
}

static void print_usage(void)
{
	fprintf(stderr,
	        "Usage: ssp-connector --host HOST --port PORT [--stream 0|1|2] [--uuid UUID]\n"
	        "\n"
	        "Native SSP (Simple Stream Protocol) client for Z CAM cameras.\n"
	        "Outputs video/audio frames to stdout for the obs-ssp plugin.\n"
	        "\n"
	        "  --host, -h    Camera IP address\n"
	        "  --port, -p    Camera SSP port (default: 9999)\n"
	        "  --stream, -s  Stream style: 0=default, 1=main, 2=secondary\n"
	        "  --uuid, -u    UUID (accepted for compatibility, unused)\n");
}

int main(int argc, char **argv)
{
	if (process_args(argc, argv) < 0) {
		print_usage();
		return 1;
	}

	/* Unbuffered stdout for IPC pipe */
	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(logfile, NULL, _IONBF, 0);

	signal(SIGPIPE, SIG_IGN);
	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);

	log_conn("host: %s, port: %u, stream: %u", g_host, g_port, g_stream_style);

	/* Connect */
	g_sock = ssp_connect(g_host, g_port);
	if (g_sock < 0) {
		send_simple_msg(ExceptionMsg);
		return 1;
	}

	/* Handshake */
	if (ssp_handshake(g_sock) < 0) {
		send_simple_msg(ExceptionMsg);
		close(g_sock);
		return 1;
	}

	/* Tell the plugin we're ready */
	send_simple_msg(ConnectorOkMsg);

	/* Start streaming */
	if (ssp_start_stream(g_sock, g_stream_style) < 0) {
		send_simple_msg(ExceptionMsg);
		close(g_sock);
		return 1;
	}

	send_simple_msg(ConnectionConnectedMsg);

	/* Main loop */
	int ret = ssp_stream_loop(g_sock);

	/* Cleanup */
	send_simple_msg(DisconnectMsg);
	close(g_sock);
	g_sock = -1;

	log_conn("finished (ret=%d)", ret);
	return ret < 0 ? 1 : 0;
}
