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

#ifndef SSP_CONNECTOR_PROTO_H_
#define SSP_CONNECTOR_PROTO_H_

#include <stdint.h>

#pragma pack(1)

#define SSP_PROTO

struct SSP_PROTO VideoMeta {
	uint32_t width;
	uint32_t height;
	uint32_t timescale;
	uint32_t unit;
	uint32_t gop;
	uint32_t encoder;
};

struct SSP_PROTO AudioMeta {
	uint32_t timescale;
	uint32_t unit;
	uint32_t sample_rate;
	uint32_t sample_size;
	uint32_t channel;
	uint32_t bitrate;
	uint32_t encoder;
};

struct SSP_PROTO BaseMeta {
	uint16_t pts_is_wall_clock;
	uint16_t tc_drop_frame;
	uint32_t timecode;
};

struct SSP_PROTO Metadata {
	struct BaseMeta meta;
	struct VideoMeta vmeta;
	struct AudioMeta ameta;
};

struct SSP_PROTO VideoData {
	uint64_t pts;
	uint64_t ntp_timestamp;
	uint32_t frm_no;
	uint32_t type; // I or P
	size_t len;
	uint8_t data[0];
};

struct SSP_PROTO AudioData {
	uint64_t pts;
	uint64_t ntp_timestamp;
	size_t len;
	uint8_t data[0];
};

enum MessageType {
	MetaDataMsg = 1,
	VideoDataMsg,
	AudioDataMsg,
	RecvBufferFullMsg,
	DisconnectMsg,
	ConnectionConnectedMsg,
	ExceptionMsg,
	ConnectorOkMsg,
};

struct Message {
	uint32_t type;
	uint32_t length;
	uint8_t value[0];
};

#pragma pack()

#endif