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
#include <QThread>
#include <qjsondocument.h>
#include <QApplication>

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
	// Make sure this operation runs in the main thread
	if (QThread::currentThread() != QApplication::instance()->thread()) {
		QMetaObject::invokeMethod(
			this,
			[this, callback]() { this->getResolution(callback); },
			Qt::QueuedConnection);
		return;
	}

	controller->getCameraConfig(
		CONFIG_KEY_MOVIE_RESOLUTION, [=](HttpResponse *rsp) {
			if (rsp->statusCode != 200 || rsp->code != 0) {
				callback(false);
				return false;
			}
			if (rsp->choices.count() > 0) {
				resolutions.clear();
			}
			for (const auto &i : rsp->choices) {
				resolutions.push_back(i);
			}

			current_resolution = rsp->currentValue;

			callback(true);
			return true;
		});
}

void CameraStatus::getFramerate(const StatusUpdateCallback &callback)
{
	// Make sure this operation runs in the main thread
	if (QThread::currentThread() != QApplication::instance()->thread()) {
		QMetaObject::invokeMethod(
			this,
			[this, callback]() { this->getFramerate(callback); },
			Qt::QueuedConnection);
		return;
	}

	controller->getCameraConfig(
		CONFIG_KEY_PROJECT_FPS, [=](HttpResponse *rsp) {
			if (rsp->statusCode != 200 || rsp->code != 0) {
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
	// Make sure this operation runs in the main thread
	if (QThread::currentThread() != QApplication::instance()->thread()) {
		QMetaObject::invokeMethod(
			this,
			[this, callback]() {
				this->getCurrentStream(callback);
			},
			Qt::QueuedConnection);
		return;
	}

	controller->getCameraConfig(
		CONFIG_KEY_SEND_STREAM, [=](HttpResponse *rsp) {
			if (rsp->statusCode != 200 || rsp->code != 0) {
				callback(false);
				return false;
			}
			controller->getStreamInfo(
				rsp->currentValue, [=](HttpResponse *rsp) {
					if (rsp->statusCode != 200 ||
					    rsp->code != 0) {
						callback(false);
						return false;
					}
					current_streamInfo = rsp->streamInfo;
					blog(LOG_INFO,
					     "%s get stream info % s, %d %dx%d ",
					     getIp().toStdString().c_str(),
					     current_streamInfo.steamIndex_
						     .toStdString()
						     .c_str(),
					     current_streamInfo.fps,
					     current_streamInfo.width_,
					     current_streamInfo.height_);
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
		//cb(ok);
		//return ok;
		if (ok) {
			getResolution([=](bool ok) {
				getFramerate(
					[=](bool ok) { getCurrentStream(cb); });
			});
		};
	});
}
void CameraStatus::getInfo(const StatusUpdateCallback &callback)
{
	// Make sure this operation runs in the main thread
	if (QThread::currentThread() != QApplication::instance()->thread()) {
		QMetaObject::invokeMethod(
			this, [this, callback]() { this->getInfo(callback); },
			Qt::QueuedConnection);
		return;
	}

	controller->getInfo([=](HttpResponse *rsp) {
		if (rsp->statusCode != 200 || rsp->code != 0) {
			callback(false);
			return false;
		}
		QJsonDocument doc(
			QJsonDocument::fromJson(rsp->currentValue.toUtf8()));

		model = doc["model"].toString();
		name = doc["cameraName"].toString();
		nickName = doc["nickName"].toString();
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
void CameraStatus::doSetStreamReolutionInternal(QString index,
						QString real_resolution,
						QString width, QString height,
						QString bitrate2, QString fps,
						StatusReasonUpdateCallback cb)

{
	if (current_resolution == real_resolution) {
		doSetStreamFpsInternal(index, width, height, bitrate2, fps, cb);
	} else {
		blog(LOG_INFO, "current resolution %s -> %s ",
		     current_resolution.toStdString().c_str(),
		     real_resolution.toStdString().c_str());
		controller->setCameraConfig(
			CONFIG_KEY_MOVIE_RESOLUTION, real_resolution,
			[=](HttpResponse *rsp) {
				if (rsp->statusCode != 200 || rsp->code != 0) {
					return cb(
						false,
						QString("Failed to set movie resolution to %1")
							.arg(real_resolution));
				}
				this->current_resolution = real_resolution;
				blog(LOG_INFO, "Setting fps");
				doSetStreamFpsInternal(index, width, height,
						       bitrate2, fps, cb);
			});
	}
}
void CameraStatus::doSetStreamIndexInternal(QString index, QString width,
					    QString height, QString bitrate2,
					    QString fps,
					    StatusReasonUpdateCallback cb)
{
	if (this->current_index == index) {
		blog(LOG_INFO, "Setting stream attr , stream index correct");
		this->current_index = index;
		doSetStreamInternal(index, width, height, bitrate2, fps, cb);
	} else {
		blog(LOG_INFO, "Setting index from %s to %s ",
		     current_index.toStdString().c_str(),
		     index.toStdString().c_str());
		controller->setSendStream(index, [=](HttpResponse *rsp) {
			if (rsp->statusCode != 200 || rsp->code != 0) {
				return cb(
					false,
					QString("Could not set video encoder to H.265"));
			}
			blog(LOG_INFO, "Setting stream attr");
			this->current_index = index;
			doSetStreamInternal(index, width, height, bitrate2, fps,
					    cb);
		});
	}
}
void CameraStatus::doSetStreamFpsInternal(QString index, QString width,
					  QString height, QString bitrate2,
					  QString fps,
					  StatusReasonUpdateCallback cb)
{
	//int iFps = int(fps.toFloat() + 0.1);
	QString projectFps = fps;
	if (fps == "30") {
		projectFps = "29.97";
	} else if (fps == "60") {
		projectFps = "59.94";
	}
	if (this->current_framerate == projectFps) {
		doSetStreamIndexInternal(index, width, height, bitrate2, fps,
					 cb);
	} else {
		blog(LOG_INFO, "current projectfps %s -> %s ",
		     current_framerate.toStdString().c_str(),
		     projectFps.toStdString().c_str());
		controller->setCameraConfig(
			CONFIG_KEY_PROJECT_FPS, projectFps,
			[=](HttpResponse *rsp) {
				if (rsp->statusCode != 200 || rsp->code != 0) {
					return cb(
						false,
						QString("Failed to set fps to %1")
							.arg(fps));
				}
				this->current_framerate = fps;
				doSetStreamIndexInternal(index, width, height,
							 bitrate2, fps, cb);
			});
	}
}
void CameraStatus::doSetStreamBitrateInternal(int stream_index,
					      QString resolution,
					      bool low_noise, QString fps,
					      int bitrate,
					      StatusReasonUpdateCallback cb)
{
}
void CameraStatus::doSetStreamInternal(QString index, QString width,
				       QString height, QString bitrate2,
				       QString fps,
				       StatusReasonUpdateCallback cb)
{
	controller->getStreamInfo(index.toLower(), [=](HttpResponse *rsp) {
		if (rsp->statusCode != 200 || rsp->code != 0) {
			return cb(false, QString("Could not get stream info"));
		}
		int ifps = int(fps.toFloat() + 0.1);
		current_streamInfo = rsp->streamInfo;
		if (width.toInt() == rsp->streamInfo.width_ &&
		    height.toInt() == rsp->streamInfo.height_ &&
		    ifps == rsp->streamInfo.fps &&
		    bitrate2.toInt() == rsp->streamInfo.bitrate_ * 1000 &&
		    10 == rsp->streamInfo.gop_) {

			cb(true, "same no need change");
			return;
		}
		//current_streamInfo = rsp->streamInfo;
		blog(LOG_INFO, "Setting stream from %dx%d %d %d to %dx%d %d %d",
		     width.toInt(), height.toInt(), ifps, bitrate2.toInt(),
		     rsp->streamInfo.width_, rsp->streamInfo.height_,
		     rsp->streamInfo.fps, rsp->streamInfo.bitrate_ * 1000);
		if (current_streamInfo.status_ == "idle") {
			controller->setStreamAttr(
				index.toLower(), width, height, bitrate2, "10",
				QString::number(ifps),
				current_streamInfo.encoderType_,
				[=](HttpResponse *rsp) {
					if (rsp->statusCode != 200 ||
					    rsp->code != 0) {
						return cb(
							false,
							QString("Could not set stream attr"));
					}
					return cb(true, "Success");
				});
		} else {
			// cannot set codec gop ,reoslution etc.
			blog(LOG_INFO, "stream not idle, cannot set ");
			return cb(true, "in streaming");
		}
	});
}

void CameraStatus::doSetStream(int stream_index, QString resolution,
			       bool low_noise, QString fps, int bitrate,
			       StatusReasonUpdateCallback cb)
{
	bool need_downresolution = false;
	blog(LOG_INFO,
	     "In doSetStream index %d resolution %s fps %s bitrate %d",
	     stream_index, resolution.toStdString().c_str(),
	     fps.toStdString().c_str(), bitrate);
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
		//return cb(false,"Resolution doesn't have a single * to seperate w from h");
		arr = QString("1920*1080").split("*");
		blog(LOG_INFO, "resolution error , set to 1920x1080 default");
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
	//blog(LOG_INFO, "Setting movie resolution");
	doSetStreamReolutionInternal(index, real_resolution, width, height,
				     bitrate2, fps, cb);
}

CameraStatus::~CameraStatus()
{
	if (controller) {
		QThread *controllerThread = controller->thread();
		QThread *currentThread = QThread::currentThread();

		if (currentThread == controllerThread) {
			// We are in the controller's thread. Call directly and delete.
			qDebug()
				<< "CameraStatus Destructor: In controller's thread. Cleaning up directly.";
			delete controller;
		} else {
			// We are in a different thread.
			qDebug()
				<< "CameraStatus Destructor: In different thread. Queuing cleanup for controller.";

			// 2. Safely delete the controller on its own thread.
			//    controller->deleteLater() is the simplest and generally the best way.
			//    It posts a delete event to the controller's thread event loop.
			controller->deleteLater();
			qDebug()
				<< "CameraStatus Destructor: Queued deleteLater for controller.";
		}
		controller = nullptr;
	} else {
		qDebug()
			<< "CameraStatus Destructor: Controller was already null.";
	}
}
