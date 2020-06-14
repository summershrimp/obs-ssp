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

#ifndef OBS_SSP_VFRAMEQUEUE_H
#define OBS_SSP_VFRAMEQUEUE_H
#include <QQueue>
#include <QAtomicInt>
#include <QSemaphore>
#include <QMutex>
#include <imf/ISspClient.h>
#include "pthread.h"

class VFrameQueue {
    struct Frame {
        imf::SspH264Data data;
        uint64_t time;
        bool noDrop;
    };
    typedef std::function<void(imf::SspH264Data*)> CallbackFunc;
public:
    VFrameQueue();
    void enqueue(imf::SspH264Data, uint64_t time_us, bool noDrop);
    void setFrameTime(uint64_t time_us);
    void setFrameCallback(CallbackFunc);
    void start();
    void stop();
private:
    static void run(VFrameQueue *q);
    static void *pthread_run(void *q);
    CallbackFunc callback;
    QQueue<Frame> frameQueue;
    QSemaphore sem;
    QMutex queueLock;
    pthread_t thread;
    QAtomicInt running;
    uint64_t maxTime;
};


#endif //OBS_SSP_VFRAMEQUEUE_H
