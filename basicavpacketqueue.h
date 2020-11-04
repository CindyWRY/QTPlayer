#ifndef AVPACKETQUEUE_H
#define AVPACKETQUEUE_H

#include <QQueue>

extern "C"
{
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
}

#include "SDL2/SDL.h"

class BasicAvPacketQueue
{
public:
    explicit BasicAvPacketQueue();

    void basEnqueue(AVPacket *packet);

    void basDequeue(AVPacket *packet, bool isBlock);

    bool isEmpty();

    void empty();

    int queueSize();

private:
    SDL_mutex *m_mutex;
    SDL_cond *m_cond;

    QQueue<AVPacket> m_queue;
};

#endif // AVPACKETQUEUE_H
