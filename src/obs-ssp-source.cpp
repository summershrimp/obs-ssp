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

#include "obs-ssp.h"
#include "imf/ISspClient.h"
#include "imf/threadloop.h"

#include "ssp-controller.h"
#include "ssp-client.h"
#include "VFrameQueue.h"

extern "C" {
#include "ffmpeg-decode.h"
}

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
#define PROP_BITRATE "ssp_bitrate"
#define PROP_STREAM_INDEX "ssp_stream_index"
#define PROP_ENCODER "ssp_encoding"

#define SSP_IP_DIRECT "10.98.32.1"
#define SSP_IP_WIFI "10.98.33.1"
#define SSP_IP_USB "172.18.18.1"


using namespace std::placeholders;

struct ssp_source;

struct ssp_connection {
    SSPClient *client;
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
    bool running;
    int i_frame_shown;

    ssp_source *source;
};

struct ssp_source
{
	obs_source_t* source;
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
    ssp_connection *conn;
};

static void ssp_conn_start(ssp_connection *s);
static void ssp_conn_stop(ssp_connection *s);
static void ssp_stop(ssp_source *s, bool detach);
static void ssp_start(ssp_source *s);

static void ssp_video_data_enqueue(struct imf::SspH264Data *video, ssp_connection *s) {
    if (!s->running) {
        return;
    }
    if(!s->queue){
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
        assert (s->vformat == AV_CODEC_ID_H264 || s->vformat == AV_CODEC_ID_HEVC);
        if (ffmpeg_decode_init(&s->vdecoder, s->vformat, s->source->hwaccel) < 0) {
            blog(LOG_WARNING, "Could not initialize video decoder");
            return;
        }
    }
    if(s->source->wait_i_frame && !s->i_frame_shown) {
        if(video->type == 5) {
            s->i_frame_shown = true;
        } else {
            return;
        }
    }

    int64_t ts = video->pts;
    bool got_output;
    bool success = ffmpeg_decode_video(&s->vdecoder, video->data, video->len, &ts,
                                       VIDEO_RANGE_PARTIAL, &s->frame, &got_output);
    if (!success) {
        blog(LOG_WARNING, "Error decoding video");
        return;
    }

    if (got_output) {
        if(s->source->sync_mode == PROP_SYNC_INTERNAL){
            s->frame.timestamp = os_gettime_ns();
        } else {
            s->frame.timestamp = (uint64_t) video->pts * 1000;
        }
//        if (flip)
//            frame.flip = !frame.flip;
        obs_source_output_video2(s->source->source, &s->frame);
    }
}

static void ssp_on_audio_data(struct imf::SspAudioData *audio, ssp_connection *s)
{
    if (!s->running) {
        return;
    }
    if (!ffmpeg_decode_valid(&s->adecoder)) {
        if (ffmpeg_decode_init(&s->adecoder, s->aformat, false) < 0) {
            blog(LOG_WARNING, "Could not initialize audio decoder");
            return;
        }
    }
    uint8_t* data = audio->data;
    size_t size = audio->len;
    bool got_output = false;
    do {
        bool success = ffmpeg_decode_audio(&s->adecoder, data, size,
                                           &s->audio, &got_output);
        if (!success) {
            blog(LOG_WARNING, "Error decoding audio");
            return;
        }
        if (got_output) {
            if(s->source->sync_mode == PROP_SYNC_INTERNAL) {
                s->audio.timestamp = os_gettime_ns();
                s->audio.timestamp +=
                        ((uint64_t)s->audio.samples_per_sec * 1000000000ULL /
                         (uint64_t)s->sample_size);
            } else {
                s->audio.timestamp = (uint64_t) audio->pts * 1000;
            }
            obs_source_output_audio(s->source->source, &s->audio);
        } else {
            break;
        }

        size = 0;
        data = nullptr;
    } while (got_output);
}

static void ssp_on_meta_data(struct imf::SspVideoMeta *v,
                             struct imf::SspAudioMeta *a,
                             struct imf::SspMeta * m,
                             ssp_connection *s) {
    blog(LOG_INFO, "ssp v meta: encoder: %u, gop:%u, height:%u, timescale:%u, unit:%u, width:%u", v->encoder, v->gop, v->height, v->timescale, v->unit, v->width);
    blog(LOG_INFO, "ssp a meta: uinit: %u, timescale:%u, encoder:%u, bitrate:%u, channel:%u, sample_rate:%u, sample_size:%u", a->unit, a->timescale, a->encoder, a->bitrate, a->channel, a->sample_rate, a->sample_size);
    blog(LOG_INFO, "ssp i meta: pts_is_wall_clock: %u, tc_drop_frame:%u, timecode:%u,", m->pts_is_wall_clock, m->tc_drop_frame, m->timecode);
    s->vformat = v->encoder == VIDEO_ENCODER_H264 ? AV_CODEC_ID_H264 : AV_CODEC_ID_H265;
    s->frame.width = v->width;
    s->frame.height = v->height;
    s->sample_size = a->sample_size;
    s->audio.samples_per_sec = a->sample_rate;
    s->aformat = a->encoder == AUDIO_ENCODER_AAC ? AV_CODEC_ID_AAC : AV_CODEC_ID_NONE;

}

void *thread_ssp_reconnect(void *data) {
    auto s = (ssp_connection *) data;
    auto source = s->source;
    ssp_stop(source, true);
    ssp_start(source);
    return nullptr;
}

static void ssp_on_disconnected(ssp_connection *s) {
    blog(LOG_INFO, "ssp device disconnected.");
    pthread_t thread;
    if(s->running) {
        blog(LOG_INFO, "still running, reconnect...");
        pthread_create(&thread, nullptr, thread_ssp_reconnect, (void *)s);
        pthread_detach(thread);
    }
}

static void ssp_on_exception(int code, const char* description, ssp_connection *s) {
    blog(LOG_ERROR, "ssp exception %d: %s", code, description);
    //s->running = false;
}

static void ssp_start(ssp_source *s){
    auto conn = (ssp_connection *)bzalloc(sizeof(ssp_connection));
    conn->source = s;
    s->conn = conn;
    ssp_conn_start(conn);
}

static void ssp_conn_stop(ssp_connection *conn) {
    blog(LOG_INFO, "Stopping ssp client...");
    conn->running = false;
    auto client = conn->client;
    auto queue = conn->queue;

    if(client){
        emit client->Stop();
        delete client;
    }
    if(queue){
        queue->stop();
        delete queue;
    }

    blog(LOG_INFO, "SSP client stopped.");

    if(ffmpeg_decode_valid(&conn->adecoder)) {
        ffmpeg_decode_free(&conn->adecoder);
    }
    if(ffmpeg_decode_valid(&conn->vdecoder)) {
        ffmpeg_decode_free(&conn->vdecoder);
    }

    blog(LOG_INFO, "SSP released.");
}

static void* thread_ssp_conn_stop(void *data) {
    auto *conn = (ssp_connection *)data;
    if(!data) {
        return nullptr;
    }
    ssp_conn_stop(conn);
    bfree(conn);
    return nullptr;
}

static void ssp_stop(ssp_source *s, bool detach = false){
    if(!s || ! s->conn) {
        return ;
    }
    pthread_t thread;
    pthread_create(&thread, nullptr, thread_ssp_conn_stop, (void *)s->conn);
    s->conn = nullptr;
    if(detach) {
        pthread_detach(thread);
    } else {
        pthread_join(thread, nullptr);
    }
}


static void ssp_conn_start(ssp_connection *s){
    blog(LOG_INFO, "Starting ssp client...");
    std::string ip = s->source->source_ip;
    blog(LOG_INFO, "target ip: %s", s->source->source_ip);
    if(strlen(s->source->source_ip) == 0) {
        return;
    }
    assert(s->client == nullptr);
    assert(s->source != nullptr);
    s->client = new SSPClient(ip, s->source->bitrate/8);
    s->client->setOnH264DataCallback(std::bind(ssp_video_data_enqueue, _1, s));
    s->client->setOnAudioDataCallback(std::bind(ssp_on_audio_data, _1, s));
    s->client->setOnMetaCallback(std::bind(ssp_on_meta_data, _1, _2, _3, s));
    s->client->setOnConnectionConnectedCallback([](){
        blog(LOG_INFO, "ssp connected.");
    });
    s->client->setOnDisconnectedCallback(std::bind(ssp_on_disconnected, s));
    s->client->setOnExceptionCallback(std::bind(ssp_on_exception, _1, _2, s));

    assert(s->queue == nullptr);
    s->queue = new VFrameQueue;
    s->queue->setFrameCallback(std::bind(ssp_on_video_data, _1, s));

    s->queue->start();
    emit s->client->Start();
    s->running = true;
    blog(LOG_INFO, "SSP client started.");
}

static obs_source_frame* blank_video_frame()
{
	obs_source_frame* frame = obs_source_frame_create(VIDEO_FORMAT_NONE, 0, 0);
	frame->timestamp = os_gettime_ns();
	return frame;
}

const char* ssp_source_getname(void* data)
{
	UNUSED_PARAMETER(data);
	return obs_module_text("SSPPlugin.SSPSourceName");
}

bool source_ip_modified(void* data, obs_properties_t *props,
                        obs_property_t *property,
                        obs_data_t *settings) {
    auto s = (struct ssp_source*)data;
    const char *source_ip = obs_data_get_string(settings, PROP_SOURCE_IP);
    s->ip_checked = false;
    if(strcmp(source_ip, PROP_CUSTOM_VALUE) == 0) {
        obs_property_t *custom_ip = obs_properties_get(props, PROP_CUSTOM_SOURCE_IP);
        obs_property_t *check_ip = obs_properties_get(props, PROP_CHECK_IP);
        obs_property_set_visible(property, false);
        obs_property_set_visible(custom_ip, true);
        obs_property_set_visible(check_ip, true);
        return true;
    }
    if(s->cameraStatus->getIp() == source_ip) {
        return false;
    }
    s->cameraStatus->setIp(source_ip);
    s->cameraStatus->refreshAll([=](bool ok){
        if(!ok) return;
        s->ip_checked = true;
        obs_source_update_properties(s->source);
    });
    return false;
}

static bool custom_ip_modify_callback(void* data, obs_properties_t *props,
      obs_property_t *property,
      obs_data_t *settings) {
    auto s = (struct ssp_source*)data;
    if(s->ip_checked || !s->do_check){
        s->ip_checked = false;
        blog(LOG_INFO, "ip modified, no need to check.");
        return false;
    }
    s->do_check = false;
    blog(LOG_INFO, "ip modified, need to check.");
    auto ip = obs_data_get_string(settings, PROP_CUSTOM_SOURCE_IP);
    if(strcmp(ip, "") == 0) {
        return false;
    }
    s->cameraStatus->setIp(ip);
    s->cameraStatus->refreshAll([=](bool ok){
        if(!ok) return;
        blog(LOG_INFO, "refresh ok");
        s->ip_checked = true;
        obs_source_update_properties(s->source);
    });

    blog(LOG_INFO, "ip check queued.");
    return false;
}

static bool resolution_modify_callback(void* data, obs_properties_t *props,
                                      obs_property_t *property,
                                      obs_data_t *settings) {

    auto s = (struct ssp_source*)data;
    auto framerates = obs_properties_get(props, PROP_FRAME_RATE);
    obs_property_list_clear(framerates);

    if(s->cameraStatus->model.isEmpty()) {
        return false;
    }

    auto resolution = obs_data_get_string(settings, PROP_RESOLUTION);
    obs_property_list_add_string(framerates, "25 fps", "25");
    obs_property_list_add_string(framerates, "30 fps", "29.97");
    blog(LOG_INFO, "Camera model: %s", s->cameraStatus->model.toStdString().c_str());
    if(strcmp(resolution, "1920*1080") != 0 || !s->cameraStatus->model.contains(E2C_MODEL_CODE, Qt::CaseInsensitive)) {
        obs_property_list_add_string(framerates, "50 fps", "50");
        obs_property_list_add_string(framerates, "60 fps", "59.94");
    }
    return true;
}


static bool check_ip_callback(obs_properties_t *props,
                              obs_property_t *property, void *data){
    auto s = (struct ssp_source*)data;
    s->do_check = true;
    obs_source_update_properties(s->source);
    return false;
}

static bool no_check_modified(void* data, obs_properties_t *props,
                              obs_property_t *property,
                              obs_data_t *settings){
    auto s = (struct ssp_source*)data;
    s->no_check = true;
    return true;
}

obs_properties_t* ssp_source_getproperties(void* data)
{
    char nametext[256];
	auto s = (struct ssp_source*)data;

	obs_properties_t* props = obs_properties_create();
	obs_properties_set_flags(props, OBS_PROPERTIES_DEFER_UPDATE);

	obs_property_t* source_ip = obs_properties_add_list(props, PROP_SOURCE_IP,
		obs_module_text("SSPPlugin.SourceProps.SourceIp"),
        OBS_COMBO_TYPE_LIST,
        OBS_COMBO_FORMAT_STRING);

    snprintf(nametext, 256, "%s (%s)", obs_module_text("SSPPlugin.IP.Fixed"), SSP_IP_DIRECT);
    obs_property_list_add_string(source_ip, nametext, SSP_IP_DIRECT);

    snprintf(nametext, 256, "%s (%s)", obs_module_text("SSPPlugin.IP.Wifi"), SSP_IP_WIFI);
    obs_property_list_add_string(source_ip, nametext, SSP_IP_WIFI);

    snprintf(nametext, 256, "%s (%s)", obs_module_text("SSPPlugin.IP.USB"), SSP_IP_USB);
    obs_property_list_add_string(source_ip, nametext, SSP_IP_USB);

    int count = 0;

	SspMDnsIterator iter;
	while(iter.hasNext()) {
        ssp_device_item *item = iter.next();
        if(item == nullptr){
            continue;
        }
        snprintf(nametext, 256, "%s (%s)", item->device_name.c_str(), item->ip_address.c_str());
        obs_property_list_add_string(source_ip, nametext, item->ip_address.c_str());
        ++count;
	}

	if(count == 0)
	    obs_property_list_add_string(source_ip, obs_module_text("SSPPlugin.SourceProps.NotFound"), "");
    obs_property_list_add_string(source_ip, obs_module_text("SSPPlugin.SourceProps.Custom"), PROP_CUSTOM_VALUE);


    obs_property_t* custom_source_ip = obs_properties_add_text(props, PROP_CUSTOM_SOURCE_IP,
        obs_module_text("SSPPlugin.SourceProps.SourceIp"),
        OBS_TEXT_DEFAULT);

    obs_property_t* no_check = obs_properties_add_bool(props, PROP_NO_CHECK,
            obs_module_text("SSPPlugin.SourceProps.DontCheck"));
    obs_property_set_modified_callback2(no_check, no_check_modified, data);

    obs_property_t* check_button = obs_properties_add_button2(props, PROP_CHECK_IP,
       obs_module_text("SSPPlugin.SourceProps.CheckIp"),
       check_ip_callback, data);

    obs_property_set_visible(custom_source_ip, false);
    obs_property_set_visible(check_button, false);

    obs_property_set_modified_callback2(source_ip, source_ip_modified, data);
    obs_property_set_modified_callback2(custom_source_ip, custom_ip_modify_callback, data);

	obs_property_t* sync_modes = obs_properties_add_list(props, PROP_SYNC,
		obs_module_text("SSPPlugin.SourceProps.Sync"),
		OBS_COMBO_TYPE_LIST,
		OBS_COMBO_FORMAT_INT);

	obs_property_list_add_int(sync_modes,
		obs_module_text("SSPPlugin.SyncMode.Internal"),
		PROP_SYNC_INTERNAL);
	obs_property_list_add_int(sync_modes,
		obs_module_text("SSPPlugin.SyncMode.SSPTimestamp"),
		PROP_SYNC_SSP_TIMESTAMP);


	obs_properties_add_bool(props, PROP_HW_ACCEL,
		obs_module_text("SSPPlugin.SourceProps.HWAccel"));

	obs_property_t* latency_modes = obs_properties_add_list(props, PROP_LATENCY,
		obs_module_text("SSPPlugin.SourceProps.Latency"),
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);

	obs_property_list_add_int(latency_modes,
		obs_module_text("SSPPlugin.SourceProps.Latency.Normal"),
		PROP_LATENCY_NORMAL);
	obs_property_list_add_int(latency_modes,
		obs_module_text("SSPPlugin.SourceProps.Latency.Low"),
		PROP_LATENCY_LOW);

    obs_property_t* encoders = obs_properties_add_list(props, PROP_ENCODER,
                                                       obs_module_text("SSPPlugin.SourceProps.Encoder"),
                                                       OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    obs_property_list_add_string(encoders, "H264", "H264");
    obs_property_list_add_string(encoders, "H265", "H265");

    obs_properties_add_bool(props, PROP_EXP_WAIT_I,
                            obs_module_text("SSPPlugin.SourceProps.WaitIFrame"));

    obs_property_t* resolutions = obs_properties_add_list(props, PROP_RESOLUTION,
                            obs_module_text("SSPPlugin.SourceProps.Resolution"),
                            OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    obs_property_list_add_string(resolutions, "4K-UHD", "3840*2160");
    obs_property_list_add_string(resolutions, "4K-DCI", "4096*2160");
    obs_property_list_add_string(resolutions, "1080p", "1920*1080");

    obs_property_set_modified_callback2(resolutions, resolution_modify_callback, data);

    obs_properties_add_list(props, PROP_FRAME_RATE,
                            obs_module_text("SSPPlugin.SourceProps.FrameRate"),
                            OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);

    obs_properties_add_int(props, PROP_BITRATE,
                            obs_module_text("SSPPlugin.SourceProps.Bitrate"),
                            10, 300, 5);

    obs_properties_add_bool(props, PROP_LED_TALLY,
                            obs_module_text("SSPPlugin.SourceProps.LedAsTally"));

	return props;
}

void ssp_source_getdefaults(obs_data_t* settings)
{
	obs_data_set_default_int(settings, PROP_SYNC, PROP_SYNC_SSP_TIMESTAMP);
	obs_data_set_default_int(settings, PROP_LATENCY, PROP_LATENCY_NORMAL);
	obs_data_set_default_string(settings, PROP_SOURCE_IP, "");
    obs_data_set_default_string(settings, PROP_CUSTOM_SOURCE_IP, "");
    obs_data_set_default_int(settings, PROP_BITRATE, 20);
    obs_data_set_default_bool(settings, PROP_HW_ACCEL, false);
    obs_data_set_default_bool(settings, PROP_EXP_WAIT_I, false);
    obs_data_set_default_bool(settings, PROP_LED_TALLY, false);
    obs_data_set_default_string(settings, PROP_ENCODER, "H264");
    obs_data_set_default_string(settings, PROP_FRAME_RATE, "29.97");

}

void ssp_source_update(void* data, obs_data_t* settings)
{
	auto s = (struct ssp_source*)data;

	ssp_stop(s, true);

	s->hwaccel = obs_data_get_bool(settings, PROP_HW_ACCEL);

	s->sync_mode = (int)obs_data_get_int(settings, PROP_SYNC);
    s->source_ip = obs_data_get_string(settings, PROP_SOURCE_IP);
    if(strcmp(s->source_ip, PROP_CUSTOM_VALUE) == 0) {
        s->source_ip = obs_data_get_string(settings, PROP_CUSTOM_SOURCE_IP);
    }
    if(strlen(s->source_ip) == 0){
        return;
    }
	const bool is_unbuffered =
		(obs_data_get_int(settings, PROP_LATENCY) == PROP_LATENCY_LOW);
	obs_source_set_async_unbuffered(s->source, is_unbuffered);

	s->wait_i_frame = obs_data_get_bool(settings, PROP_EXP_WAIT_I);

    s->tally = obs_data_get_bool(settings, PROP_LED_TALLY);

    auto encoder = obs_data_get_string(settings, PROP_ENCODER);
    auto resolution = obs_data_get_string(settings, PROP_RESOLUTION);
    auto framerate = obs_data_get_string(settings, PROP_FRAME_RATE);
    auto bitrate = obs_data_get_int(settings, PROP_BITRATE);

    int stream_index;
    if(strcmp(encoder, "H265") == 0) {
        stream_index = 0;
    } else {
        stream_index = 1;
    }

    bitrate *= 1024*1024;

    s->bitrate = bitrate;

    s->cameraStatus->setStream(stream_index, resolution, framerate, bitrate, [=](bool ok){
        if(!ok && !s->no_check)
            return;
       ssp_start(s);
    });
}

void ssp_source_shown(void* data)
{
    auto s = (struct ssp_source*)data;
    if(s->tally) {
        s->cameraStatus->setLed(true);
    }
    blog(LOG_INFO, "ssp source shown.");
}

void ssp_source_hidden(void* data)
{
    auto s = (struct ssp_source*)data;
    if(s->tally) {
        s->cameraStatus->setLed(false);
    }
    blog(LOG_INFO, "ssp source hidden.");
}

void ssp_source_activated(void* data)
{
    blog(LOG_INFO, "ssp source activated.");
}

void ssp_source_deactivated(void* data)
{
    blog(LOG_INFO, "ssp source deactivated.");
}

void* ssp_source_create(obs_data_t* settings, obs_source_t* source)
{
	auto s = (struct ssp_source*)bzalloc(sizeof(struct ssp_source));
	s->source = source;
	s->do_check = false;
	s->no_check = false;
	s->ip_checked = false;
	s->cameraStatus = new CameraStatus();
    ssp_source_update(s, settings);
	return s;
}

static void * thread_source_destory(void *data){
    blog(LOG_INFO, "destroying source...");
    auto s = (struct ssp_source*)data;

    ssp_stop(s);
    bfree(s);
    blog(LOG_INFO, "source destroyed.");
    return nullptr;
}

void ssp_source_destroy(void* data)
{
    auto s = (struct ssp_source*)data;
    pthread_t thread;
    pthread_create(&thread, nullptr, thread_source_destory, data);
    pthread_detach(thread);
    delete s->cameraStatus;
}

struct obs_source_info create_ssp_source_info()
{
	struct obs_source_info ssp_source_info = {};
    ssp_source_info.id				= "ssp_source";
    ssp_source_info.type			= OBS_SOURCE_TYPE_INPUT;
    ssp_source_info.output_flags	= OBS_SOURCE_ASYNC_VIDEO | OBS_SOURCE_AUDIO |
                                      OBS_SOURCE_DO_NOT_DUPLICATE;
    ssp_source_info.get_name		= ssp_source_getname;
    ssp_source_info.get_properties	= ssp_source_getproperties;
    ssp_source_info.get_defaults	= ssp_source_getdefaults;
    ssp_source_info.update			= ssp_source_update;
    ssp_source_info.show			= ssp_source_shown;
    ssp_source_info.hide			= ssp_source_hidden;
    ssp_source_info.activate		= ssp_source_activated;
    ssp_source_info.deactivate		= ssp_source_deactivated;
    ssp_source_info.create			= ssp_source_create;
    ssp_source_info.destroy			= ssp_source_destroy;

	return ssp_source_info;
}
