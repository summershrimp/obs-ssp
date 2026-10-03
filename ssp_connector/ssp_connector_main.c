/*
 * ssp_connector_main.c — CLI wrapper for the SSP client library
 *
 * Bridges the ssp_client_t library to the obs-ssp plugin IPC protocol.
 * Receives SSP frames via callbacks and writes packed Message structs
 * to stdout for consumption by the OBS plugin.
 *
 * Copyright (c) 2026, Hedonistic, LLC
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

#include "include/ssp/ssp.h"
#include "ssp_connector_proto.h"

#ifndef logfile
#define logfile stderr
#endif

#define log_conn(fmt, ...) \
	fprintf(logfile, "[ssp-connector] %s:%d " fmt "\n", \
	        __FILE__, __LINE__, ##__VA_ARGS__)

/* ═══════════════════════════════════════════════════════════════════════════
 * Globals (CLI only — the library itself has none)
 * ═══════════════════════════════════════════════════════════════════════════ */

static ssp_client_t *g_client = NULL;
static volatile sig_atomic_t stop_requested = 0;

/* ═══════════════════════════════════════════════════════════════════════════
 * IPC output to obs-ssp plugin (stdout pipe)
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
	if (sz != (int)sizeof(msg)) {
		log_conn("failed to send message type %d", type);
		return -1;
	}
	return 0;
}

/* ExceptionMsg carries a nested Message followed by a terminated description. */
static int send_exception(uint32_t code, const char *description)
{
	size_t description_len = strlen(description) + 1;
	size_t payload_len = sizeof(struct Message) + description_len;
	struct Message *msg = malloc(sizeof(*msg) + payload_len);
	if (!msg)
		return -1;
	msg->type = ExceptionMsg;
	msg->length = (uint32_t)payload_len;
	struct Message *error = (struct Message *)msg->value;
	error->type = code;
	error->length = (uint32_t)description_len;
	memcpy(error->value, description, description_len);
	int result = msg_write(msg, sizeof(*msg) + payload_len);
	free(msg);
	return result == (int)(sizeof(struct Message) + payload_len) ? 0 : -1;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * SSP Callbacks → IPC Messages
 * ═══════════════════════════════════════════════════════════════════════════ */

static void on_metadata(const ssp_metadata_t *meta, void *userdata)
{
	(void)userdata;

	struct Metadata md;
	memset(&md, 0, sizeof(md));

	md.vmeta.timescale = meta->video.timescale;
	md.vmeta.unit = meta->video.unit;
	md.vmeta.width = meta->video.width;
	md.vmeta.height = meta->video.height;
	md.vmeta.gop = meta->video.gop;
	md.vmeta.encoder = meta->video.encoder;

	md.ameta.sample_rate = meta->audio.sample_rate;
	md.ameta.unit = meta->audio.unit;
	md.ameta.timescale = meta->audio.timescale;
	md.ameta.sample_size = meta->audio.sample_size;
	md.ameta.channel = meta->audio.channel;
	md.ameta.bitrate = meta->audio.bitrate;
	md.ameta.encoder = meta->audio.encoder;

	md.meta.pts_is_wall_clock = meta->base.pts_is_wall_clock;
	md.meta.timecode = meta->base.timecode;
	md.meta.tc_drop_frame = meta->base.tc_drop_frame;

	size_t msg_len = sizeof(struct Message) + sizeof(struct Metadata);
	struct Message *msg = (struct Message *)malloc(msg_len);
	if (!msg)
		return;

	msg->type = MetaDataMsg;
	msg->length = sizeof(struct Metadata);
	memcpy(msg->value, &md, sizeof(md));

	msg_write(msg, msg_len);
	free(msg);
}

static void on_video(const ssp_video_frame_t *frame, void *userdata)
{
	(void)userdata;

	size_t msg_len = sizeof(struct Message) + sizeof(struct VideoData) + frame->data_len;
	struct Message *msg = (struct Message *)malloc(msg_len);
	if (!msg) {
		ssp_client_stop(g_client);
		return;
	}

	msg->type = VideoDataMsg;
	msg->length = (uint32_t)(sizeof(struct VideoData) + frame->data_len);

	struct VideoData *vd = (struct VideoData *)msg->value;
	vd->pts = frame->pts;
	vd->ntp_timestamp = 0;
	vd->frm_no = frame->frame_number;
	vd->type = frame->frame_type;
	vd->len = frame->data_len;
	memcpy(vd->data, frame->data, frame->data_len);

	int sz = msg_write(msg, msg_len);
	free(msg);

	if (sz != (int)msg_len)
		ssp_client_stop(g_client);
}

static void on_audio(const ssp_audio_frame_t *frame, void *userdata)
{
	(void)userdata;

	size_t msg_len = sizeof(struct Message) + sizeof(struct AudioData) + frame->data_len;
	struct Message *msg = (struct Message *)malloc(msg_len);
	if (!msg) {
		ssp_client_stop(g_client);
		return;
	}

	msg->type = AudioDataMsg;
	msg->length = (uint32_t)(sizeof(struct AudioData) + frame->data_len);

	struct AudioData *ad = (struct AudioData *)msg->value;
	ad->pts = frame->pts;
	ad->ntp_timestamp = 0;
	ad->len = frame->data_len;
	memcpy(ad->data, frame->data, frame->data_len);

	int sz = msg_write(msg, msg_len);
	free(msg);

	if (sz != (int)msg_len)
		ssp_client_stop(g_client);
}

static void on_connected(void *userdata)
{
	(void)userdata;
	send_simple_msg(ConnectionConnectedMsg);
}

static void on_disconnected(void *userdata)
{
	(void)userdata;
	send_simple_msg(DisconnectMsg);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Signal Handling
 * ═══════════════════════════════════════════════════════════════════════════ */

static void signal_handler(int sig)
{
	(void)sig;
	stop_requested = 1;
}

static bool should_stop(void *userdata)
{
	(void)userdata;
	return stop_requested != 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * CLI
 * ═══════════════════════════════════════════════════════════════════════════ */

static void print_usage(void)
{
	fprintf(stderr,
		"Usage: ssp-connector --host HOST --port PORT "
		"[--stream 0|1|2] [--uuid UUID]\n"
		"\n"
		"Native SSP (Simple Stream Protocol) client for Z CAM cameras.\n"
		"Outputs video/audio frames to stdout for the obs-ssp plugin.\n"
		"\n"
		"  --host, -h    Camera IP address\n"
		"  --port, -p    Camera SSP port (default: 9999)\n"
		"  --stream, -s  Stream style: 0=default, 1=main, 2=secondary\n"
		"  --uuid, -u    UUID (accepted for compatibility, unused)\n"
		"\n"
		"Version: %s\n",
		ssp_version_string());
}

int main(int argc, char **argv)
{
	char host[256] = {0};
	unsigned int port = 9999;
	uint32_t stream_style = SSP_STREAM_DEFAULT;

	/* Parse arguments */
	int t = 1;
	while (t < argc) {
		if (t + 1 >= argc) {
			print_usage();
			return 1;
		}

		if (!strcmp(argv[t], "-h") || !strcmp(argv[t], "--host")) {
			++t;
			strncpy(host, argv[t], sizeof(host) - 1);
		} else if (!strcmp(argv[t], "-p") || !strcmp(argv[t], "--port")) {
			++t;
			port = (unsigned int)strtoul(argv[t], NULL, 0);
		} else if (!strcmp(argv[t], "-s") || !strcmp(argv[t], "--stream")) {
			++t;
			stream_style = (uint32_t)strtoul(argv[t], NULL, 0);
		} else if (!strcmp(argv[t], "-u") || !strcmp(argv[t], "--uuid")) {
			++t; /* accepted for compat, unused */
		} else {
			print_usage();
			return 1;
		}
		++t;
	}

	if (strlen(host) == 0 || port == 0) {
		print_usage();
		return 1;
	}

	/* Windows text mode would rewrite bytes in the packed IPC stream. */
#ifdef _WIN32
	if (_setmode(_fileno(stdout), _O_BINARY) == -1)
		return 1;
#endif
	/* Unbuffered stdout for IPC pipe */
	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(logfile, NULL, _IONBF, 0);

#ifndef _WIN32
	signal(SIGPIPE, SIG_IGN);
#endif
	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);

	log_conn("host: %s, port: %u, stream: %u", host, port, stream_style);

	/* Create SSP client */
	g_client = ssp_client_create();
	if (!g_client) {
		log_conn("failed to create SSP client");
		return 1;
	}

	ssp_client_set_target(g_client, host, port);
	ssp_client_set_stream(g_client, stream_style);

	ssp_callbacks_t cb = {
		.on_metadata = on_metadata,
		.on_video = on_video,
		.on_audio = on_audio,
		.on_connected = on_connected,
		.on_disconnected = on_disconnected,
		.userdata = NULL,
		.should_stop = should_stop,
	};
	ssp_client_set_callbacks(g_client, &cb);

	/* The receiver requires this before any other IPC message, including errors. */
	if (send_simple_msg(ConnectorOkMsg) < 0) {
		ssp_client_destroy(g_client);
		g_client = NULL;
		return 1;
	}

	/* Connect and stream (blocks until stop or disconnect) */
	int ret = ssp_client_connect(g_client);

	if (ret < 0 && !stop_requested)
		send_exception(1, "SSP connection, authentication, or stream receive failed");

	/* Send ConnectionConnected after successful handshake is now
	 * handled inside the library via on_connected callback.
	 * The disconnect message is sent via on_disconnected callback. */

	ssp_client_destroy(g_client);
	g_client = NULL;

	log_conn("finished (ret=%d)", ret);
	return ret < 0 ? 1 : 0;
}
