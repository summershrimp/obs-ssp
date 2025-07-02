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

#include <string>
#include <string.h>
#include <stdlib.h>
#include <sstream>
#include <atomic>
#include "ssp-mdns.h"

#ifdef _WIN32
#include <Windows.h>
#endif

#include <functional>
#include <QObject>
#include <mutex>
#include <obs-module.h>
#include <obs.h>
#include <util/platform.h>
#include <util/threading.h>
#include <chrono>
#include <thread>

#include <QApplication>

#include "obs-ssp.h"
#include "imf/ISspClient.h"
#include "imf/threadloop.h"

#include "ssp-controller.h"
#include "ssp-client-iso.h"
#include "VFrameQueue.h"

extern "C" {
#include "ffmpeg-decode.h"
}

#include "ssp-toolbar.h"

#include "camera-status-manager.h"

#include <unordered_map>
#include <unordered_set>

#define PROP_SOURCE_IP "ssp_source_ip"
#define PROP_CUSTOM_SOURCE_IP "ssp_custom_source_ip"
#define PROP_NO_CHECK "ssp_no_check"
#define PROP_CHECK_IP "ssp_check_ip"

#define PROP_CUSTOM_VALUE "\x01\x02custom"

#define PROP_HW_ACCEL "ssp_recv_hw_accel"
#define PROP_SYNC "ssp_sync"
#define PROP_LATENCY "latency"
#define PROP_VIDEO_RANGE "video_range"
#define PROP_EXP_WAIT_I "exp_wait_i_frame"

#define PROP_BW_HIGHEST 0
#define PROP_BW_LOWEST 1
#define PROP_BW_AUDIO_ONLY 2

#define PROP_SYNC_INTERNAL 0
#define PROP_SYNC_SSP_TIMESTAMP 1

#define PROP_LATENCY_NORMAL 0
#define PROP_LATENCY_LOW 1

#define PROP_LED_TALLY "led_as_tally_light"
#define PROP_RESOLUTION "ssp_resolution"
#define PROP_FRAME_RATE "ssp_frame_rate"
#define PROP_LOW_NOISE "ssp_low_noise"
#define PROP_BITRATE "ssp_bitrate"
#define PROP_STREAM_INDEX "ssp_stream_index"
#define PROP_ENCODER "ssp_encoding"

#define SSP_IP_DIRECT "10.98.32.1"
#define SSP_IP_WIFI "10.98.33.1"
#define SSP_IP_USB "172.18.18.1"

using namespace std::placeholders;

struct ssp_source;

struct ssp_connection {
	SSPClientIso *client;
	ffmpeg_decode vdecoder;
	uint32_t width;
	uint32_t height;
	AVCodecID vformat;
	obs_source_frame2 frame;

	ffmpeg_decode adecoder;
	uint32_t sample_size;
	AVCodecID aformat;
	obs_source_audio audio;

	VFrameQueue *queue;
	std::atomic<bool> running;
	int i_frame_shown;
	std::atomic<int> reconnect_attempt;

	// copy from ssp_source
	char *source_ip;
	int hwaccel;
	int bitrate;
	int wait_i_frame;
	int sync_mode;
	obs_source_t *source;
	// not used
	int video_range;

	pthread_mutex_t lck;
};

struct ssp_source {
	obs_source_t *source;
	CameraStatus *cameraStatus;

	int sync_mode;
	int video_range;
	int hwaccel;
	int bitrate;
	int wait_i_frame;
	int tally;

	bool do_check;
	bool no_check;
	bool ip_checked;

	const char *source_ip;
	std::shared_ptr<ssp_connection> conn;
};

// Add a global map to track active connections
static std::mutex active_conns_mutex;
static std::unordered_map<std::string, std::weak_ptr<ssp_connection>>
	active_conns;

// Keep the active_ips for HTTP request tracking
static std::mutex active_ips_mutex;
static std::unordered_set<std::string> active_ips;

static bool is_ip_active(const std::string &ip)
{
	std::lock_guard<std::mutex> lock(active_ips_mutex);
	return active_ips.find(ip) != active_ips.end();
}

static void add_active_ip(const std::string &ip)
{
	std::lock_guard<std::mutex> lock(active_ips_mutex);
	active_ips.insert(ip);
}

static void remove_active_ip(const std::string &ip)
{
	std::lock_guard<std::mutex> lock(active_ips_mutex);
	active_ips.erase(ip);
}

static void ssp_conn_start(ssp_connection *s);
static void ssp_conn_stop(ssp_connection *s);
static void ssp_stop(ssp_source *s);
static void ssp_start(ssp_source *s);
void *thread_ssp_reconnect(void *data);

static void ssp_video_data_enqueue(struct imf::SspH264Data *video,
				   ssp_connection *s)
{
	if (!s->running) {
		return;
	}
	if (!s->queue) {
		return;
	}
	s->queue->enqueue(*video, video->pts, video->type == 5);
}

static void ssp_on_video_data(struct imf::SspH264Data *video, ssp_connection *s)
{
	if (!s->running) {
		return;
	}
	if (!ffmpeg_decode_valid(&s->vdecoder)) {
		assert(s->vformat == AV_CODEC_ID_H264 ||
		       s->vformat == AV_CODEC_ID_HEVC);
		if (ffmpeg_decode_init(&s->vdecoder, s->vformat, s->hwaccel) <
		    0) {
			ssp_blog(LOG_WARNING,
				 "Could not initialize video decoder");
			return;
		}
	}
	if (s->wait_i_frame && !s->i_frame_shown) {
		if (video->type == 5) {
			s->i_frame_shown = true;
		} else {
			return;
		}
	}

	int64_t ts = video->pts;
	bool got_output;
	bool success = ffmpeg_decode_video(&s->vdecoder, video->data,
					   video->len, &ts, VIDEO_CS_DEFAULT,
					   VIDEO_RANGE_PARTIAL, &s->frame,
					   &got_output);
	if (!success) {
		ssp_blog(LOG_WARNING, "Error decoding video");
		return;
	}

	if (got_output) {
		if (s->sync_mode == PROP_SYNC_INTERNAL) {
			s->frame.timestamp = os_gettime_ns();
		} else {
			s->frame.timestamp = (uint64_t)video->pts * 1000;
		}
		//        if (flip)
		//            frame.flip = !frame.flip;
		obs_source_output_video2(s->source, &s->frame);
	}
}

static void ssp_on_audio_data(struct imf::SspAudioData *audio,
			      ssp_connection *s)
{
	if (!s->running) {
		return;
	}
	if (!ffmpeg_decode_valid(&s->adecoder)) {
		if (ffmpeg_decode_init(&s->adecoder, s->aformat, false) < 0) {
			ssp_blog(LOG_WARNING,
				 "Could not initialize audio decoder");
			return;
		}
	}
	uint8_t *data = audio->data;
	size_t size = audio->len;
	bool got_output = false;
	do {
		bool success = ffmpeg_decode_audio(&s->adecoder, data, size,
						   &s->audio, &got_output);
		if (!success) {
			ssp_blog(LOG_WARNING, "Error decoding audio");
			return;
		}
		if (got_output) {
			if (s->sync_mode == PROP_SYNC_INTERNAL) {
				s->audio.timestamp = os_gettime_ns();
				s->audio.timestamp +=
					((uint64_t)s->audio.samples_per_sec *
					 1000000000ULL /
					 (uint64_t)s->sample_size);
			} else {
				s->audio.timestamp =
					(uint64_t)audio->pts * 1000;
			}
			if (s->running)
				obs_source_output_audio(s->source, &s->audio);
		} else {
			break;
		}

		size = 0;
		data = nullptr;
	} while (got_output);
}

static void ssp_on_meta_data(struct imf::SspVideoMeta *v,
			     struct imf::SspAudioMeta *a,
			     struct imf::SspMeta *m, ssp_connection *s)
{
	ssp_blog(
		LOG_INFO,
		"ssp v meta: encoder: %u, gop:%u, height:%u, timescale:%u, unit:%u, width:%u",
		v->encoder, v->gop, v->height, v->timescale, v->unit, v->width);
	ssp_blog(
		LOG_INFO,
		"ssp a meta: uinit: %u, timescale:%u, encoder:%u, bitrate:%u, channel:%u, sample_rate:%u, sample_size:%u",
		a->unit, a->timescale, a->encoder, a->bitrate, a->channel,
		a->sample_rate, a->sample_size);
	ssp_blog(
		LOG_INFO,
		"ssp i meta: pts_is_wall_clock: %u, tc_drop_frame:%u, timecode:%u,",
		m->pts_is_wall_clock, m->tc_drop_frame, m->timecode);
	s->vformat = v->encoder == VIDEO_ENCODER_H264 ? AV_CODEC_ID_H264
						      : AV_CODEC_ID_H265;
	s->frame.width = v->width;
	s->frame.height = v->height;
	s->sample_size = a->sample_size;
	s->audio.samples_per_sec = a->sample_rate;
	s->aformat = a->encoder == AUDIO_ENCODER_AAC ? AV_CODEC_ID_AAC
						     : AV_CODEC_ID_NONE;
}

static void ssp_on_disconnected(ssp_connection *s)
{
	ssp_blog(LOG_INFO, "ssp device disconnected.");

	// Get weak_ptr from global map
	std::weak_ptr<ssp_connection> weak_conn;
	{
		std::lock_guard<std::mutex> lock(active_conns_mutex);
		auto it = active_conns.find(s->source_ip);
		if (it != active_conns.end()) {
			weak_conn = it->second;
		}
	}

	if (s->running) {
		ssp_blog(LOG_INFO, "still running, reconnect...");

		// Set a flag that we're attempting to reconnect
		static std::atomic<bool> reconnecting(false);

		// Only allow one reconnect thread at a time
		if (!reconnecting.exchange(true)) {
			pthread_t thread;
			pthread_create(
				&thread, nullptr,
				[](void *data) -> void * {
					auto weak_conn = *static_cast<
						std::weak_ptr<ssp_connection> *>(
						data);
					delete static_cast<
						std::weak_ptr<ssp_connection> *>(
						data);

					// Try to get shared_ptr from weak_ptr
					if (auto conn = weak_conn.lock()) {
						thread_ssp_reconnect(
							conn.get());
					} else {
						ssp_blog(
							LOG_INFO,
							"Connection was destroyed before reconnect could start");
					}
					reconnecting.store(false);
					return nullptr;
				},
				new std::weak_ptr<ssp_connection>(weak_conn));
			pthread_detach(thread);
		} else {
			ssp_blog(LOG_INFO, "already reconnecting, skipping");
		}
	}
}

static void ssp_on_exception(int code, const char *description,
			     ssp_connection *s)
{
	ssp_blog(LOG_ERROR, "ssp exception %d: %s", code, description);
	//s->running = false;
}

static void ssp_start(ssp_source *s)
{
	auto conn = std::make_shared<ssp_connection>();
	conn->source = s->source;
	if (s->source_ip == nullptr || strlen(s->source_ip) == 0) {
		return;
	}
	conn->source_ip = strdup(s->source_ip);
	conn->wait_i_frame = s->wait_i_frame;
	conn->hwaccel = s->hwaccel;
	conn->bitrate = s->bitrate;
	conn->sync_mode = s->sync_mode;
	conn->video_range = s->video_range;
	conn->reconnect_attempt = 0;
	pthread_mutex_init(&conn->lck, nullptr);

	// Store weak_ptr in global map
	{
		std::lock_guard<std::mutex> lock(active_conns_mutex);
		active_conns[s->source_ip] = conn;
	}

	s->conn = conn;
	ssp_conn_start(conn.get());
}

static void ssp_conn_stop(ssp_connection *conn)
{
	ssp_blog(LOG_INFO, "Stopping ssp client...");
	pthread_mutex_lock(&conn->lck);
	conn->running = false;
	auto client = conn->client;
	auto queue = conn->queue;

	if (client) {
		client->Stop();
		delete client;
	}
	if (queue) {
		queue->stop();
		delete queue;
	}

	ssp_blog(LOG_INFO, "SSP client stopped.");

	if (ffmpeg_decode_valid(&conn->adecoder)) {
		ffmpeg_decode_free(&conn->adecoder);
	}
	if (ffmpeg_decode_valid(&conn->vdecoder)) {
		ffmpeg_decode_free(&conn->vdecoder);
	}

	ssp_blog(LOG_INFO, "SSP conn stopped.");
	pthread_mutex_unlock(&conn->lck);
	pthread_mutex_destroy(&conn->lck);
}

static void ssp_stop(ssp_source *s)
{
	if (!s) {
		return;
	}

	// Remove from active connections map
	if (s->source_ip) {
		std::lock_guard<std::mutex> lock(active_conns_mutex);
		active_conns.erase(s->source_ip);
	}

	auto conn = s->conn;
	s->conn = nullptr; // Clear shared_ptr
	if (!conn) {
		return;
	}
	ssp_conn_stop(conn.get());
	free((void *)conn->source_ip);
	// No need to bfree conn as shared_ptr will handle deletion
}

static void ssp_conn_start(ssp_connection *s)
{
	ssp_blog(LOG_INFO, "Starting ssp client...");
	assert(s->client == nullptr);
	assert(s->source != nullptr);

	std::string ip = s->source_ip;
	ssp_blog(LOG_INFO, "target ip: %s", s->source_ip);
	ssp_blog(LOG_INFO, "source bitrate: %d", s->bitrate);
	if (strlen(s->source_ip) == 0) {
		return;
	}
	pthread_mutex_lock(&s->lck);
	s->client = new SSPClientIso(ip, s->bitrate / 8);
	s->client->setOnH264DataCallback(
		std::bind(ssp_video_data_enqueue, _1, s));
	s->client->setOnAudioDataCallback(std::bind(ssp_on_audio_data, _1, s));
	s->client->setOnMetaCallback(
		std::bind(ssp_on_meta_data, _1, _2, _3, s));
	s->client->setOnConnectionConnectedCallback([s]() {
		ssp_blog(
			LOG_INFO,
			"ssp connected successfully, resetting reconnect counter from %d to 0",
			s->reconnect_attempt.load());
		s->reconnect_attempt = 0;
	});
	s->client->setOnDisconnectedCallback(std::bind(ssp_on_disconnected, s));
	s->client->setOnExceptionCallback(
		std::bind(ssp_on_exception, _1, _2, s));

	assert(s->queue == nullptr);
	s->queue = new VFrameQueue;
	s->queue->setFrameCallback(std::bind(ssp_on_video_data, _1, s));

	s->queue->start();
	emit s->client->Start();
	s->running = true;
	pthread_mutex_unlock(&s->lck);
	ssp_blog(LOG_INFO, "SSP client started.");
}

void *thread_ssp_reconnect(void *data)
{
	auto conn = static_cast<ssp_connection *>(data);

	// Calculate delay based on reconnect attempt
	int attempt = conn->reconnect_attempt++;
	int delay_seconds;
	if (attempt == 0) {
		delay_seconds = 3; // First attempt: 3 seconds
	} else if (attempt == 1) {
		delay_seconds = 6; // Second attempt: 6 seconds
	} else if (attempt == 2) {
		delay_seconds = 10; // Third attempt: 10 seconds
	} else {
		delay_seconds = 15; // All later attempts: 15 seconds
	}

	ssp_blog(LOG_INFO, "Waiting %d seconds before reconnect attempt %d...",
		 delay_seconds, attempt + 1);
	std::this_thread::sleep_for(std::chrono::seconds(delay_seconds));

	// Check if connection is still valid before proceeding
	{
		std::lock_guard<std::mutex> lock(active_conns_mutex);
		auto it = active_conns.find(conn->source_ip);
		if (it == active_conns.end() || it->second.expired()) {
			ssp_blog(
				LOG_INFO,
				"Connection was destroyed during reconnect delay");
			return nullptr;
		}
	}

	ssp_blog(LOG_INFO, "Stopping ssp client in thread_ssp_reconnect...");
	pthread_mutex_lock(&conn->lck);
	if (!conn->running) {
		pthread_mutex_unlock(&conn->lck);
		return nullptr;
	}
	auto client = conn->client;
	auto queue = conn->queue;

	if (client) {
		client->Stop();
		delete client;
		conn->client = nullptr;
	}
	if (queue) {
		queue->stop();
		delete queue;
		conn->queue = nullptr;
	}

	ssp_blog(LOG_INFO, "SSP client stopped.");

	if (ffmpeg_decode_valid(&conn->adecoder)) {
		ffmpeg_decode_free(&conn->adecoder);
	}
	if (ffmpeg_decode_valid(&conn->vdecoder)) {
		ffmpeg_decode_free(&conn->vdecoder);
	}

	ssp_blog(LOG_INFO, "SSP conn stopped.");

	//ssp_blog(LOG_INFO, "Starting ssp client...");
	assert(conn->client == nullptr);
	assert(conn->source != nullptr);

	std::string ip = conn->source_ip;
	ssp_blog(LOG_INFO, "target ip: %s", conn->source_ip);
	ssp_blog(LOG_INFO, "source bitrate: %d", conn->bitrate);
	if (strlen(conn->source_ip) == 0) {
		pthread_mutex_unlock(&conn->lck);
		return nullptr;
	}
	conn->client = new SSPClientIso(ip, conn->bitrate / 8);
	conn->client->setOnH264DataCallback(
		std::bind(ssp_video_data_enqueue, _1, conn));
	conn->client->setOnAudioDataCallback(
		std::bind(ssp_on_audio_data, _1, conn));
	conn->client->setOnMetaCallback(
		std::bind(ssp_on_meta_data, _1, _2, _3, conn));
	conn->client->setOnConnectionConnectedCallback(
		[]() { ssp_blog(LOG_INFO, "ssp connected."); });
	conn->client->setOnDisconnectedCallback(
		std::bind(ssp_on_disconnected, conn));
	conn->client->setOnExceptionCallback(
		std::bind(ssp_on_exception, _1, _2, conn));

	assert(conn->queue == nullptr);
	conn->queue = new VFrameQueue;
	conn->queue->setFrameCallback(std::bind(ssp_on_video_data, _1, conn));

	conn->queue->start();
	emit conn->client->Start();
	pthread_mutex_unlock(&conn->lck);
	ssp_blog(LOG_INFO, "SSP client started.");

	return nullptr;
}

static obs_source_frame *blank_video_frame()
{
	obs_source_frame *frame =
		obs_source_frame_create(VIDEO_FORMAT_NONE, 0, 0);
	frame->timestamp = os_gettime_ns();
	return frame;
}

const char *ssp_source_getname(void *data)
{
	UNUSED_PARAMETER(data);
	return obs_module_text("SSPPlugin.SSPSourceName");
}

static void update_ssp_data(obs_data_t *settings, CameraStatus *status)
{
	StreamInfo &streamInfo = status->current_streamInfo;
	if (status->model == nullptr || status->model.isEmpty()) {
		return;
	}
	ssp_blog(LOG_INFO,
		 "Got stream info for %s: %dx%d@%d fps, %s, bitrate: %d",
		 streamInfo.steamIndex_.toStdString().c_str(),
		 streamInfo.width_, streamInfo.height_, streamInfo.fps,
		 streamInfo.encoderType_.toStdString().c_str(),
		 streamInfo.bitrate_);

	obs_data_t *source_settings = settings;
	// Update encoder setting based on current stream
	if (!obs_data_has_user_value(source_settings, PROP_ENCODER)) {
		if (streamInfo.encoderType_.toLower() == "h265") {
			obs_data_set_string(source_settings, PROP_ENCODER,
					    "H265");
			ssp_blog(LOG_INFO, "Setting encoder from camera: H265");
		} else if (streamInfo.encoderType_.toLower() == "h264") {
			obs_data_set_string(source_settings, PROP_ENCODER,
					    "H264");
			ssp_blog(LOG_INFO, "Setting encoder from camera: H264");
		}
	}

	// Update resolution setting if available from camera
	if (!obs_data_has_user_value(source_settings, PROP_RESOLUTION)) {
		std::ostringstream resStr;
		resStr << streamInfo.width_ << "*" << streamInfo.height_;
		obs_data_set_string(source_settings, PROP_RESOLUTION,
				    resStr.str().c_str());
		ssp_blog(LOG_INFO, "Setting resolution from camera: %s",
			 resStr.str().c_str());
	}

	// Update framerate setting if available from camera
	if (!obs_data_has_user_value(source_settings, PROP_FRAME_RATE)) {
		std::string fps = std::to_string(streamInfo.fps);
		if (fps == "30") {
			fps = "29.97";
		}
		if (fps == "60") {
			fps = "59.94";
		}
		obs_data_set_string(source_settings, PROP_FRAME_RATE,
				    fps.c_str());
		ssp_blog(LOG_INFO, "Setting framerate from camera: %d",
			 fps.c_str());
	}

	// Update bitrate from camera (convert from bytes to Mbps)
	if (streamInfo.bitrate_ > 0 &&
	    !obs_data_has_user_value(source_settings, PROP_BITRATE)) {
		int bitrateInMbps =
			streamInfo.bitrate_ /
			1000; // Ensure we have a reasonable value between 5-300
		if (bitrateInMbps >= 3 && bitrateInMbps <= 300) {
			obs_data_set_int(source_settings, PROP_BITRATE,
					 bitrateInMbps);
			ssp_blog(LOG_INFO,
				 "Setting bitrate from camera: %d Mbps",
				 bitrateInMbps);
		}
	}
}
bool source_ip_modified(void *data, obs_properties_t *props,
			obs_property_t *property, obs_data_t *settings)
{
	auto s = (struct ssp_source *)data;
	const char *source_ip = obs_data_get_string(settings, PROP_SOURCE_IP);
	s->ip_checked = false;
	if (strcmp(source_ip, PROP_CUSTOM_VALUE) == 0) {
		obs_property_t *custom_ip =
			obs_properties_get(props, PROP_CUSTOM_SOURCE_IP);
		obs_property_t *check_ip =
			obs_properties_get(props, PROP_CHECK_IP);
		obs_property_set_visible(property, false);
		obs_property_set_visible(custom_ip, true);
		obs_property_set_visible(check_ip, true);
		return true;
	}

	// Create CameraStatus if not already present
	if (source_ip == nullptr || strlen(source_ip) == 0) {
		return false;
	}
	ssp_blog(LOG_INFO, "source_ip_modified now %s", source_ip);

	if (s->cameraStatus != nullptr &&
	    s->cameraStatus->getIp() == source_ip) {
		return false;
	}
	ssp_stop(s);
	//if (!s->cameraStatus) {
	s->cameraStatus =
		CameraStatusManager::instance()->getOrCreate(source_ip);
	//}
	if (s->cameraStatus != nullptr) {
		update_ssp_data(settings, s->cameraStatus);
		obs_source_update(s->source, settings);
	} else {
		ssp_blog(LOG_INFO, "cannot create camera status for %s",
			 source_ip);
	}

	//s->cameraStatus->setIp(source_ip);
	add_active_ip(source_ip);

	s->cameraStatus->refreshAll([=, ip = source_ip](bool ok) {
		if (ok && is_ip_active(ip)) {
			s->ip_checked = true;
			update_ssp_data(settings, s->cameraStatus);
			//obs_source_update(s->source, settings);
			//obs_source_update_properties(s->source);
		}
	});
	return false;
}

static bool custom_ip_modify_callback(void *data, obs_properties_t *props,
				      obs_property_t *property,
				      obs_data_t *settings)
{
	auto s = (struct ssp_source *)data;
	if (s->ip_checked || !s->do_check) {
		s->ip_checked = false;
		ssp_blog(LOG_INFO, "ip modified, no need to check.%s ",
			 s->source_ip);
		return false;
	}
	s->do_check = false;

	auto ip = obs_data_get_string(settings, PROP_CUSTOM_SOURCE_IP);
	if (strcmp(ip, "") == 0) {
		return false;
	}
	ssp_blog(LOG_INFO, "ip modified, need to check. %s", ip);
	ssp_stop(s);
	// Create CameraStatus if not already present
	//if (!s->cameraStatus) {
	s->cameraStatus = CameraStatusManager::instance()->getOrCreate(ip);
	//} /* else {
	//	s->cameraStatus->setIp(ip);
	//}*/
	add_active_ip(ip);
	s->cameraStatus->refreshAll(
		[=, ip = std::string(s->source_ip)](bool ok) {
			if (ok && is_ip_active(ip)) {
				s->ip_checked = true;
				update_ssp_data(settings, s->cameraStatus);
				//obs_source_update(s->source, settings);
				//obs_source_update_properties(s->source);
			}
		});

	ssp_blog(LOG_INFO, "ip check queued.");
	return false;
}

static bool resolution_modify_callback(void *data, obs_properties_t *props,
				       obs_property_t *property,
				       obs_data_t *settings)
{
	auto s = (struct ssp_source *)data;
	auto framerates = obs_properties_get(props, PROP_FRAME_RATE);
	//obs_properties_set_flags();
	obs_property_list_clear(framerates);

	auto resolution = obs_data_get_string(settings, PROP_RESOLUTION);

	obs_property_list_add_string(framerates, "25 fps", "25");
	obs_property_list_add_string(framerates, "30 fps", "29.97");

	if (!s->cameraStatus || s->cameraStatus->model.isEmpty()) {
		return false;
	} else {
		update_ssp_data(settings, s->cameraStatus);
		//obs_source_update_properties(s->source);
	}

	ssp_blog(LOG_INFO, "Camera model: %s",
		 s->cameraStatus->model.toStdString().c_str());
	if (strcmp(resolution, "1920*1080") != 0 ||
	    (s->cameraStatus != nullptr &&
	     !s->cameraStatus->model.contains(E2C_MODEL_CODE,
					      Qt::CaseInsensitive))) {
		obs_property_list_add_string(framerates, "50 fps", "50");
		obs_property_list_add_string(framerates, "60 fps", "59.94");
	}
	return true;
}

static bool check_ip_callback(obs_properties_t *props, obs_property_t *property,
			      void *data)
{
	auto s = (struct ssp_source *)data;
	s->do_check = true;
	obs_source_update_properties(s->source);
	return false;
}

obs_properties_t *ssp_source_getproperties(void *data)
{
	char nametext[256];
	auto s = (struct ssp_source *)data;

	obs_properties_t *props = obs_properties_create();
	obs_properties_set_flags(props, OBS_PROPERTIES_DEFER_UPDATE);

	obs_property_t *source_ip = obs_properties_add_list(
		props, PROP_SOURCE_IP,
		obs_module_text("SSPPlugin.SourceProps.SourceIp"),
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);

	snprintf(nametext, 256, "%s (%s)",
		 obs_module_text("SSPPlugin.IP.Fixed"), SSP_IP_DIRECT);
	obs_property_list_add_string(source_ip, nametext, SSP_IP_DIRECT);

	snprintf(nametext, 256, "%s (%s)", obs_module_text("SSPPlugin.IP.Wifi"),
		 SSP_IP_WIFI);
	obs_property_list_add_string(source_ip, nametext, SSP_IP_WIFI);

	snprintf(nametext, 256, "%s (%s)", obs_module_text("SSPPlugin.IP.USB"),
		 SSP_IP_USB);
	obs_property_list_add_string(source_ip, nametext, SSP_IP_USB);

	int count = 0;

	SspMDnsIterator iter;
	while (iter.hasNext()) {
		ssp_device_item *item = iter.next();
		if (item == nullptr) {
			continue;
		}
		if (active_ips.find(item->ip_address) != active_ips.end()) {
			if (s->source_ip != nullptr &&
			    item->ip_address != s->source_ip) {
				continue;
			}
			if (s->source_ip == nullptr) {
				continue;
			}
		}
		snprintf(nametext, 256, "%s (%s)", item->device_name.c_str(),
			 item->ip_address.c_str());
		obs_property_list_add_string(source_ip, nametext,
					     item->ip_address.c_str());
		++count;
	}

	if (count == 0) {

		obs_property_list_add_string(
			source_ip,
			obs_module_text("SSPPlugin.SourceProps.NotFound"), "");
	}
	obs_property_list_add_string(
		source_ip, obs_module_text("SSPPlugin.SourceProps.Custom"),
		PROP_CUSTOM_VALUE);

	obs_property_t *custom_source_ip = obs_properties_add_text(
		props, PROP_CUSTOM_SOURCE_IP,
		obs_module_text("SSPPlugin.SourceProps.SourceIp"),
		OBS_TEXT_DEFAULT);

	obs_property_t *no_check = obs_properties_add_bool(
		props, PROP_NO_CHECK,
		obs_module_text("SSPPlugin.SourceProps.DontCheck"));

	obs_property_t *check_button = obs_properties_add_button2(
		props, PROP_CHECK_IP,
		obs_module_text("SSPPlugin.SourceProps.CheckIp"),
		check_ip_callback, data);

	obs_property_set_visible(custom_source_ip, false);
	obs_property_set_visible(check_button, false);

	obs_property_set_modified_callback2(source_ip, source_ip_modified,
					    data);
	obs_property_set_modified_callback2(custom_source_ip,
					    custom_ip_modify_callback, data);

	obs_property_t *sync_modes = obs_properties_add_list(
		props, PROP_SYNC, obs_module_text("SSPPlugin.SourceProps.Sync"),
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);

	obs_property_list_add_int(
		sync_modes, obs_module_text("SSPPlugin.SyncMode.Internal"),
		PROP_SYNC_INTERNAL);
	obs_property_list_add_int(
		sync_modes, obs_module_text("SSPPlugin.SyncMode.SSPTimestamp"),
		PROP_SYNC_SSP_TIMESTAMP);

	obs_properties_add_bool(
		props, PROP_HW_ACCEL,
		obs_module_text("SSPPlugin.SourceProps.HWAccel"));

	obs_property_t *latency_modes = obs_properties_add_list(
		props, PROP_LATENCY,
		obs_module_text("SSPPlugin.SourceProps.Latency"),
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);

	obs_property_list_add_int(
		latency_modes,
		obs_module_text("SSPPlugin.SourceProps.Latency.Normal"),
		PROP_LATENCY_NORMAL);
	obs_property_list_add_int(
		latency_modes,
		obs_module_text("SSPPlugin.SourceProps.Latency.Low"),
		PROP_LATENCY_LOW);

	obs_property_t *encoders = obs_properties_add_list(
		props, PROP_ENCODER,
		obs_module_text("SSPPlugin.SourceProps.Encoder"),
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(encoders, "H264", "H264");
	obs_property_list_add_string(encoders, "H265", "H265");

	obs_properties_add_bool(
		props, PROP_EXP_WAIT_I,
		obs_module_text("SSPPlugin.SourceProps.WaitIFrame"));

	obs_property_t *resolutions = obs_properties_add_list(
		props, PROP_RESOLUTION,
		obs_module_text("SSPPlugin.SourceProps.Resolution"),
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(resolutions, "4K-UHD", "3840*2160");
	obs_property_list_add_string(resolutions, "4K-DCI", "4096*2160");
	obs_property_list_add_string(resolutions, "1080p", "1920*1080");

	obs_properties_add_bool(
		props, PROP_LOW_NOISE,
		obs_module_text("SSPPlugin.SourceProps.LowNoise"));

	obs_property_set_modified_callback2(resolutions,
					    resolution_modify_callback, data);

	obs_property_t *framerate = obs_properties_add_list(
		props, PROP_FRAME_RATE,
		obs_module_text("SSPPlugin.SourceProps.FrameRate"),
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);

	obs_properties_add_int(props, PROP_BITRATE,
			       obs_module_text("SSPPlugin.SourceProps.Bitrate"),
			       5, 300, 5);

	obs_property_t *tally = obs_properties_add_bool(
		props, PROP_LED_TALLY,
		obs_module_text("SSPPlugin.SourceProps.LedAsTally"));

	// Hide certain properties if needed
	if (s->cameraStatus &&
	    s->cameraStatus->model.contains(IPMANS_MODEL_CODE,
					    Qt::CaseInsensitive)) {
		obs_property_set_visible(resolutions, false);
		obs_property_set_visible(encoders, false);
		obs_property_set_visible(framerate, false);
		obs_property_set_visible(tally, false);
	}
	obs_data_t *settings = obs_source_get_settings(s->source);
	if (s->source_ip != nullptr) {
		obs_data_set_string(settings, PROP_SOURCE_IP, s->source_ip);
		if (s->cameraStatus == nullptr) {
			s->cameraStatus =
				CameraStatusManager::instance()->getOrCreate(
					s->source_ip);
		}
		if (s->cameraStatus != nullptr &&
		    s->cameraStatus->model != nullptr) {
			//obs_get_source_data

			update_ssp_data(settings, s->cameraStatus);
			//obs_source_update(s->source, settings);

			ssp_blog(LOG_INFO,
				 "%s update for the settings from camerastatus",
				 s->source_ip);
		}
	}
	obs_data_release(settings);
	return props;
}

void ssp_source_getdefaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, PROP_SYNC, PROP_SYNC_SSP_TIMESTAMP);
	obs_data_set_default_int(settings, PROP_LATENCY, PROP_LATENCY_LOW);
	obs_data_set_default_string(settings, PROP_SOURCE_IP, "");
	obs_data_set_default_string(settings, PROP_CUSTOM_SOURCE_IP, "");
	obs_data_set_default_int(settings, PROP_BITRATE, 20);
	obs_data_set_default_bool(settings, PROP_HW_ACCEL, false);
	obs_data_set_default_bool(settings, PROP_EXP_WAIT_I, true);
	obs_data_set_default_bool(settings, PROP_LED_TALLY, false);
	obs_data_set_default_bool(settings, PROP_LOW_NOISE, false);
	obs_data_set_default_string(settings, PROP_ENCODER, "H264");
	obs_data_set_default_string(settings, PROP_FRAME_RATE, "29.97");
}

// Add this helper function to compare settings with stored data
static bool settings_changed(obs_data_t *new_settings, ssp_source *s)
{
	// Check IP changes
	const char *new_ip = obs_data_get_string(new_settings, PROP_SOURCE_IP);
	if (strcmp(new_ip, PROP_CUSTOM_VALUE) == 0) {
		new_ip = obs_data_get_string(new_settings,
					     PROP_CUSTOM_SOURCE_IP);
	}
	if (s->source_ip && strcmp(s->source_ip, new_ip) != 0) {
		ssp_blog(LOG_INFO, "IP changed from %s to %s", s->source_ip,
			 new_ip);
		return true;
	}

	// Check other critical settings that require restart
	bool new_hwaccel = obs_data_get_bool(new_settings, PROP_HW_ACCEL);
	if (s->hwaccel != new_hwaccel) {
		ssp_blog(LOG_INFO,
			 "HW acceleration setting changed from %d to %d",
			 s->hwaccel, new_hwaccel);
		return true;
	}

	int new_sync_mode = (int)obs_data_get_int(new_settings, PROP_SYNC);
	if (s->sync_mode != new_sync_mode) {
		ssp_blog(LOG_INFO, "Sync mode changed from %d to %d",
			 s->sync_mode, new_sync_mode);
		return true;
	}

	int new_bitrate =
		obs_data_get_int(new_settings, PROP_BITRATE) * 1000 * 1000;
	if (s->bitrate != new_bitrate) {
		ssp_blog(LOG_INFO, "Bitrate changed from %d to %d", s->bitrate,
			 new_bitrate);
		return true;
	}

	bool new_wait_i = obs_data_get_bool(new_settings, PROP_EXP_WAIT_I);
	if (s->wait_i_frame != new_wait_i) {
		ssp_blog(LOG_INFO, "Wait I-frame setting changed from %d to %d",
			 s->wait_i_frame, new_wait_i);
		return true;
	}

	// For encoder and resolution, we need to check if they would result in a different stream
	const char *new_encoder =
		obs_data_get_string(new_settings, PROP_ENCODER);
	const char *new_resolution =
		obs_data_get_string(new_settings, PROP_RESOLUTION);
	const char *new_framerate =
		obs_data_get_string(new_settings, PROP_FRAME_RATE);
	bool new_low_noise = obs_data_get_bool(new_settings, PROP_LOW_NOISE);

	// If we have a camera status, check if the stream settings would change
	if (s->cameraStatus) {
		StreamInfo &current = s->cameraStatus->current_streamInfo;
		int new_stream_index = (strcmp(new_encoder, "H265") == 0) ? 0
									  : 1;

		// Parse resolution string (e.g. "1920*1080")
		int new_width = 0, new_height = 0;
		if (sscanf(new_resolution, "%d*%d", &new_width, &new_height) ==
		    2) {

			int ifps = atof(new_framerate) + 0.1;
			int streamIndex = current.steamIndex_ == "stream1" ? 1
									   : 0;
			if (current.width_ != new_width ||
			    current.height_ != new_height ||
			    streamIndex != new_stream_index ||
			    current.fps != ifps) {
				ssp_blog(
					LOG_INFO,
					"Stream settings changed: %dx%d fps:%d @%s %s -> %dx%d fps:%d @%s %s %s",
					current.width_, current.height_,
					current.fps,
					current.steamIndex_.toStdString()
						.c_str(),
					current.encoderType_.toStdString()
						.c_str(),
					new_width, new_height, ifps,
					new_stream_index == 0 ? "stream0"
							      : "stream1",
					new_encoder,
					new_low_noise ? "low noise" : "normal");
				return true;
			}
		}
	}
	return false;
}

void ssp_source_update(void *data, obs_data_t *settings)
{
	auto s = (struct ssp_source *)data;

	// Compare new settings with our stored data
	bool needs_restart = settings_changed(settings, s);

	// If no critical settings changed, we can skip the restart
	if (!needs_restart && s->conn != nullptr) {
		ssp_blog(LOG_INFO,
			 "No critical settings changed, skipping restart");
		return;
	}

	ssp_blog(LOG_INFO, "Critical settings changed, stop %s", s->source_ip);
	ssp_stop(s);

	s->hwaccel = obs_data_get_bool(settings, PROP_HW_ACCEL);
	s->sync_mode = (int)obs_data_get_int(settings, PROP_SYNC);
	const char *source_ip = obs_data_get_string(settings, PROP_SOURCE_IP);
	if (strcmp(source_ip, PROP_CUSTOM_VALUE) == 0) {
		source_ip =
			obs_data_get_string(settings, PROP_CUSTOM_SOURCE_IP);
	}
	if (strlen(source_ip) == 0) {
		return;
	}
	ssp_blog(LOG_INFO, "ip from %s to %s", s->source_ip, source_ip);
	// Update source_ip and active IP tracking
	std::string oldIp = s->source_ip == nullptr ? "" : s->source_ip;
	if (s->source_ip && strcmp(s->source_ip, source_ip)) {
		remove_active_ip(s->source_ip);
		free((void *)s->source_ip);
		s->source_ip = strdup(source_ip);
	} else if (s->source_ip == nullptr) {
		s->source_ip = strdup(source_ip);
	}

	add_active_ip(s->source_ip);
	const char *sourceName = obs_source_get_name(s->source);
	if (sourceName && source_ip && strlen(source_ip) > 0) {
		if (oldIp != "") {
			SspToolbarManager::instance()->removeSourceAction(
				sourceName, oldIp.c_str());
		}
		SspToolbarManager::instance()->addSourceAction(sourceName,
							       source_ip);
	}
	// Get or create CameraStatus from the manager (without reference counting)
	// This will fetch an existing one or create a new one that will persist
	s->cameraStatus =
		CameraStatusManager::instance()->getOrCreate(source_ip);

	// Only proceed if we have a valid CameraStatus
	if (!s->cameraStatus) {
		ssp_blog(LOG_WARNING,
			 "No CameraStatus available, can't proceed");
		return;
	}

	// Set the IP of our camera from the configuration (used to build the url)
	//s->cameraStatus->setIp(s->source_ip);

	const bool is_unbuffered =
		(obs_data_get_int(settings, PROP_LATENCY) == PROP_LATENCY_LOW);
	obs_source_set_async_unbuffered(s->source, is_unbuffered);

	s->wait_i_frame = obs_data_get_bool(settings, PROP_EXP_WAIT_I);

	s->tally = obs_data_get_bool(settings, PROP_LED_TALLY);

	auto encoder = obs_data_get_string(settings, PROP_ENCODER);
	auto resolution = obs_data_get_string(settings, PROP_RESOLUTION);

	auto low_noise = obs_data_get_bool(settings, PROP_LOW_NOISE);
	auto framerate = obs_data_get_string(settings, PROP_FRAME_RATE);
	auto bitrate = obs_data_get_int(settings, PROP_BITRATE);
	auto nocheck = obs_data_get_bool(settings, PROP_NO_CHECK);

	int stream_index;
	if (strcmp(encoder, "H265") == 0) {
		stream_index = 0;
	} else {
		stream_index = 1;
	}

	bitrate *= 1000 * 1000;

	s->bitrate = bitrate;

	ssp_blog(LOG_INFO, "Calling setStream on ssp source %s", s->source_ip);
	s->cameraStatus->setStream(
		stream_index, resolution, low_noise, framerate, bitrate,
		[s, nocheck, ip = std::string(s->source_ip)](bool ok,
							     QString reason) {
			// Check if this IP is still active
			if (!is_ip_active(ip)) {
				ssp_blog(
					LOG_INFO,
					"Source for IP %s was destroyed before stream setup completed",
					ip.c_str());
				return;
			}

			if (!ok && !nocheck) {
				blog(LOG_INFO, "%s",
				     QString("setStream failed, not starting ssp: %1")
					     .arg(reason)
					     .toStdString()
					     .c_str());
			} else {
				ssp_blog(LOG_INFO,
					 "Set stream succeeded, starting ssp");
				// Double check IP is still active before starting
				if (is_ip_active(ip)) {
					if (!s->conn) {
						ssp_start(s);
					} else {
						ssp_blog(
							LOG_INFO,
							"Source for IP %s already started!!",
							ip.c_str());
					}

				} else {
					ssp_blog(
						LOG_INFO,
						"Source for IP %s was destroyed before stream could start",
						ip.c_str());
				}
			}
		});

	s->cameraStatus->getCurrentStream([](bool ok) {
		if (!ok) {
			ssp_blog(LOG_WARNING,
				 "Failed to get current stream info");
		}
	});
}

void ssp_source_shown(void *data)
{
	auto s = (struct ssp_source *)data;
	if (s->tally && s->cameraStatus) {
		s->cameraStatus->setLed(true);
	}
	ssp_blog(LOG_INFO, "ssp source shown.");
}

void ssp_source_hidden(void *data)
{
	auto s = (struct ssp_source *)data;
	if (s->tally && s->cameraStatus) {
		s->cameraStatus->setLed(false);
	}
	ssp_blog(LOG_INFO, "ssp source hidden.");
}

void ssp_source_activated(void *data)
{
	ssp_blog(LOG_INFO, "ssp source activated.");
}

void ssp_source_deactivated(void *data)
{
	ssp_blog(LOG_INFO, "ssp source deactivated.");
}

void *ssp_source_create(obs_data_t *settings, obs_source_t *source)
{
	ssp_blog(LOG_INFO, "ssp_source_create");

	auto s = (struct ssp_source *)bzalloc(sizeof(struct ssp_source));
	s->source = source;
	s->do_check = false;
	s->no_check = true;
	s->ip_checked = false;
	s->cameraStatus = nullptr;
	s->sync_mode = PROP_SYNC_SSP_TIMESTAMP;
	s->wait_i_frame = true;
	s->hwaccel = false;

	// Get source IP from settings
	const char *sourceIp = obs_data_get_string(settings, PROP_SOURCE_IP);
	if (strcmp(sourceIp, PROP_CUSTOM_VALUE) == 0) {
		sourceIp = obs_data_get_string(settings, PROP_CUSTOM_SOURCE_IP);
	}

	// Add IP to active set if we have a valid IP
	if (sourceIp && strlen(sourceIp) > 0) {
		add_active_ip(sourceIp);
	}
	s->source_ip = nullptr;
	// Get or create the CameraStatus from manager only if we have a valid IP
	if (sourceIp && strlen(sourceIp) > 0) {
		s->cameraStatus =
			CameraStatusManager::instance()->getOrCreate(sourceIp);

		// If we got a valid camera status with stream info, update settings
		if (s->cameraStatus->model != nullptr &&
		    !s->cameraStatus->model.isEmpty()) {
			update_ssp_data(settings, s->cameraStatus);
			obs_source_update(s->source, settings);
		}
		s->source_ip = strdup(sourceIp);
	}

	//s->source_ip = nullptr;

	ssp_source_update(s, settings);

	// Add toolbar action for the new source
	const char *sourceName = obs_source_get_name(source);
	if (sourceName && sourceIp && strlen(sourceIp) > 0) {
		SspToolbarManager::instance()->addSourceAction(sourceName,
							       sourceIp);
	}

	return s;
}

void ssp_source_destroy(void *data)
{
	if (!data) {
		ssp_blog(LOG_INFO, "destroying source: null data pointer");
		return;
	}

	auto s = (struct ssp_source *)data;
	ssp_blog(LOG_INFO, "destroying source...");
	// Remove toolbar action for the source
	const char *sourceName = obs_source_get_name(s->source);
	if (sourceName && s->source_ip) {
		// Make copies of the strings for thread safety
		std::string nameStr(sourceName);
		std::string ipStr(s->source_ip);

		// Execute in the main thread and wait for completion
		QMetaObject::invokeMethod(
			QApplication::instance(),
			[nameStr, ipStr]() {
				SspToolbarManager::instance()
					->removeSourceAction(
						QString::fromStdString(nameStr),
						QString::fromStdString(ipStr));
			},
			Qt::QueuedConnection);
	}

	// Remove IP from active set if we have a valid IP
	if (s->source_ip) {
		remove_active_ip(s->source_ip);
	}

	// First, ensure we have a valid source
	if (!s->source) {
		return;
	}
	ssp_stop(s);

	// Properly release the CameraStatus reference
	if (s->cameraStatus && s->source_ip) {
		std::string ipStr(s->source_ip);
		CameraStatusManager::instance()->release(ipStr);
	}

	// Cleanup the rest of the source
	if (s->source_ip) {
		free((void *)s->source_ip);
		s->source_ip = nullptr;
	}

	bfree(s);
	ssp_blog(LOG_INFO, "source destroyed.");
}
void ssp_source_load(void *data, obs_data_t *settings)
{
	ssp_blog(LOG_INFO, "source load.");
}
struct obs_source_info create_ssp_source_info()
{
	struct obs_source_info ssp_source_info = {};
	ssp_source_info.id = "ssp_source";
	ssp_source_info.type = OBS_SOURCE_TYPE_INPUT;
	ssp_source_info.output_flags = OBS_SOURCE_ASYNC_VIDEO |
				       OBS_SOURCE_AUDIO |
				       OBS_SOURCE_DO_NOT_DUPLICATE;
	ssp_source_info.get_name = ssp_source_getname;
	ssp_source_info.get_properties = ssp_source_getproperties;
	ssp_source_info.get_defaults = ssp_source_getdefaults;
	ssp_source_info.update = ssp_source_update;
	ssp_source_info.show = ssp_source_shown;
	ssp_source_info.hide = ssp_source_hidden;
	ssp_source_info.activate = ssp_source_activated;
	ssp_source_info.deactivate = ssp_source_deactivated;
	ssp_source_info.create = ssp_source_create;
	ssp_source_info.destroy = ssp_source_destroy;
	ssp_source_info.load = ssp_source_load;
	return ssp_source_info;
}
