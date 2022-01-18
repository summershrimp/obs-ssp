/*
obs-ssp
 Copyright (C) 2019-2020 Yibai Zhang

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; If not, see <https://www.gnu.org/licenses/>
*/

#include <obs.h>
#include <util/dstr.h>
#include <util/platform.h>

#ifdef _WIN32
#include <windows.h>
#endif

#if defined(__APPLE__)
#include <dlfcn.h>
#endif

#include <QUuid>
#include <QFileInfo>
#include <QDir>
#include <pthread.h>

#include "obs-ssp.h"
#include "ssp-client-iso.h"

static Message *msg_recv(os_process_pipe *pipe)
{
	size_t sz = 0;
	Message *msg = (Message *)bmalloc(sizeof(Message));
	if (!msg) {
		return nullptr;
	}
	sz = os_process_pipe_read(pipe, (uint8_t *)msg, sizeof(Message));
	if (sz != sizeof(Message)) {
		bfree(msg);
		return nullptr;
	}
	if (msg->length == 0) {
		return msg;
	}
	Message *msg_all = nullptr;
	msg_all = (Message *)bmalloc(sizeof(Message) + msg->length);
	memcpy(msg_all, msg, sizeof(Message));
	bfree(msg);
	sz = os_process_pipe_read(pipe, msg_all->value, msg_all->length);
	if (sz != msg_all->length) {
		bfree(msg_all);
		return nullptr;
	}
	return msg_all;
}

static void msg_free(Message *msg)
{
	if (msg) {
		bfree(msg);
	}
}

SSPClientIso::SSPClientIso(const std::string &ip, uint32_t bufferSize)
{
	this->ip = ip;
	this->bufferSize = bufferSize;
	this->running = false;

#if defined(__APPLE__)
	Dl_info info;
	dladdr((const void *)msg_free, &info);
	QFileInfo plugin_path(info.dli_fname);
	ssp_connector_path =
		plugin_path.dir().filePath(QStringLiteral(SSP_CONNECTOR));
#else
	ssp_connector_path = QStringLiteral(SSP_CONNECTOR);
#endif
	connect(this, SIGNAL(Start()), this, SLOT(doStart()));
}
using namespace std::placeholders;

void SSPClientIso::doStart()
{
	struct dstr cmd;

	dstr_init_copy(&cmd, ssp_connector_path.toStdString().c_str());
	dstr_insert_ch(&cmd, 0, '\"');
	dstr_cat(&cmd, "\" ");
#if defined(__APPLE__) && defined(__arm64__)
	dstr_insert(&cmd, 0, "arch -x86_64 ");
#endif
	dstr_cat(&cmd, "--host ");
	dstr_cat(&cmd, this->ip.c_str());
	dstr_cat(&cmd, " --port ");
	dstr_cat(&cmd, "9999");

	this->pipe = os_process_pipe_create(cmd.array, "r");
	blog(LOG_INFO, "Start ssp-connector at: %s", cmd.array);
	dstr_free(&cmd);

	if (!this->pipe) {
		blog(LOG_WARNING, "Start ssp-connector failed.");
		return;
	}
	this->running = true;

	pthread_t thread;
	pthread_create(&thread, nullptr, SSPClientIso::ReceiveThread, this);
	pthread_detach(thread);
}

void *SSPClientIso::ReceiveThread(void *arg)
{
	auto th = (SSPClientIso *)arg;
	Message *msg;

	msg = msg_recv(th->pipe);
	if (!msg) {
		blog(LOG_WARNING, "Receive error !");
		th->running = false;
		return nullptr;
	}
	if (msg->type != MessageType::ConnectorOkMsg) {
		blog(LOG_WARNING, "Protocol error !");
		th->Stop();
		return nullptr;
	}

	while (th->running) {
		msg = msg_recv(th->pipe);
		if (!msg) {
			blog(LOG_WARNING, "Receive error !");
			th->Stop();
			break;
		}

		switch (msg->type) {
		case MessageType::MetaDataMsg:
			th->OnMetadata((Metadata *)msg->value);
			break;
		case MessageType::VideoDataMsg:
			th->OnH264Data((VideoData *)msg->value);
			break;
		case MessageType::AudioDataMsg:
			th->OnAudioData((AudioData *)msg->value);
			break;
		case MessageType::RecvBufferFullMsg:
			th->OnRecvBufferFull();
			break;
		case MessageType::DisconnectMsg:
			th->OnDisconnected();
			break;
		case MessageType::ConnectionConnectedMsg:
			th->OnConnectionConnected();
			break;
		case MessageType::ExceptionMsg:
			th->OnException((Message *)msg->value);
			break;
		default:
			blog(LOG_WARNING, "Protocol error !");
			th->Stop();
			break;
		}

		msg_free(msg);
	}
	return nullptr;
}

void SSPClientIso::Restart()
{
	this->Stop();
	emit this->Start();
}

void SSPClientIso::Stop()
{
	blog(LOG_INFO, "ssp client stopping...");
	this->running = false;
	os_process_pipe_destroy(this->pipe);
}

void SSPClientIso::OnRecvBufferFull()
{
	this->bufferFullCallback();
}

void SSPClientIso::OnH264Data(VideoData *videoData)
{
	imf::SspH264Data video;
	video.frm_no = videoData->frm_no;
	video.ntp_timestamp = videoData->ntp_timestamp;
	video.pts = videoData->pts;
	video.type = videoData->type;
	video.len = videoData->len;
	video.data = videoData->data;
	this->h264DataCallback(&video);
}
void SSPClientIso::OnAudioData(AudioData *audioData)
{
	struct imf::SspAudioData audio;
	audio.ntp_timestamp = audioData->ntp_timestamp;
	audio.pts = audioData->pts;
	audio.len = audioData->len;
	audio.data = audioData->data;
	this->audioDataCallback(&audio);
}
void SSPClientIso::OnMetadata(Metadata *metadata)
{

	struct imf::SspVideoMeta vmeta;
	struct imf::SspAudioMeta ameta;
	struct imf::SspMeta meta;

	ameta.bitrate = metadata->ameta.bitrate;
	ameta.channel = metadata->ameta.channel;
	ameta.encoder = metadata->ameta.encoder;
	ameta.sample_rate = metadata->ameta.sample_rate;
	ameta.sample_size = metadata->ameta.sample_size;
	ameta.timescale = metadata->ameta.timescale;
	ameta.unit = metadata->ameta.unit;

	vmeta.encoder = metadata->vmeta.encoder;
	vmeta.gop = metadata->vmeta.gop;
	vmeta.height = metadata->vmeta.height;
	vmeta.timescale = metadata->vmeta.timescale;
	vmeta.unit = metadata->vmeta.unit;
	vmeta.width = metadata->vmeta.width;

	meta.pts_is_wall_clock = metadata->meta.pts_is_wall_clock;
	meta.tc_drop_frame = metadata->meta.tc_drop_frame;
	meta.timecode = metadata->meta.timecode;

	this->metaCallback(&vmeta, &ameta, &meta);
}
void SSPClientIso::OnDisconnected()
{
	this->disconnectedCallback();
}
void SSPClientIso::OnConnectionConnected()
{
	this->connectedCallback();
}
void SSPClientIso::OnException(Message *exception)
{
	this->exceptionCallback(exception->type, (char *)exception->value);
}

void SSPClientIso::setOnRecvBufferFullCallback(
	const imf::OnRecvBufferFullCallback &cb)
{
	this->bufferFullCallback = cb;
}

void SSPClientIso::setOnAudioDataCallback(const imf::OnAudioDataCallback &cb)
{
	this->audioDataCallback = cb;
}

void SSPClientIso::setOnMetaCallback(const imf::OnMetaCallback &cb)
{
	this->metaCallback = cb;
}

void SSPClientIso::setOnDisconnectedCallback(
	const imf::OnDisconnectedCallback &cb)
{
	this->disconnectedCallback = cb;
}

void SSPClientIso::setOnConnectionConnectedCallback(
	const imf::OnConnectionConnectedCallback &cb)
{
	this->connectedCallback = cb;
}

void SSPClientIso::setOnH264DataCallback(const imf::OnH264DataCallback &cb)
{
	this->h264DataCallback = cb;
}

void SSPClientIso::setOnExceptionCallback(const imf::OnExceptionCallback &cb)
{
	this->exceptionCallback = cb;
}
