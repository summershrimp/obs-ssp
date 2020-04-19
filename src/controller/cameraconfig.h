/* 
 * Copyright (c) 2015-2020, Shenzhen Imaginevision Technology Limited 
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
 * 3.  Neither the name of Shenzhen Imaginevision Technology Limited ("ZCAM")
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

#ifndef CAMERACONFIG_H
#define CAMERACONFIG_H

#include <QList>
#include <QNetworkReply>
#include <functional>
#include <cstdio>

class CameraController;

#define CONFIG_KEY_SATURATION "saturation"
#define CONFIG_KEY_SHARPNESS "sharpness"
#define CONFIG_KEY_CONTRAST "contrast"
#define CONFIG_KEY_BRIGHTNESS "brightness"
#define CONFIG_KEY_LUMA_LEVEL "luma_level"
#define CONFIG_KEY_METERING "meter_mode"
#define CONFIG_KEY_FLICKER "flicker"
#define CONFIG_KEY_ISO "iso"
#define CONFIG_KEY_MAX_ISO "max_iso"
#define CONFIG_KEY_WB "wb"
#define CONFIG_KEY_MWB "mwb"
#define CONFIG_KEY_WB_PRIORITY "wb_priority"
#define CONFIG_KEY_IRIS "iris"
#define CONFIG_KEY_AF_MODE "af_mode"
#define CONFIG_KEY_AF_AREA "af_area"
#define CONFIG_KEY_FOCUS_METHOD "focus"
#define CONFIG_KEY_MF_DRIVE "mf_drive"
#define CONFIG_KEY_CAF "caf"
#define CONFIG_KEY_CAF_RANGE "caf_range"
#define CONFIG_KEY_CAF_SENSITIVITY "caf_sens"
#define CONFIG_KEY_CAF_LIVE "live_caf"
#define CONFIG_KEY_MF_MAG "mf_mag"
#define CONFIG_KEY_MF_ASSIST "mf_assist"
#define CONFIG_KEY_PHOTO_QUALITY "photo_q"
#define CONFIG_KEY_PHOTO_TIMELAPSE "photo_tl"
#define CONFIG_KEY_PHOTO_BURST "burst"
#define CONFIG_KEY_PHOTO_BURST_SPD "burst_spd"
#define CONFIG_KEY_SHUTTER_SPEED "shutter_spd"
#define CONFIG_KEY_SHUTTER_ANGLE "shutter_angle"
#define CONFIG_KEY_SHUTTER_TIME "shutter_time"
#define CONFIG_KEY_EXPOSURE_MODE "shoot_mode"
#define CONFIG_KEY_SHUTTER_OP "sht_operation"
#define CONFIG_KEY_SPLIT_DURATION "split_duration"
#define CONFIG_KEY_LENS_ZOOM "lens_zoom"
#define CONFIG_KEY_LENS_ZOOM_POS "lens_zoom_pos"
#define CONFIG_KEY_LENS_FOCUS_POS "lens_focus_pos"
#define CONFIG_KEY_DRIVE_MODE "drive_mode"
#define CONFIG_KEY_RECORD_MODE "record_mode"
#define CONFIG_KEY_SELF_TIMER_INTERVAL "photo_self_interval"
#define CONFIG_KEY_RECORD_TL_INTERVAL "record_tl_interval"
#define CONFIG_KEY_TIMELAPSE_INTERVAL "photo_tl_interval"
#define CONFIG_KEY_TIMELAPSE_NUM "photo_tl_num"
#define CONFIG_KEY_VIDEO_SYSTEM "video_system"
#define CONFIG_KEY_HDMI_OSD "hdmi_osd"
#define CONFIG_KEY_FN_BTN_FUN "Fn"
#define CONFIG_KEY_CAMERA_ROT "camera_rot"
#define CONFIG_KEY_HDMI_FMT "hdmi_fmt"
#define CONFIG_KEY_TINT "tint"
#define CONFIG_KEY_LCD_BACK_LIGHT "lcd_backlight"
#define CONFIG_KEY_FILE_NUMBER "file_number"
#define CONFIG_KEY_VIDEO_OUTPUT "video_output"
#define CONFIG_KEY_POWER_BTN_FUN "F2"
#define CONFIG_KEY_OLED "oled"
#define CONFIG_KEY_BEEP "beep"
#define CONFIG_KEY_LED "led"
#define CONFIG_KEY_LCD "lcd"
#define CONFIG_KEY_LCD_AUTO_OFF "auto_off_lcd"
#define CONFIG_KEY_ROTATION "rotation"
#define CONFIG_KEY_MAX_EXPOSSURE_TIME "max_exp"
#define CONFIG_KEY_MAX_EXPOSSURE_SHUTTER_TIME "max_exp_shutter_time"
#define CONFIG_KEY_MAX_EXPOSSURE_SHUTTER_ANGLE "max_exp_shutter_angle"
#define CONFIG_KEY_NOISE_REDUCTION "??"
#define CONFIG_KEY_AUTO_POWER_OFF "auto_off"
#define CONFIG_KEY_LUT "lut"
#define CONFIG_KEY_DISTORTION "dewarp"
#define CONFIG_KEY_VIGNETTE "vignette"
#define CONFIG_KEY_MULTIPLE_MODE "multiple_mode"
#define CONFIG_KEY_GRID_DISPLAY "grid_display"
#define CONFIG_KEY_FUCOS_POS "lens_focus_spd"
#define CONFIG_KEY_FOCUS_SPD "lens_focus_pos"
#define CONFIG_KEY_MOVIE_FORMAT  "movfmt"
#define CONFIG_KEY_MOVIE_RESOLUTION  "resolution"
#define CONFIG_KEY_PROJECT_FPS "project_fps"
#define CONFIG_KEY_BITRATE  "bitrate_level"
#define CONFIG_KEY_VIDEO_ENCODER "video_encoder"
#define CONFIG_KEY_LIVE_FNO "live_ae_fno"
#define CONFIG_KEY_LIVE_ISO "live_ae_iso"
#define CONFIG_KEY_LIVE_SHUTTER "live_ae_shutter"
#define CONFIG_KEY_PHOTO_SIZE "photosize"
#define CONFIG_KEY_SSID "ssid"
#define CONFIG_KEY_AP_KEY "ap_key"
#define CONFIG_KEY_BATTERY "battery"
#define CONFIG_KEY_EV "ev"
#define CONFIG_KEY_PRIMARY_BITRATE "primary_bitrate"
#define CONFIG_KEY_SEC_RATE_CONTROL "sec_rate_control"
#define CONFIG_KEY_SEC_STREAM_ROTATION "sec_rotate"
#define CONFIG_KEY_SEC_AUDIO "sec_audio"
#define CONFIG_KEY_SEC_RESOLUTION "sec_resolution"
#define CONFIG_KEY_SEC_GOP_M "sec_gop_m"
#define CONFIG_KEY_SEC_GOP_N "sec_gop_n"
#define CONFIG_KEY_SEC_BITRATE "sec_bitrate"
#define CONFIG_KEY_SUPPORT_PREVIEW "support_sec"

#define CONFIG_KEY_UNION_AE "union_ae"
#define CONFIG_KEY_UNION_AE_DELTA "union_ae_delta"
#define CONFIG_KEY_UNION_AE_PRIORITY "union_ae_priority"
#define CONFIG_KEY_UNION_AWB "union_awb"
#define CONFIG_KEY_UNION_AWB_DELTA "union_awb_delta"
#define CONFIG_KEY_UNION_AWB_PRIORITY "union_awb_priority"

#define CONFIG_KEY_NETWORK_MODE "Network"
#define CONFIG_KEY_AWB_COORDINATE "-awb_coordinate-"
#define CONFIG_KEY_AWB_METERING "-awb_metering-"
#define CONFIG_KEY_AE_COORDINATE "-ae_coordinate-"
#define CONFIG_KEY_AE_METERING "-ae_metering-"

#define CONFIG_KEY_VIDEO_RECORD_DURATION "rec_duration"
#define CONFIG_KEY_VIDEO_TIMELASE_ENABLE "enable_video_tl"
#define CONFIG_KEY_VIDEO_TIMELAPSE_INTERVAL "video_tl_interval"
#define CONFIG_KEY_SNAP_INTERVAL "vr_snap_interval"
#define CONFIG_KEY_SNAP_ENABLE "enable_vr_snap"
#define CONFIG_KEY_SNAP_COUNT "vr_snap_count"
#define CONFIG_KEY_SNAP_TOKEN "vr_snap_token"

#define CONFIG_KEY_MULTIPLE_ID "multiple_id"
#define CONFIG_KEY_MULTIPLE_MODE "multiple_mode"

#define CONFIG_KEY_SN "sn"
#define CONFIG_KEY_SEND_STREAM "send_stream"

#define  CONFIG_KEY_AE_LOCK  "ae_lock"

#define SESSION_HEARTBEAT "heart_x_beat"

#define MUTIPLE_MODE_MASTER "master"
#define MUTIPLE_MODE_SLAVE "slave"
#define MUTIPLE_MODE_NONE "none"

#define NETWORK_MODE_ROUTER "Router"
#define NETWORK_MODE_DIRECT "Direct"
#define NETWORK_MODE_STATIC "Static"
#define SEND_STREAM_0       "Stream0"
#define SEND_STREAM_1       "Stream1"
#define AE_LOCK             "Lock"
#define AE_UNLOCK           "Unlock"
#define CONFIG_KEY_AUDIO_ENCODER  "primary_audio"
#define CONFIG_KEY_AUDIO_CHANNEL  "audio_channel"
#define CONFIG_KEY_AUDIO_IN_GAIN  "audio_input_gain"
#define CONFIG_KEY_AUDIO_OUT_GAIN "audio_output_gain"
#define CONFIG_KEY_AUDIO_PHANTOM_POWER "audio_phantom_power"
#define CONFIG_KEY_TC_COUNT_UP    "tc_count_up"
#define CONFIG_KEY_TC_HDMI        "tc_hdmi_dispaly"
#define CONFIG_KEY_MOVVFR          "movvfr" 
#define CONFIG_KEY_WDR             "compose_mode"
#define CONFIG_KEY_DUAL_ISO	       "dual_iso"
#define CONFIG_KEY_RECORD_FILE_FORMAT "record_file_format"
#define CONFIG_KEY_REC_PROXY        "rec_proxy_file"
#define CONFIG_KEY_REC_FPS          "rec_fps"
#define CONFIG_KEY_WIFI          "wifi"

#define CAMERA_CONFIG_MOVIE_FORMAT      0
#define CAMERA_CONFIG_PHOTO_SIZE        1
#define CAMERA_CONFIG_WB                2
#define CAMERA_CONFIG_ISO               3
#define CAMERA_CONFIG_SHARPNESS         4
#define CAMERA_CONFIG_CONTRAST          5
#define CAMERA_CONFIG_AE_METER_MODE     6

#define SAVE_IP_DEPARATOR       "-"
#define DOWNLOAD_THREAD_POOL_MAX_DEFAULT_SIZE 2

#define HTTP_DEFAULT_PORT  80

#define HEART_BEAT_INTERVAL (1000 * 5)

#define SET_CONFIG_DELAY_TIME  1000


enum RequestType {
    REQUEST_TYPE_CODE,
    REQUEST_TYPE_MESSAGE,
    REQUEST_TYPE_CONFIG,
    REQUEST_TYPE_INFO,
    REQUEST_TYPE_FILES,
    REQUEST_TYPE_MEDIA_INFO,
    REQUEST_TYPE_NETINFO,
	REQUEST_TYPE_STREAM_INFO
};

enum ConfigType {
    CONFIG_TYPE_CHOICE = 1,
    CONFIG_TYPE_RANGE,
    CONFIG_TYPE_STRING
};


struct CameraConfigInfo {
    int code;
    int min;
    int max;
    int step;
    int intValue;
    bool readOnly;
    QString msg;
    QString key;
    QString currentValue;
    ConfigType type;
    QList<QString> choices;
};

struct StreamInfo {
	QString steamIndex_;
	QString encoderType_;
	QString bitWidth_;
	int width_;
	int height_;
	int fps;
	int bitrate_;
	int gop_;
	int rotation_;
	int splitDuration_;
	QString status_;
};
struct HttpResponse {
 	struct StreamInfo streamInfo;

    //Config type
    int intValue;
    int min;
    int max;
    int step;
    int code;
    int statusCode;
    bool readOnly;
    ConfigType type;
    RequestType reqType;
    QString reqKey;
    QString reqValue;
    QString key;
    QString msg;
    QString shortPath;
    QString currentValue;
    QList<QString> choices;
    QList<QString> files;
    QNetworkReply::NetworkError responseError;
};

typedef std::function<void(struct HttpResponse *rsp)> OnRequestCallback;

struct HttpRequest {
    bool useShortPath;
    int timeout;
    QString key;
    QString value;
    QString shortPath;
    QString fullPath;
    RequestType reqType;
    OnRequestCallback callback;
};

typedef struct {
    QString name;
    QString sn;
    QString inputFmt;
    QString outputFmt;
    QString time;
    QStringList configs;
}SBOConfig;

class CameraConfig {
public:
    static ConfigType getConfigType(const QString &key);
    static void parseForConfig(const QJsonObject &json, struct HttpResponse *response);
    static void parseForCommon(const QJsonObject &json, struct HttpResponse *response);
	static void parseForStreamInfo(const QJsonObject &json, struct HttpResponse *response);
    CameraConfig();
    ~CameraConfig();
};

#endif // CAMERACONFIG_H










