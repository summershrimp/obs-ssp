/* 
 * Copyright (c) 2015-2021, Yibai Zhang
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1.  Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright notice,
 *     this list of conditions and the following disclaimer in the documentation
 *     and/or other materials provided with the distribution.
 * 3.  Neither the name of Yibai Zhang, obs-ssp, ssp_connector
 *     nor the names contributors may be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <string>

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

#include <imf/ssp/sspclient.h>
#include <imf/net/threadloop.h>

#include "main.h"
#include "ssp_connector_proto.h"

char address[256] = {0};
unsigned int port = 0;
char uuid[64] = {0};

imf::SspClient *gSspClient = nullptr;
imf::Loop *gLoop = nullptr;

int msg_write(char *buf, size_t size)
{
	Message *msg = (Message *)buf;
	size_t writed = 0, cur = 0;
	//log_conn("send msg type: %d, size %d", msg->type, msg->length);
	writed = fwrite(buf, 1, size, stdout);
	fflush(stdout);
	if (ferror(stdout)) {
		log_conn("ferror on msg_write");
		return -1;
	}
	return writed;
}

int process_args(int argc, char **argv)
{
	int t = 1;
	while (t < argc) {
		if (t + 1 >= argc) {
			return -1;
		}
		if ((!strcmp(argv[t], "-h") || !strcmp(argv[t], "--host"))) {
			++t;
			strncpy(address, argv[t], sizeof(address));
		} else if (!strcmp(argv[t], "-p") ||
			   !strcmp(argv[t], "--port")) {
			++t;
			port = strtoul(argv[t], NULL, 0);
		} else if (!strcmp(argv[t], "-u") ||
			   !strcmp(argv[t], "--uuid")) {
			++t;
			strncpy(uuid, argv[t], sizeof(uuid));
		} else {
			return -1;
		}

		++t;
	}

	if (strlen(address) == 0 || port == 0) {
		return -1;
	}
	return 0;
}

void print_usage(void)
{
	fprintf(stderr,
		"Usage: ssp_connector --host host --port port [--uuid uuid]");
}

static void on_general_message(MessageType type)
{
	Message msg;
	msg.length = 0;
	msg.type = type;
	int sz = msg_write((char *)&msg, sizeof(msg));
	if (sz != sizeof(msg)) {
		log_conn("stopped.");
		gSspClient->stop();
		gLoop->quit();
	}
}

static void on_video(imf::SspH264Data *video)
{
	size_t len = sizeof(Message) + sizeof(VideoData) + video->len;
	auto *msg = (Message *)malloc(len);
	msg->type = VideoDataMsg;
	msg->length = sizeof(VideoData) + video->len;
	auto *videoData = (VideoData *)msg->value;
	videoData->frm_no = video->frm_no;
	videoData->ntp_timestamp = video->ntp_timestamp;
	videoData->pts = video->pts;
	videoData->type = video->type;
	videoData->len = video->len;
	memcpy(videoData->data, video->data, video->len);
	int sz = msg_write((char *)msg, len);
	free(msg);
	if (sz != len) {
		log_conn("stopped.");
		gSspClient->stop();
		gLoop->quit();
	}
}

static void on_audio(imf::SspAudioData *audio)
{
	size_t len = sizeof(Message) + sizeof(AudioData) + audio->len;
	auto *msg = (Message *)malloc(len);
	msg->type = AudioDataMsg;
	msg->length = sizeof(AudioData) + audio->len;
	auto *audioData = (AudioData *)msg->value;
	audioData->ntp_timestamp = audio->ntp_timestamp;
	audioData->pts = audio->pts;
	audioData->len = audio->len;
	memcpy(audioData->data, audio->data, audio->len);

	int sz = msg_write((char *)msg, len);
	free(msg);
	if (sz != len) {
		log_conn("stopped.");
		gSspClient->stop();
		gLoop->quit();
	}
}
static void on_meta(imf::SspVideoMeta *vmeta, struct imf::SspAudioMeta *ameta,
		    struct imf::SspMeta *meta)
{
	size_t len = sizeof(Message) + sizeof(Metadata);
	auto *msg = (Message *)malloc(len);
	msg->type = MetaDataMsg;
	msg->length = sizeof(Metadata);
	auto *metadata = (Metadata *)msg->value;
	metadata->ameta.bitrate = ameta->bitrate;
	metadata->ameta.channel = ameta->channel;
	metadata->ameta.encoder = ameta->encoder;
	metadata->ameta.sample_rate = ameta->sample_rate;
	metadata->ameta.sample_size = ameta->sample_size;
	metadata->ameta.timescale = ameta->timescale;
	metadata->ameta.unit = ameta->unit;

	metadata->vmeta.encoder = vmeta->encoder;
	metadata->vmeta.gop = vmeta->gop;
	metadata->vmeta.height = vmeta->height;
	metadata->vmeta.timescale = vmeta->timescale;
	metadata->vmeta.unit = vmeta->unit;
	metadata->vmeta.width = vmeta->width;

	metadata->meta.pts_is_wall_clock = meta->pts_is_wall_clock;
	metadata->meta.tc_drop_frame = meta->tc_drop_frame;
	metadata->meta.timecode = meta->timecode;

	int sz = msg_write((char *)msg, len);
	free(msg);
	if (sz != len) {
		log_conn("stopped sz != len %d != %d.", sz, len);
		gSspClient->stop();
		gLoop->quit();
	}
}
static void on_exception(int code, const char *description)
{
	size_t len =
		sizeof(Message) + sizeof(Message) + strlen(description) + 1;
	auto *msg = (Message *)malloc(len);
	msg->type = ExceptionMsg;
	msg->length = sizeof(Message) + strlen(description) + 1;
	auto *errmsg = (Message *)msg->value;
	errmsg->length = strlen(description) + 1;
	errmsg->type = code;
	strcpy((char *)errmsg->value, description);
	int sz = msg_write((char *)msg, len);
	free(msg);
	if (sz != len) {
		log_conn("exception error.");
	}
	gSspClient->stop();
	gLoop->quit();
}

static void setup(imf::Loop *loop)
{
	auto client = new imf::SspClient(address, loop, 0x400000, port, 0);
	client->init();
	gSspClient = client;

	client->setOnH264DataCallback(on_video);
	client->setOnMetaCallback(on_meta);
	client->setOnAudioDataCallback(on_audio);
	client->setOnExceptionCallback(on_exception);
	client->setOnConnectionConnectedCallback(
		std::bind(on_general_message, ConnectionConnectedMsg));
	client->setOnRecvBufferFullCallback(
		std::bind(on_general_message, RecvBufferFullMsg));
	client->setOnDisconnectedCallback([=]() {
		on_general_message(DisconnectMsg);
		client->stop();
		loop->quit();
	});
	client->start();

	Message msg;
	msg.length = 0;
	msg.type = ConnectorOkMsg;
	int sz = msg_write((char *)&msg, sizeof(msg));

	if (sz != sizeof(msg)) {
		log_conn("stopped.");
		client->stop();
		gLoop->quit();
	}
}

int main(int argc, char **argv)
{
	int ret = process_args(argc, argv);
	if (ret) {
		print_usage();
		return -1;
	}
#ifdef _WIN32
	_setmode(_fileno(stdout), O_BINARY);
#endif
	setvbuf(stdout, NULL, _IONBF, 0);
	//setbuf(stdout, nullptr); // unbuffered stdout

	log_conn("host: %s\nport: %d\nuuid: %s\n", address, port, uuid);
	auto loop = new imf::Loop();
	loop->init();
	gLoop = loop;
	setup(gLoop);
	loop->loop();
	log_conn("loop finished");
	delete loop;
	if (gSspClient) {
		delete gSspClient;
	}
	return 0;
}
