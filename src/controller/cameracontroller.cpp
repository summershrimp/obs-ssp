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

#include <QThread>
#include <QUrl>
#include <QQueue>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
#include <QBuffer>
#include <QTcpSocket>

#include <stdio.h>
#include "cameracontroller.h"


CameraController::CameraController(QObject *parent)
	: QObject(parent),
	requesting_(false),
	reply_(Q_NULLPTR),
	networkManager_(new QNetworkAccessManager(this)),
	httpRequestQueue_(new QQueue<struct HttpRequest *>()){
	//connect(networkManager_, SIGNAL(finished(QNetworkReply*)), this, SLOT(handleReqeustResult()));
	connect(networkManager_, &QNetworkAccessManager::networkAccessibleChanged, this, [=](QNetworkAccessManager::NetworkAccessibility accessible) {
		if (accessible != QNetworkAccessManager::NetworkAccessibility::Accessible)
			networkManager_->setNetworkAccessible(QNetworkAccessManager::Accessible);
	});
}
 

CameraController::~CameraController() {
    disconnect(this);
    while (!httpRequestQueue_->isEmpty()) {
        delete httpRequestQueue_->dequeue();
    }
    delete httpRequestQueue_;
	httpRequestQueue_ = nullptr;
	delete networkManager_;
	networkManager_ = nullptr;
	reply_ = nullptr;

}

void CameraController::setIp(const QString &ip) {
    cancelAllReqs();
    ip_ = ip;
}


void CameraController::clearConnectionStatus()
{
    cancelAllReqs();
}

void CameraController::cancelAllReqs() {
    while (!httpRequestQueue_->isEmpty()) {
        delete httpRequestQueue_->dequeue();
    }
    cancelCurrentReq();
}

void CameraController::cancelReqs(QStringList keys) {

    if (keys.size() <= 0) {
        return;
    }

    for (int i = 0;i < httpRequestQueue_->size();i++) {
        HttpRequest *req = httpRequestQueue_->at(i);
        if (i != 0 && req != NULL && keys.contains(req->key)) {
            httpRequestQueue_->removeAt(i);
        }
    }
}

void CameraController::resetNetwork() {
    //Log
    QNetworkAccessManager::NetworkAccessibility access = networkManager_->networkAccessible();

    networkManager_->setNetworkAccessible(QNetworkAccessManager::Accessible);

    //Log
    access = networkManager_->networkAccessible();


}


void CameraController::cancelCurrentReq() {
//    if (requesting_) {
//        requesting_ = false;
//
//        if (httpRequestQueue_->size() == 0) {
//            HttpRequest *req = new HttpRequest();
//            req->key = HTTP_REQUEST_KEY_INVALID;
//            httpRequestQueue_->enqueue(req);
//        }
//        QTimer::singleShot(1, reply_, SLOT(abort()));
//    }
}


void CameraController::getCameraConfig(const QString &key, OnRequestCallback callback) {
    getCameraConfig(key, HTTP_COMMAND_TIMEOUT, callback);
}

void CameraController::getCameraConfig(const QString &key, int timeout, OnRequestCallback callback) {
    struct HttpRequest *req = new HttpRequest();
    QString shortPath;
    shortPath.append(URL_CTRL_GET).append("?").append("k=").append(key);
    req->useShortPath = true;
    req->shortPath = shortPath;
    req->key = key;
    req->reqType = RequestType::REQUEST_TYPE_CONFIG;
    req->timeout = timeout;
    req->callback = callback;
    commonRequest(req);
}

void CameraController::getInfo(OnRequestCallback callback) {
    auto *req = new HttpRequest();
    QString shortPath;
    shortPath.append(URL_INFO);
    req->useShortPath = true;
    req->shortPath = shortPath;
    req->reqType = RequestType::REQUEST_TYPE_INFO;
    req->timeout = HTTP_COMMAND_TIMEOUT;
    req->callback = callback;
    commonRequest(req);
}


void CameraController::requestForCode(const QString &shortPath, OnRequestCallback callback) {
    requestForCode(shortPath, HTTP_COMMAND_TIMEOUT, callback);
}

void CameraController::requestForCode(const QString &shortPath, int timeout, OnRequestCallback callback) {
    struct HttpRequest *req = new HttpRequest();
    req->useShortPath = true;
    req->shortPath = shortPath;
    req->reqType = RequestType::REQUEST_TYPE_CODE;
    req->timeout = timeout;
    req->callback = callback;
    commonRequest(req);
}
void CameraController::setCameraConfig(const QString &key, const QString &value, OnRequestCallback callback) {
    QString shortPath;
    // /ctrl/stream_setting?index=stream1&width=1920&height=1080
    shortPath.append(URL_CTRL_SET).append("?").append(key).append("=").append(value);
    requestForCode(shortPath, callback);
}

void CameraController::setSendStream(const QString & value, OnRequestCallback callback) {
    QString shortPath;
    shortPath.append(URL_CTRL_SET).append("?send_stream=").append(value);
    requestForCode(shortPath, callback);
}
void CameraController::setStreamBitrate(const QString &index, const QString &bitrate, OnRequestCallback callback) {
    QString shortPath;
    shortPath.append(URL_CTRL_STREAM_SETTING).append("?index=").append(index).append("&bitrate=").append(bitrate);
    requestForCode(shortPath, callback);
}

void CameraController::setStreamBitrateAndGop(const QString &index, const QString &bitrate,const QString &gop, OnRequestCallback callback) {
	QString shortPath;
	shortPath.append(URL_CTRL_STREAM_SETTING).append("?index=").append(index).append("&bitrate=").append(bitrate).append("&gop_n=").append(gop).append("&bitwidth=8bit");
	requestForCode(shortPath, callback);
}

void CameraController::setStreamBitwidth(const QString &index, const QString &bitwidth, OnRequestCallback callback) {
    QString shortPath;
    shortPath.append(URL_CTRL_STREAM_SETTING).append("?index=").append(index).append("&bitwidth=").append(bitwidth);
    requestForCode(shortPath, callback);
}

void CameraController::setStreamResolution(const QString &index, const QString &width,const QString& height, OnRequestCallback callback) {
	QString shortPath;
	// /ctrl/stream_setting?index=stream1&width=1920&height=1080
	shortPath.append(URL_CTRL_STREAM_SETTING).append("?index=").append(index).append("&width=").append(width).append("&height=").append(height);
	requestForCode(shortPath, callback);
}
void CameraController::setStreamCodec(const QString &index, const QString &codec, OnRequestCallback callback) {
	QString shortPath;
	// /ctrl/stream_setting?index=stream1&width=1920&height=1080
	//shortPath.append(URL_CTRL_STREAM_SETTING).append("?index=").append(index).append("&encoderType=").append(codec).append("bitwidth=8bit");
	shortPath.append(URL_CTRL_STREAM_SETTING).append("?index=").append(index.toLower()).append("&bitwidth=8bit");
	requestForCode(shortPath, callback);
}
void CameraController::setStreamGop(const QString &index, const QString &gop, OnRequestCallback callback) {
	QString shortPath;
	// /ctrl/stream_setting?index=stream1&width=1920&height=1080
	//shortPath.append(URL_CTRL_STREAM_SETTING).append("?index=").append(index).append("&encoderType=").append(codec).append("bitwidth=8bit");
	shortPath.append(URL_CTRL_STREAM_SETTING).append("?index=").append(index.toLower()).append("&gop_n=").append(gop);
	requestForCode(shortPath, callback);
}
void CameraController::setStreamFPS(const QString &index, const QString& fps, OnRequestCallback callback) {
	QString shortPath;
	// /ctrl/stream_setting?index=stream1&width=1920&height=1080
	shortPath.append(URL_CTRL_STREAM_SETTING).append("?index=").append(index).append("&fps=").append(fps);
	requestForCode(shortPath, callback);
}
void CameraController::getStreamInfo(const QString & index, OnRequestCallback callback)
{
	QString shortPath;
	shortPath.append(URL_CTRL_STREAM_SETTING).append("?index=").append(index).append("&action=query");
	struct HttpRequest *req = new HttpRequest();
	req->useShortPath = true;
	req->key = URL_CTRL_STREAM_SETTING;
	req->shortPath = shortPath;
	req->reqType = RequestType::REQUEST_TYPE_STREAM_INFO;
	req->timeout = HTTP_COMMAND_TIMEOUT;
	req->callback = callback;
	commonRequest(req);
}



void CameraController::commonRequest(HttpRequest *req) {
    //httpRequestQueue_->enqueue(req);
    nextRequest(req);
}

void CameraController::nextRequest(HttpRequest *req) {
    if (networkManager_ == NULL) {
        return;
    }
    requesting_ = true;
    QString path = buildRequestPath(req->shortPath, req->fullPath, req->useShortPath);

    QUrl url(path);
    qDebug() << url;
    QNetworkRequest request;
    request.setRawHeader("Connection", "Keep-Alive");
    request.setUrl(url);

    reply_ = networkManager_->get(request);
    QTimer::singleShot(req->timeout, reply_, SLOT(abort()));
    connect(reply_, &QNetworkReply::finished, [=](){
        handleRequestResult(req, reply_);
    });
}

void CameraController::nextRequest() {
    if (httpRequestQueue_->size() > 0 && !requesting_ && networkManager_ != NULL) {
        requesting_ = true;
        const struct HttpRequest *req = httpRequestQueue_->head();
        QString path = buildRequestPath(req->shortPath, req->fullPath, req->useShortPath);

        QUrl url(path);
        QNetworkRequest request;
        request.setRawHeader("Connection", "Keep-Alive");
        request.setUrl(url);
	
        reply_ = networkManager_->get(request);
        QTimer::singleShot(req->timeout, reply_, SLOT(abort()));
    }
}

void CameraController::handleRequestResult(HttpRequest *req, QNetworkReply *reply_) {
    int httpCode = 999;
    if (reply_->attribute(QNetworkRequest::HttpStatusCodeAttribute).isValid()) {
        httpCode = reply_->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    }
    requesting_ = false;
//    if (req->key == HTTP_REQUEST_KEY_INVALID) {
//        reply_->deleteLater();
//
//        nextRequest();
//        delete req;
//        return;
//    }
    struct HttpResponse *rsp = new HttpResponse();
    rsp->reqKey = req->key;
    rsp->reqValue = req->value;
    rsp->shortPath = req->shortPath;
    rsp->reqType = req->reqType;
    rsp->statusCode = httpCode;
    rsp->responseError = reply_->error();

    QString info = req->useShortPath ? ip_ + rsp->shortPath : req->fullPath;
    if (reply_->error() == QNetworkReply::NetworkError::NoError) {
        parseResponse(reply_->readAll(), rsp, req->reqType);

    } else {
        if (info.contains(SESSION_HEARTBEAT) &&
            rsp->responseError == QNetworkReply::NetworkError::UnknownNetworkError) {
            resetNetwork();
        }
    }
    reply_->deleteLater();
    reply_ = nullptr;
    if (rsp->shortPath == URL_CTRL_SESSION) {

    }

    if (req->callback != NULL) {
        req->callback(rsp);
    } else {
        delete rsp;
    }

    delete req;
}

void CameraController::handleReqeustResult() {
	requesting_ = false;
    if (httpRequestQueue_->size() > 0) {
        struct HttpRequest *req = httpRequestQueue_->dequeue();
        handleRequestResult(req, reply_);
    }   
    nextRequest();
}


void CameraController::parseResponse(const QByteArray &byteData, struct HttpResponse *rsp, RequestType reqType) {
    qDebug() << byteData;
    QJsonDocument doc(QJsonDocument::fromJson(byteData));
    if (!doc.isNull() && doc.isObject()) {
        if (reqType == REQUEST_TYPE_CONFIG) {
            CameraConfig::parseForConfig(doc.object(), rsp);
        } else if (reqType == REQUEST_TYPE_INFO) {
            rsp->code = 0;
            rsp->currentValue = doc["model"].toString();
        } else if (reqType == REQUEST_TYPE_FILES) {
        } else if (reqType == REQUEST_TYPE_MEDIA_INFO) {
        } else if (reqType == REQUEST_TYPE_NETINFO) {
		}
		else if (reqType == REQUEST_TYPE_STREAM_INFO) {
			CameraConfig::parseForStreamInfo(doc.object(), rsp);
		}
		else {
            CameraConfig::parseForCommon(doc.object(), rsp);
        }
    }
}


QString CameraController::buildRequestPath(const QString &shortPath, const QString &ip, bool useShortPath) {
    if (useShortPath) {
        QString path;
        path.append("http://").append(ip_).append(shortPath);
        return path;
    } else {
        QString path;
        path.append("http://").append(ip);
        return path;
    }
}

