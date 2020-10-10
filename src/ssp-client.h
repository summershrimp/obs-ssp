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

#ifndef OBS_SSP_SSP_CLIENT_H
#define OBS_SSP_SSP_CLIENT_H
#include <QObject>
#include <mutex>

#include <imf/ISspClient.h>
#include <imf/threadloop.h>

class SSPClient : public QObject{
    Q_OBJECT

public:
    SSPClient(const std::string &ip, uint32_t bufferSize);
    static void PreStart(SSPClient *my, imf::Loop *loop);

    virtual void setOnRecvBufferFullCallback(const imf::OnRecvBufferFullCallback & cb);
    virtual void setOnH264DataCallback(const imf::OnH264DataCallback & cb);
    virtual void setOnAudioDataCallback(const imf::OnAudioDataCallback & cb);
    virtual void setOnMetaCallback(const imf::OnMetaCallback & cb);
    virtual void setOnDisconnectedCallback(const imf::OnDisconnectedCallback & cb);
    virtual void setOnConnectionConnectedCallback(const imf::OnConnectionConnectedCallback & cb);
    virtual void setOnExceptionCallback(const imf::OnExceptionCallback & cb);

signals:
    void Start();
    /* Never Use an instance after destroy!!!!!! */
    void Destroy();

private slots:
    void doStart();
    void doDestroy();

private:
    imf::ThreadLoop *threadLoop;
    imf::ISspClient_class *impl;
    std::mutex implLock;
    std::mutex loopLock;
    bool running;
    std::string ip;
    uint32_t bufferSize;

    imf::OnRecvBufferFullCallback bufferFullCallback;
    imf::OnH264DataCallback h264DataCallback;
    imf::OnAudioDataCallback audioDataCallback;
    imf::OnConnectionConnectedCallback connectedCallback;
    imf::OnDisconnectedCallback disconnectedCallback;
    imf::OnMetaCallback metaCallback;
    imf::OnExceptionCallback exceptionCallback;
};

/*
  s->clientLooper = new imf::ThreadLoop(std::bind(ssp_setup_client, _1, s), create_loop_class);
 */


#endif //OBS_SSP_SSP_CLIENT_H
