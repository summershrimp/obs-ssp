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

#ifndef OBS_SSP_SSP_CONTROLLER_H
#define OBS_SSP_SSP_CONTROLLER_H

#include <functional>
#include <QObject>
#include "controller/cameracontroller.h"

#define E2C_MODEL_CODE "elephant"
#define IPMANS_MODEL_CODE "wlm"

typedef std::function<void(bool ok)> StatusUpdateCallback;
typedef std::function<void(bool ok, QString)> StatusReasonUpdateCallback;


class CameraStatus : QObject {
    Q_OBJECT
public:
    CameraStatus();
    void setIp(const QString& ip);
    QString getIp() {return controller->ip();}
    void getResolution(const StatusUpdateCallback&);
    void getFramerate(const StatusUpdateCallback&);
    void getCurrentStream(const StatusUpdateCallback&);
    void getInfo(const StatusUpdateCallback&);
    void refreshAll(const StatusUpdateCallback&);
    CameraController *getController() { return controller;}
    ~CameraStatus();

    void setLed(bool isOn);

    QString model;
    std::vector<QString> resolutions;
    QString current_resolution;
    std::vector<QString> framerates;
    QString current_framerate;
    StreamInfo current_streamInfo;
    void setStream(int stream_index, QString resolution, bool low_noise, QString fps, int bitrate, StatusReasonUpdateCallback cb);

    signals:
    void onSetStream(int stream_index, QString resolution, bool low_noise, QString fps, int bitrate, StatusReasonUpdateCallback cb);
    void onRefresh(StatusUpdateCallback cb);
    void onSetLed(bool on);
private slots:
    void doSetStream(int stream_index, QString resolution, bool low_noise, QString fps, int bitrate, StatusReasonUpdateCallback cb);
    void doRefresh(StatusUpdateCallback cb);
    void doSetLed(bool on);

private:
    CameraController *controller;
};

#endif //OBS_SSP_SSP_CONTROLLER_H
