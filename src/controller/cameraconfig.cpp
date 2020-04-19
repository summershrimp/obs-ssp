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

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>

#include "cameraConfig.h"

CameraConfig::CameraConfig() {

}

CameraConfig::~CameraConfig() {
    //TODO delete something
}
void CameraConfig::parseForConfig(const QJsonObject &json, HttpResponse *response) {
    response->readOnly = json["ro"].toInt() == 0 ? false : true;
    response->key = json["key"].toString();
    response->code = json["code"].toInt();

    int t = json["type"].toInt();
    if (t == CONFIG_TYPE_CHOICE) {
        response->type = CONFIG_TYPE_CHOICE;
        response->currentValue = json["value"].toString();
        response->choices.clear();
        QJsonArray opts = json["opts"].toArray();
        for (int index = 0; index < opts.size(); index++) {
            QString val = opts[index].toString();
            response->choices.append(val);
        }
    } else if (t == CONFIG_TYPE_RANGE) {
        response->type = CONFIG_TYPE_RANGE;
        response->intValue = json["value"].toInt();
        response->min = json["min"].toInt();
        response->max = json["max"].toInt();
        response->step = json["step"].toInt();
    } else {
        response->type = CONFIG_TYPE_STRING;
        response->currentValue = json["value"].toString();
    }
}

void CameraConfig::parseForCommon(const QJsonObject &json, HttpResponse *response) {
    response->code = json["code"].toInt();
    response->msg = json["msg"].toString();
}

void CameraConfig::parseForStreamInfo(const QJsonObject & json, HttpResponse * response)
{
	response->code = 0;
	response->streamInfo.bitrate_ = json["bitrate"].toInt();
	response->streamInfo.encoderType_= json["encoderType"].toString();
	response->streamInfo.gop_ = json["gop_n"].toInt();
	response->streamInfo.height_ = json["height"].toInt();
	response->streamInfo.rotation_ = json["rotation"].toInt();
	response->streamInfo.splitDuration_ = json["splitDuration"].toInt();
	response->streamInfo.status_ = json["status"].toString();
	response->streamInfo.steamIndex_ = json["streamIndex"].toString();
	response->streamInfo.width_ = json["width"].toInt();
	if (json.contains("bitwidth")) {
		response->streamInfo.bitWidth_ = json["bitwidth"].toString();
	}
	if (json.contains("fps")) {
		response->streamInfo.fps = json["fps"].toInt();
	}
}








