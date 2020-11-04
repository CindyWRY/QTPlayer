#include "basicavpacketqueue.h"

BasicAvPacketQueue::BasicAvPacketQueue()
{
    m_mutex   = SDL_CreateMutex();
    m_cond    = SDL_CreateCond();
}

void BasicAvPacketQueue::basEnqueue(AVPacket *packet)
{
    SDL_LockMutex(m_mutex);

    m_queue.enqueue(*packet);

    SDL_CondSignal(m_cond);
    SDL_UnlockMutex(m_mutex);
}

void BasicAvPacketQueue::basDequeue(AVPacket *packet, bool isBlock)
{
    SDL_LockMutex(m_mutex);
    while (1) {
        if (!m_queue.isEmpty()) {
            *packet = m_queue.dequeue();
            break;
        } else if (!isBlock) {
            break;
        } else {
            SDL_CondWait(m_cond, m_mutex);
        }
    }
    SDL_UnlockMutex(m_mutex);
}

void BasicAvPacketQueue::empty()
{
    SDL_LockMutex(m_mutex);
    while (m_queue.size() > 0) {
        AVPacket packet = m_queue.dequeue();
        av_packet_unref(&packet);
    }

    SDL_UnlockMutex(m_mutex);
}

bool BasicAvPacketQueue::isEmpty()
{
    return m_queue.isEmpty();
}

int BasicAvPacketQueue::queueSize()
{
    return m_queue.size();
}
