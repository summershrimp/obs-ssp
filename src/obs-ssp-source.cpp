/*
obs-ssp
 Copyright (C) 2016-2018 Yibai Zhang

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
#include <imf/ssp/sspclient.h>
#include <imf/net/loop.h>
#include <imf/net/threadloop.h>

#ifdef _WIN32
#include <Windows.h>
#endif

#include <functional>
#include <mutex>
#include <obs-module.h>

#include <util/platform.h>
#include <util/threading.h>
#include <chrono>
#include <thread>

#include "obs-ssp.h"
extern "C" {
#include "ffmpeg-decode.h"
}

#define PROP_SOURCE_IP "ssp_source_ip"
#define PROP_HW_ACCEL "ssp_recv_hw_accel"
#define PROP_SYNC "ssp_sync"
#define PROP_LATENCY "latency"
#define PROP_VIDEO_RANGE "video_range"

#define PROP_BW_HIGHEST 0
#define PROP_BW_LOWEST 1
#define PROP_BW_AUDIO_ONLY 2

#define PROP_SYNC_INTERNAL 0
#define PROP_SYNC_SSP_TIMESTAMP 1

#define PROP_LATENCY_NORMAL 0
#define PROP_LATENCY_LOW 1
using namespace std::placeholders;

struct ssp_source
{
	obs_source_t* source;
    imf::ThreadLoop *clientLooper;
    imf::SspClient *client;
    int sync_mode;
    int video_range;
    int hwaccel;
	bool running;
	const char *source_ip;

	ffmpeg_decode vdecoder;
	uint32_t width;
	uint32_t height;
	AVCodecID vformat;
    obs_source_frame2 frame;

    ffmpeg_decode adecoder;
    uint32_t sample_size;
    AVCodecID aformat;
    obs_source_audio audio;

};

static void ssp_on_video_data(struct imf::SspH264Data *video, ssp_source *s)
{
    if (!ffmpeg_decode_valid(&s->vdecoder)) {
        assert (s->vformat == AV_CODEC_ID_H264 || s->vformat == AV_CODEC_ID_HEVC);
        if (ffmpeg_decode_init(&s->vdecoder, s->vformat, false) < 0) {
            blog(LOG_WARNING, "Could not initialize video decoder");
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
        if(s->sync_mode == PROP_SYNC_INTERNAL){
            s->frame.timestamp = os_gettime_ns();
        } else {
            s->frame.timestamp = (uint64_t) ts * 1000;
        }
//        if (flip)
//            frame.flip = !frame.flip;
        obs_source_output_video2(s->source, &s->frame);
    }
}

static void ssp_on_audio_data(struct imf::SspAudioData *audio, ssp_source *s)
{
    if (!ffmpeg_decode_valid(&s->adecoder)) {
        if (ffmpeg_decode_init(&s->adecoder, s->aformat, false) < 0) {
            blog(LOG_WARNING, "Could not initialize audio decoder");
            return;
        }
    }
    uint8_t* data = audio->data;
    uint32_t size = audio->len;
    bool got_output = false;
    do {
        bool success = ffmpeg_decode_audio(&s->adecoder, data, size,
                                           &s->audio, &got_output);
        if (!success) {
            blog(LOG_WARNING, "Error decoding audio");
            return;
        }
        if (got_output) {
            if(s->sync_mode == PROP_SYNC_INTERNAL) {
                s->audio.timestamp = os_gettime_ns();
                s->audio.timestamp +=
                        ((uint64_t)s->audio.samples_per_sec * 1000000000ULL /
                         (uint64_t)s->sample_size);
            } else {
                s->audio.timestamp = (uint64_t) audio->pts * 1000;
            }
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
                             struct imf::SspMeta * m,
                             ssp_source *s)
{
    blog(LOG_INFO, "ssp v meta: encoder: %u, gop:%u, height:%u, timescale:%u, unit:%u, width:%u", v->encoder, v->gop, v->height, v->timescale, v->unit, v->width);
    blog(LOG_INFO, "ssp a meta: uinit: %u, timescale:%u, encoder:%u, bitrate:%u, channel:%u, sample_rate:%u, sample_size:%u", a->unit, a->timescale, a->encoder, a->bitrate, a->channel, a->sample_rate, a->sample_size);
    blog(LOG_INFO, "ssp i meta: pts_is_wall_clock: %u, tc_drop_frame:%u, timecode:%u,", m->pts_is_wall_clock, m->tc_drop_frame, m->timecode);
    s->vformat = v->encoder == VIDEO_ENCODER_H264 ? AV_CODEC_ID_H264 : AV_CODEC_ID_HEVC;
    s->frame.width = v->width;
    s->frame.height = v->height;
    s->sample_size = a->sample_size;
    s->audio.samples_per_sec = a->sample_rate;
    s->aformat = a->encoder == AUDIO_ENCODER_AAC ? AV_CODEC_ID_AAC : AV_CODEC_ID_NONE;

}

static void ssp_on_disconnected(ssp_source *s) {
    blog(LOG_INFO, "ssp device disconnected.");
    s->running = false;
}

static void ssp_on_exception(int code, const char* description, ssp_source *s) {
    blog(LOG_ERROR, "ssp exception %d: %s\n", code, description);
}

static void ssp_stop(ssp_source *s){
    if(!s) {
        return ;
    }
    blog(LOG_INFO, "Stopping ssp client...");
    if(s->client){
        s->client->stop();
    }
    if(s->clientLooper){
        s->clientLooper->stop();
    }
    blog(LOG_INFO, "SSP client stopped.");
    if(s->client){
        //delete s->client;
        s->client = nullptr;
    }
    if(s->clientLooper){
        //delete s->clientLooper;
        s->clientLooper = nullptr;
    }
    blog(LOG_INFO, "SSP released.");
}

static void ssp_setup_client(imf::Loop *loop, ssp_source *s)
{
    std::string ip = s->source_ip;
    blog(LOG_INFO, "target ip: %s", s->source_ip);
    if(strlen(s->source_ip) == 0) {
        return;
    }
    assert(s->client == nullptr);
    s->client = new imf::SspClient(ip, loop, 0x40000);
    s->client->init();
    s->client->setOnH264DataCallback(std::bind(ssp_on_video_data, _1, s));
    s->client->setOnAudioDataCallback(std::bind(ssp_on_audio_data, _1, s));
    s->client->setOnMetaCallback(std::bind(ssp_on_meta_data, _1, _2, _3, s));
    s->client->setOnDisconnectedCallback(std::bind(ssp_on_disconnected, s));
    s->client->setOnExceptionCallback(std::bind(ssp_on_exception, _1, _2, s));
    s->client->start();
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

obs_properties_t* ssp_source_getproperties(void* data)
{
	auto s = (struct ssp_source*)data;

	obs_properties_t* props = obs_properties_create();
	obs_properties_set_flags(props, OBS_PROPERTIES_DEFER_UPDATE);

	obs_property_t* source_ip = obs_properties_add_text(props, PROP_SOURCE_IP,
		obs_module_text("SSPPlugin.SourceProps.SourceIp"),
		OBS_TEXT_DEFAULT);

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

	return props;
}

void ssp_source_getdefaults(obs_data_t* settings)
{
	obs_data_set_default_int(settings, PROP_SYNC, PROP_SYNC_SSP_TIMESTAMP);
	obs_data_set_default_int(settings, PROP_LATENCY, PROP_LATENCY_NORMAL);
	obs_data_set_default_string(settings, PROP_SOURCE_IP, "10.98.32.1");
}

void ssp_source_update(void* data, obs_data_t* settings)
{
	auto s = (struct ssp_source*)data;

	if(s->running) {
		s->running = false;
	}
	ssp_stop(s);

	s->hwaccel = obs_data_get_bool(settings, PROP_HW_ACCEL);

	s->sync_mode = (int)obs_data_get_int(settings, PROP_SYNC);
    s->source_ip = obs_data_get_string(settings, PROP_SOURCE_IP);
	const bool is_unbuffered =
		(obs_data_get_int(settings, PROP_LATENCY) == PROP_LATENCY_LOW);
	obs_source_set_async_unbuffered(s->source, is_unbuffered);
    blog(LOG_INFO, "Starting ssp client...");
	s->clientLooper = new imf::ThreadLoop(std::bind(ssp_setup_client, _1, (ssp_source*)data));
	s->clientLooper->start();
	s->running = true;
    blog(LOG_INFO, "SSP client started.");

}

void ssp_source_shown(void* data)
{
    blog(LOG_INFO, "ssp source shown.\n");
}

void ssp_source_hidden(void* data)
{
    blog(LOG_INFO, "ssp source hidden.\n");
}

void ssp_source_activated(void* data)
{
    blog(LOG_INFO, "ssp source activated.\n");
}

void ssp_source_deactivated(void* data)
{
    blog(LOG_INFO, "ssp source deactivated.\n");
}

void* ssp_source_create(obs_data_t* settings, obs_source_t* source)
{
	auto s = (struct ssp_source*)bzalloc(sizeof(struct ssp_source));
	s->source = source;
	s->running = false;
    ssp_source_update(s, settings);
	return s;
}

void ssp_source_destroy(void* data)
{
	auto s = (struct ssp_source*)data;

    if(ffmpeg_decode_valid(&s->adecoder)) {
        ffmpeg_decode_free(&s->adecoder);
    }
    if(ffmpeg_decode_valid(&s->vdecoder)) {
        ffmpeg_decode_free(&s->vdecoder);
    }

	s->running = false;
	ssp_stop(s);
	bfree(s);
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
