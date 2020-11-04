#ifndef DECODER_H
#define DECODER_H

#include <QThread>
#include <QImage>

extern "C"
{
#include "libavfilter/avfiltergraph.h"
#include "libavfilter/buffersink.h"
#include "libavfilter/buffersrc.h"
#include "libswscale/swscale.h"
#include "libavdevice/avdevice.h"
#include "libavutil/pixfmt.h"
#include "libavutil/opt.h"
#include "libavcodec/avfft.h"
#include "libavutil/imgutils.h"
}

#include "audiodecoder.h"

class Decoder : public QThread
{
    Q_OBJECT

public:
    enum PlayState {
        STOP,
        PAUSE,
        PLAYING,
        FINISH
    };

    explicit Decoder();
    ~Decoder();

    double getCurrentTime();
    void decoderSeekProgress(qint64 pos);
    int getVolume();
    void setVolume(int volume);

private:
    void run();
    void clearData();
    void setPlayState(Decoder::PlayState state);
    void displayVideo(QImage image);
    static int videoThread(void *arg);
    double synchronize(AVFrame *frame, double pts);
    bool isRealtime(AVFormatContext *m_pFormatCtx);
    int initFilter();

    int m_fileType;

    int m_videoIndex;
    int m_audioIndex;
    int m_subtitleIndex;

    QString m_currentFile;//file name
    QString m_currentType;//file type 

    qint64 m_timeTotal;

    AVPacket m_seekPacket;
    qint64 m_seekPos;
    double m_seekTime;

    PlayState m_playState;
    bool m_isStop;
    bool m_gotStop;
    bool m_isPause;
    bool m_isSeek;
    bool m_isReadFinished;
    bool m_isDecodeFinished;

    AVFormatContext *m_pFormatCtx;

    AVCodecContext *m_pCodecCtx;          // video codec context

    BasicAvPacketQueue m_videoQueue;
    BasicAvPacketQueue m_subtitleQueue;

    AVStream *m_videoStream;//ÊÓÆµÁ÷

    double m_videoClk;    // video frame timestamp

    AudioDecoder *m_audioDecoder;

    AVFilterGraph   *m_filterGraph;
    AVFilterContext *m_filterSinkCxt;
    AVFilterContext *m_filterSrcCxt;

public slots:
    void decoderFile(QString file, QString type);
    void stopVideo();
    void pauseVideo();
    void audioFinished();

signals:
    void readFinished();
    void gotVideo(QImage image);
    void gotVideoTime(qint64 time);
    void playStateChanged(Decoder::PlayState state);

};

#endif // DECODER_H
