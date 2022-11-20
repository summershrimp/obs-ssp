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

#ifndef CAMERACONTROLLER_H
#define CAMERACONTROLLER_H

#define ZCAM_RTMP 1
#include <QObject>
#include <QQueue>
#include <QNetworkReply>
#include <QAbstractSocket>
#include <functional>
#include <cstdio>

#include "cameraconfig.h"

#define URL_INFO "/info"
#define URL_CTRL_GET "/ctrl/get"
#define URL_CTRL_SET "/ctrl/set"
#define URL_CTRL_STREAM "/ctrl/primary_stream"
#define URL_CTRL_STREAM_SETTING "/ctrl/stream_setting"
#define URL_CTRL_SESSION "/ctrl/session"

#define HTTP_REQUEST_KEY_INVALID "invalid"

#define HTTP_SHORT_TIMEOUT 3 * 1000
#define HTTP_NORMAL_TIMEOUT 5 * 1000
#define HTTP_COMMAND_TIMEOUT 3 * 1000
#define HTTP_LONG_TIMEOUT 8 * 1000
#define HTTP_GET_MODE_LONG_TIMEOUT 20 * 1000

#define SESSION_HEARTBEAT_FAIL_TIME 2

class CameraConfig;
class QTimer;
class QUrl;
class QNetworkAccessManager;
class QBuffer;
class QTcpSocket;

class CameraController : public QObject {
	Q_OBJECT
public:
	explicit CameraController(QObject *parent = 0);
	~CameraController();

	void getCameraConfig(const QString &key, int timeout,
			     OnRequestCallback callback);
	void getCameraConfig(const QString &key, OnRequestCallback callback);
	void setCameraConfig(const QString &key, const QString &value,
			     OnRequestCallback callback);
	void getInfo(OnRequestCallback callback);

	void requestForCode(const QString &shortPath, int timeout,
			    OnRequestCallback callback);
	void requestForCode(const QString &shortPath,
			    OnRequestCallback callback);

	void setSendStream(const QString &value, OnRequestCallback callback);
	void setStreamBitrate(const QString &index, const QString &bitrate,
			      OnRequestCallback callback);
	void setStreamBitrateAndGop(const QString &index,
				    const QString &bitrate, const QString &gop,
				    OnRequestCallback callback);
	void setStreamResolution(const QString &index, const QString &width,
				 const QString &height,
				 OnRequestCallback callback);
	void setStreamFPS(const QString &index, const QString &fps,
			  OnRequestCallback callback);
	void setStreamCodec(const QString &index, const QString &codec,
			    OnRequestCallback callback);
	void setStreamGop(const QString &index, const QString &gop,
			  OnRequestCallback callback);
	void setStreamBitwidth(const QString &index, const QString &bitwidth,
			       OnRequestCallback callback);
	void getStreamInfo(const QString &index, OnRequestCallback callback);
	void setIp(const QString &ip);

	void clearConnectionStatus();
	void cancelCurrentReq();
	void cancelAllReqs();
	void cancelReqs(QStringList keys);
	void resetNetwork();
	QString ip() const { return ip_; }

private slots:
	void handleReqeustResult();

private:
	void handleRequestResult(HttpRequest *req, QNetworkReply *reply);
	//Http request
	void nextRequest();
	void nextRequest(HttpRequest *req);
	void commonRequest(struct HttpRequest *req);
	void parseResponse(const QByteArray &byteData, struct HttpResponse *rsp,
			   RequestType reqType);
	QString buildRequestPath(const QString &shortPath, const QString &ip,
				 bool useShortPath);

	QString ip_;
	bool requesting_;
	QNetworkReply *reply_;
	QNetworkAccessManager *networkManager_;
	QQueue<struct HttpRequest *> *httpRequestQueue_;
};

#endif // CAMERACONTROLLER_H
