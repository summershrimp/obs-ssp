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

#include <obs.h>

#include "obs-ssp.h"
#include "ssp-client.h"

SSPClient::SSPClient(const std::string &ip, uint32_t bufferSize) {
    this->ip = ip;
    this->bufferSize = bufferSize;
    this->threadLoop = nullptr;
    this->impl = nullptr;
    this->running = false;
    connect(this, SIGNAL(Start()), this, SLOT(doStart()));
    connect(this, SIGNAL(Destroy()), this, SLOT(doDestroy()));
}
using namespace std::placeholders;

void SSPClient::doStart() {
    loopLock.lock();
    if(this->threadLoop) {
        loopLock.unlock();
        return;
    }
    this->threadLoop = new imf::ThreadLoop(std::bind(SSPClient::PreStart, this, _1), create_loop_class);
    this->threadLoop->start();
    loopLock.unlock();
}

void SSPClient::doDestroy() {
    implLock.lock();
    if(impl) {
        impl->stop();
        impl->destroy();
        impl = nullptr;
    }
    implLock.unlock();

    loopLock.lock();
    if(threadLoop){
        threadLoop->stop();
        delete threadLoop;
        threadLoop = nullptr;
    }
    loopLock.unlock();
    delete this;
}

void SSPClient::PreStart(SSPClient *my, imf::Loop *loop) {
    if(!my || ! loop) {
        return;
    }
    my->implLock.lock();
    my->impl = create_ssp_class(my->ip, loop, my->bufferSize, 9999, imf::STREAM_DEFAULT);
    my->impl->init();
    if(my->bufferFullCallback) {
        my->impl->setOnRecvBufferFullCallback(my->bufferFullCallback);
    }
    if(my->exceptionCallback) {
        my->impl->setOnExceptionCallback(my->exceptionCallback);
    }
    if(my->h264DataCallback) {
        my->impl->setOnH264DataCallback(my->h264DataCallback);
    }
    if(my->connectedCallback) {
        my->impl->setOnConnectionConnectedCallback(my->connectedCallback);
    }
    if(my->disconnectedCallback) {
        my->impl->setOnDisconnectedCallback(my->disconnectedCallback);
    }
    if(my->metaCallback) {
        my->impl->setOnMetaCallback(my->metaCallback);
    }
    if(my->audioDataCallback) {
        my->impl->setOnAudioDataCallback(my->audioDataCallback);
    }
    my->impl->start();
    my->implLock.unlock();
}

void SSPClient::setOnRecvBufferFullCallback(const imf::OnRecvBufferFullCallback &cb) {
    this->bufferFullCallback = cb;
}

void SSPClient::setOnAudioDataCallback(const imf::OnAudioDataCallback &cb) {
    this->audioDataCallback = cb;
}

void SSPClient::setOnMetaCallback(const imf::OnMetaCallback &cb) {
    this->metaCallback = cb;
}

void SSPClient::setOnDisconnectedCallback(const imf::OnDisconnectedCallback &cb) {
    this->disconnectedCallback = cb;
}

void SSPClient::setOnConnectionConnectedCallback(const imf::OnConnectionConnectedCallback &cb) {
    this->connectedCallback = cb;
}

void SSPClient::setOnH264DataCallback(const imf::OnH264DataCallback &cb) {
    this->h264DataCallback = cb;
}

void SSPClient::setOnExceptionCallback(const imf::OnExceptionCallback &cb) {
    this->exceptionCallback = cb;
}
