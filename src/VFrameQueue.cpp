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

#include <util/platform.h>
#include "VFrameQueue.h"
#include <QDebug>

VFrameQueue::VFrameQueue() {
    maxTime = 0;
}


void VFrameQueue::start() {
    running = true;
    pthread_create(&thread, nullptr, pthread_run, (void *)this);
}


void VFrameQueue::stop() {
    running = false;
    sem.release();
    pthread_join(thread, nullptr);
}


void VFrameQueue::setFrameCallback(VFrameQueue::CallbackFunc cb) {
    callback = std::move(cb);
}


void VFrameQueue::setFrameTime(uint64_t time_us) {
    maxTime = time_us;
}


void VFrameQueue::enqueue(imf::SspH264Data data, uint64_t time_us, bool noDrop) {
    QMutexLocker locker(&queueLock);
    uint8_t *copy_data = (uint8_t *)malloc(data.len);
    memcpy(copy_data, data.data, data.len);
    data.data = copy_data;
    frameQueue.enqueue({data, time_us, noDrop});
    sem.release();
}

void* VFrameQueue::pthread_run(void *q) {
    run((VFrameQueue *)q);
    return nullptr;
}


void VFrameQueue::run(VFrameQueue *q) {
    Frame current;
    uint64_t lastFrameTime = 0, lastStartTime = 0, processingTime = 0;
    q->sem.acquire();
    q->queueLock.lock();
    current = q->frameQueue.dequeue();
    q->queueLock.unlock();
    lastStartTime = os_gettime_ns()/1000;
    q->callback(&current.data);
    lastFrameTime = current.time;
    processingTime = os_gettime_ns()/1000 - lastStartTime;
    free((void *)current.data.data);
    while(q->running){
        q->sem.acquire();
        q->queueLock.lock();
        if(q->frameQueue.empty()) {
            q->queueLock.unlock();
            continue;
        }
        current = q->frameQueue.dequeue();
        q->queueLock.unlock();
        if(current.time < lastFrameTime){
            free((void *)current.data.data);
            continue;
        }
        if(current.noDrop){
            lastStartTime = os_gettime_ns()/1000;
            q->callback(&current.data);
            lastFrameTime = current.time;
            processingTime = os_gettime_ns()/1000 - lastStartTime;
        }else if(current.time - lastFrameTime + 15000 > processingTime){
            lastStartTime = os_gettime_ns()/1000;
            q->callback(&current.data);
            lastFrameTime = current.time;
            processingTime = os_gettime_ns()/1000 - lastStartTime;
        } else {
            qDebug() <<"dropped" << current.time - lastFrameTime << processingTime;
        } // else we drop the frame
        free((void *)current.data.data);
    }
}



