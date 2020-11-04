#ifndef AUDIODECODER_H
#define AUDIODECODER_H

#include <QObject>

extern "C"
{
#include "libswresample/swresample.h"
}

#include "basicavpacketqueue.h"



class AudioDecoder : public QObject
{
    Q_OBJECT
public:
    explicit AudioDecoder(QObject *parent = nullptr);

    int openAudio(AVFormatContext *pFormatCtx, int index);
    void closeAudio();
    void pauseAudio(bool pause);
    void stopAudio();
    int getVolume();
    void setVolume(int volume);
    double getAudioClock();
    void audioPacketEnqueue(AVPacket *packet);
    void emptyAudioData();
  //  void setTotalTime(qint64 time);

private:
    int decodeAudio();
    static void audioCallback(void *userdata, quint8 *stream, int SDL_AudioBufSize);

    bool m_isStop;
    bool m_isPause;
    bool m_isreadFinished;

   // qint64 m_totalTime;
    double m_audioClock;
    int m_volume;

    AVStream *m_audioStream;

    quint8 *m_audioBuf;
    quint32 m_audioBufSize;
    DECLARE_ALIGNED(16, quint8, audioBuf1) [192000];
    quint32 m_audioBufSize1;
    quint32 m_audioBufIndex;

    SDL_AudioSpec m_spec;

    quint32 m_audioDeviceFormat;  // audio device sample format
    quint8 m_audioDepth;
    struct SwrContext *m_aCovertCtx;//ÒôÆµÖØ²ÉÑù
    qint64 m_audioDstChannelLayout;
    enum AVSampleFormat m_audioDstFmt;   // audio decode sample format ï¿½ï¿½Æµï¿½Ä²ï¿½ï¿½ï¿½ï¿½ï¿½Ê½ï¿½ï¿½

    qint64 m_audioSrcChannelLayout;
    int m_audioSrcChannels;
    enum AVSampleFormat m_audioSrcFmt;
    int m_audioSrcFreq;

    AVCodecContext *m_codecCtx;          // audio codec context

    BasicAvPacketQueue m_AudioPacketQueue;

    AVPacket m_AudioPacket;

    int m_sendReturn;

signals:
    void playFinished();

public slots:
    void readFileFinished();

};

#endif // AUDIODECODER_H
