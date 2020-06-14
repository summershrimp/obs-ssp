//
// Created by Yibai Zhang on 2020/6/14.
//

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
