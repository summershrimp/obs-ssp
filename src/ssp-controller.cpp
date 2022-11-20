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

#include <QMetaType>
#include "ssp-controller.h"
#include <obs-module.h>

CameraStatus::CameraStatus() : QObject()
{
	controller = new CameraController(this);

	qRegisterMetaType<StatusUpdateCallback>("StatusUpdateCallback");
	qRegisterMetaType<StatusReasonUpdateCallback>(
		"StatusReasonUpdateCallback");
	connect(this,
		SIGNAL(onSetStream(int, QString, bool, QString, int,
				   StatusReasonUpdateCallback)),
		this,
		SLOT(doSetStream(int, QString, bool, QString, int,
				 StatusReasonUpdateCallback)));
	connect(this, SIGNAL(onSetLed(bool)), this, SLOT(doSetLed(bool)));
	connect(this, SIGNAL(onRefresh(StatusUpdateCallback)), this,
		SLOT(doRefresh(StatusUpdateCallback)));
};

void CameraStatus::setIp(const QString &ip)
{
	controller->setIp(ip);
}

void CameraStatus::getResolution(const StatusUpdateCallback &callback)
{
	controller->getCameraConfig(
		CONFIG_KEY_MOVIE_RESOLUTION, [=](HttpResponse *rsp) {
			if (rsp->statusCode == 999) {
				callback(false);
				return false;
			}
			if (rsp->choices.count() > 0) {
				resolutions.clear();
			}
			for (const auto &i : rsp->choices) {
				resolutions.push_back(i);
			}

			current_framerate = rsp->currentValue;

			callback(true);
			return true;
		});
}

void CameraStatus::getFramerate(const StatusUpdateCallback &callback)
{
	controller->getCameraConfig(
		CONFIG_KEY_PROJECT_FPS, [=](HttpResponse *rsp) {
			if (rsp->statusCode == 999) {
				callback(false);
				return false;
			}

			if (rsp->choices.count() > 0) {
				framerates.clear();
			}
			for (const auto &i : rsp->choices) {
				framerates.push_back(i);
			}
			current_framerate = rsp->currentValue;
			callback(true);
			return true;
		});
}

void CameraStatus::getCurrentStream(const StatusUpdateCallback &callback)
{
	controller->getCameraConfig(
		CONFIG_KEY_SEND_STREAM, [=](HttpResponse *rsp) {
			if (rsp->statusCode == 999) {
				callback(false);
				return false;
			}
			controller->getStreamInfo(
				rsp->currentValue, [=](HttpResponse *rsp) {
					if (rsp->statusCode == 999) {
						callback(false);
						return false;
					}
					current_streamInfo = rsp->streamInfo;
					callback(true);
					return true;
				});
			return true;
		});
}

void CameraStatus::refreshAll(const StatusUpdateCallback &cb)
{
	emit onRefresh(cb);
}

void CameraStatus::doRefresh(StatusUpdateCallback cb)
{
	this->model = "";
	getInfo([=](bool ok) {
		cb(ok);
		return ok;
	});
}
void CameraStatus::getInfo(const StatusUpdateCallback &callback)
{
	controller->getInfo([=](HttpResponse *rsp) {
		if (rsp->statusCode == 999) {
			callback(false);
			return false;
		}
		model = rsp->currentValue;
		callback(true);
		return true;
	});
}
void CameraStatus::setLed(bool isOn)
{
	emit onSetLed(isOn);
}

void CameraStatus::doSetLed(bool isOn)
{
	controller->setCameraConfig(CONFIG_KEY_LED, isOn ? "On" : "Off",
				    [=](HttpResponse *rsp) {

				    });
}

void CameraStatus::setStream(int stream_index, QString resolution,
			     bool low_noise, QString fps, int bitrate,
			     StatusReasonUpdateCallback cb)
{
	blog(LOG_INFO, "In ::setStream emitting onSetStream");
	emit onSetStream(stream_index, resolution, low_noise, fps, bitrate, cb);
}

void CameraStatus::doSetStream(int stream_index, QString resolution,
			       bool low_noise, QString fps, int bitrate,
			       StatusReasonUpdateCallback cb)
{
	bool need_downresolution = false;
	blog(LOG_INFO, "In doSetStream");
	if (model.contains(E2C_MODEL_CODE, Qt::CaseInsensitive)) {
		if (resolution != "1920*1080" && fps.toDouble() > 30) {
			return cb(
				false,
				QString("Cannot go higher than 30fps for >1920x1080 resolution on E2C"));
		}
		if (resolution == "1920*1080" && fps.toDouble() > 30) {
			need_downresolution = true;
		}
	}
	auto bitrate2 = QString::number(bitrate);

	if (model.contains(IPMANS_MODEL_CODE, Qt::CaseInsensitive)) {
		auto index =
			QString("stream") + QString::number(stream_index + 1);
		controller->setStreamBitrate(
			index, bitrate2, [=](HttpResponse *rsp) {
				if (rsp->statusCode != 200 || rsp->code != 0) {
					return cb(
						false,
						QString("Could not set bitrate to %1")
							.arg(bitrate2));
				}
				return cb(true, "Success");
			});
		return;
	}

	QString real_resolution;
	QString width, height;
	auto arr = resolution.split("*");
	if (arr.size() < 2) {
		return cb(
			false,
			"Resolution doesn't have a single * to seperate w from h");
	}
	width = arr[0];
	height = arr[1];
	if (need_downresolution) {
		real_resolution = "1920x1080";
	} else if (resolution == "3840*2160" || resolution == "1920*1080") {
		real_resolution = "4K";
		if (low_noise)
			real_resolution += " (Low Noise)";
	} else if (resolution == "4096*2160") {
		real_resolution = "C4K";
		if (low_noise)
			real_resolution += " (Low Noise)";
	} else {
		return cb(false,
			  QString("Unknown resolution: ").arg(resolution));
	}

	auto index = QString("Stream") + QString::number(stream_index);
	blog(LOG_INFO, "Setting movie resolution");
	controller->setCameraConfig(CONFIG_KEY_MOVIE_RESOLUTION, real_resolution, [=](HttpResponse *rsp) {
		if (rsp->statusCode != 200 || rsp->code != 0) {
			return cb(
				false,
				QString("Failed to set movie resolution to %1")
					.arg(real_resolution));
		}
		blog(LOG_INFO, "Setting fps");
		controller->setCameraConfig(CONFIG_KEY_PROJECT_FPS, fps, [=](HttpResponse *rsp) {
			if (rsp->statusCode != 200 || rsp->code != 0) {
				return cb(false,
					  QString("Failed to set fps to %1")
						  .arg(fps));
			}
			blog(LOG_INFO, "Setting encoder");
			controller->setCameraConfig(CONFIG_KEY_VIDEO_ENCODER, "H.265", [=](HttpResponse *rsp) {
				blog(LOG_INFO, "Setting sendStream");

				controller->setSendStream(index, [=](HttpResponse
									     *rsp) {
					if (rsp->statusCode != 200 ||
					    rsp->code != 0) {
						return cb(
							false,
							QString("Could not set video encoder to H.265"));
					}
					blog(LOG_INFO,
					     "Setting bitrate andn gop");
					controller->setStreamBitrateAndGop(
						index.toLower(), bitrate2, "10",
						[=](HttpResponse *rsp) {
							if (rsp->statusCode !=
								    200 ||
							    rsp->code != 0) {
								return cb(
									false,
									QString("Could not set bitrate to %1 or GOP to 10")
										.arg(bitrate2));
							}
							if (stream_index == 0) {
								return cb(
									true,
									"Success");
							}
							blog(LOG_INFO,
							     "Setting stream resolution");
							controller->setStreamResolution(
								index.toLower(),
								width, height,
								[=](HttpResponse
									    *rsp) {
									if (rsp->statusCode !=
										    200 ||
									    rsp->code !=
										    0) {
										return cb(
											false,
											QString("Could not set stream resolution to %1 x %2")
												.arg(width)
												.arg(height));
									}
									return cb(
										true,
										"Success");
								});
						});
				});
			});
		});
	});
}

CameraStatus::~CameraStatus()
{
	controller->cancelAllReqs();
	delete controller;
}
