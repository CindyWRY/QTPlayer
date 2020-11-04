#include <QDebug>

#include "audiodecoder.h"

/* Minimum SDL audio buffer size, in samples. */
#define SDL_AUDIO_MIN_BUFFER_SIZE 512
/* Calculate actual buffer size keeping in mind not cause too frequent audio callbacks */
#define SDL_AUDIO_MAX_CALLBACKS_PER_SEC 30

AudioDecoder::AudioDecoder(QObject *parent) :
    QObject(parent),
    m_isStop(false),
    m_isPause(false),
    m_isreadFinished(false),
  //  m_totalTime(0),
    m_audioClock(0),
    m_volume(SDL_MIX_MAXVOLUME),
    m_audioDeviceFormat(AUDIO_F32SYS),
    m_aCovertCtx(NULL),
    m_sendReturn(0)
{

}

int AudioDecoder::openAudio(AVFormatContext *pFormatCtx, int index)
{
    AVCodec *codec;
    SDL_AudioSpec wantedSpec;
    int wantedNbChannels;
    const char *env;

    /*  soundtrack array use to adjust */
    int nextNbChannels[]   = {0, 0, 1, 6, 2, 6, 4, 6};
    int nextSampleRates[]  = {0, 44100, 48000, 96000, 192000};
    int nextSampleRateIdx = FF_ARRAY_ELEMS(nextSampleRates) - 1;

    m_isStop = false;
    m_isPause = false;
    m_isreadFinished = false;

    m_audioSrcFmt = AV_SAMPLE_FMT_NONE;
    m_audioSrcChannelLayout = 0;
    m_audioSrcFreq = 0;

	//discard useless packets like 0 size packets in avi
    pFormatCtx->streams[index]->discard = AVDISCARD_DEFAULT;

    m_audioStream = pFormatCtx->streams[index];
	
	/* Fill the codec context based on the values from the supplied codec
  	parameters. Any allocated fields in codec that have a corresponding field in
  	par are freed and replaced with duplicates of the corresponding field in par.*/
    m_codecCtx = avcodec_alloc_context3(NULL);
    avcodec_parameters_to_context(m_codecCtx, pFormatCtx->streams[index]->codecpar);

    /* find audio decoder */
    if ((codec = avcodec_find_decoder(m_codecCtx->codec_id)) == NULL) {
        avcodec_free_context(&m_codecCtx);
        qDebug() << "Audio decoder not found.";
        return -1;
    }

    /* open audio decoder */
    if (avcodec_open2(m_codecCtx, codec, NULL) < 0) {
        avcodec_free_context(&m_codecCtx);
        qDebug() << "Could not open audio decoder.";
        return -1;
    }

  //  m_totalTime = pFormatCtx->duration;

    env = SDL_getenv("SDL_AUDIO_CHANNELS");
    if (env) {
        qDebug() << "SDL audio channels";
        wantedNbChannels = atoi(env);
		// Return default channel layout for a given number of channels.
        m_audioDstChannelLayout = av_get_default_channel_layout(wantedNbChannels);
    }

    wantedNbChannels = m_codecCtx->channels;
    if (!m_audioDstChannelLayout ||
		// the number of channels in the channel layout
        (wantedNbChannels != av_get_channel_layout_nb_channels(m_audioDstChannelLayout))) {
        //Return default channel layout for a given number of channels.
		m_audioDstChannelLayout = av_get_default_channel_layout(wantedNbChannels);
        m_audioDstChannelLayout &= ~AV_CH_LAYOUT_STEREO_DOWNMIX;
    }

    wantedSpec.channels    = av_get_channel_layout_nb_channels(m_audioDstChannelLayout);
    wantedSpec.freq        = m_codecCtx->sample_rate;
	
    if (wantedSpec.freq <= 0 || wantedSpec.channels <= 0) {
        avcodec_free_context(&m_codecCtx);
        qDebug() << "Invalid sample rate or channel count, freq: " << wantedSpec.freq << " channels: " << wantedSpec.channels;
        return -1;
    }

    while (nextSampleRateIdx && nextSampleRates[nextSampleRateIdx] >= wantedSpec.freq) {
        nextSampleRateIdx--;
    }

    wantedSpec.format      = m_audioDeviceFormat;
    wantedSpec.silence     = 0;
    wantedSpec.samples     = FFMAX(SDL_AUDIO_MIN_BUFFER_SIZE, 2 << av_log2(wantedSpec.freq / SDL_AUDIO_MAX_CALLBACKS_PER_SEC));
    wantedSpec.callback    = &AudioDecoder::audioCallback;
    wantedSpec.userdata    = this;

    /* This function opens the audio device with the desired parameters, placing
     * the actual hardware parameters in the structure pointed to m_spec.
     */ // ������Ƶ�豸��������Ƶ�����̡߳������Ĳ�����wanted_spec��ʵ�ʵõ���Ӳ��������spec
    // 1) SDL�ṩ����ʹ��Ƶ�豸ȡ����Ƶ���ݷ�����
    //    a. push��SDL���ض���Ƶ�ʵ��ûص��������ڻص�������ȡ����Ƶ����
    //    b. pull���û��������ض���Ƶ�ʵ���SDL_QueueAudio()������Ƶ�豸�ṩ���ݡ���������wanted_spec.callback=NULL
  
    while (1) {
        while (SDL_OpenAudio(&wantedSpec, &m_spec) < 0) {
            qDebug() << QString("SDL_OpenAudio (%1 channels, %2 Hz): %3")
                    .arg(wantedSpec.channels).arg(wantedSpec.freq).arg(SDL_GetError());

			wantedSpec.channels = nextNbChannels[FFMIN(7, wantedSpec.channels)];
            if (!wantedSpec.channels) {
                wantedSpec.freq = nextSampleRates[nextSampleRateIdx--];
                wantedSpec.channels = wantedNbChannels;
                if (!wantedSpec.freq) {
                    avcodec_free_context(&m_codecCtx);
                    qDebug() << "No more combinations to try, audio open failed";
                    return -1;
                }
            }
            m_audioDstChannelLayout = av_get_default_channel_layout(wantedSpec.channels);
        }

        if (m_spec.format != m_audioDeviceFormat) {
            qDebug() << "SDL audio format: " << wantedSpec.format << " is not supported"
                     << ", set to advised audio format: " <<  m_spec.format;
            wantedSpec.format = m_spec.format;
            m_audioDeviceFormat = m_spec.format;
            SDL_CloseAudio();
        } else {
            break;
        }
    }

    if (m_spec.channels != wantedSpec.channels) {
        m_audioDstChannelLayout = av_get_default_channel_layout(m_spec.channels);
        if (!m_audioDstChannelLayout) {
            avcodec_free_context(&m_codecCtx);
            qDebug() << "SDL advised channel count " << m_spec.channels << " is not supported!";
            return -1;
        }
    }

    /* set sample format */
    switch (m_audioDeviceFormat) {
    case AUDIO_U8:
        m_audioDstFmt    = AV_SAMPLE_FMT_U8;
        m_audioDepth = 1;
        break;

    case AUDIO_S16SYS:
        m_audioDstFmt    = AV_SAMPLE_FMT_S16;
        m_audioDepth = 2;
        break;

    case AUDIO_S32SYS:
        m_audioDstFmt    = AV_SAMPLE_FMT_S32;
        m_audioDepth = 4;
        break;

    case AUDIO_F32SYS:
        m_audioDstFmt    = AV_SAMPLE_FMT_FLT;
        m_audioDepth = 4;
        break;

    default:
        m_audioDstFmt    = AV_SAMPLE_FMT_S16;
        m_audioDepth = 2;
        break;
    }

    /* open sound */
	
	// 2) ��Ƶ�豸�򿪺󲥷ž������������ص���
	//����SDL_PauseAudio(0)�������ص�����ʼ����������Ƶ
    SDL_PauseAudio(0);

    return 0;
}

void AudioDecoder::closeAudio()
{
    emptyAudioData();

    SDL_LockAudio();
    SDL_CloseAudio();
    SDL_UnlockAudio();

    avcodec_close(m_codecCtx);
    avcodec_free_context(&m_codecCtx);
}

void AudioDecoder::readFileFinished()
{
    m_isreadFinished = true;
}

void AudioDecoder::pauseAudio(bool pause)
{
    m_isPause = pause;
}

void AudioDecoder::stopAudio()
{
    m_isStop = true;
}

void AudioDecoder::audioPacketEnqueue(AVPacket *packet)
{
    m_AudioPacketQueue.basEnqueue(packet);
}

void AudioDecoder::emptyAudioData()
{
    m_audioBuf = nullptr;

    m_audioBufIndex = 0;
    m_audioBufSize = 0;
    m_audioBufSize1 = 0;

    m_audioClock = 0;

    m_sendReturn = 0;

    m_AudioPacketQueue.empty();
}

int AudioDecoder::getVolume()
{
    return m_volume;
}

void AudioDecoder::setVolume(int volume)
{
    this->m_volume = volume;
}

double AudioDecoder::getAudioClock()
{
    if (m_codecCtx) {
        /* control audio pts according to audio buffer data size */
        int hwBufSize   = m_audioBufSize - m_audioBufIndex;
        int bytesPerSec = m_codecCtx->sample_rate * m_codecCtx->channels * m_audioDepth;

        m_audioClock -= static_cast<double>(hwBufSize) / bytesPerSec;
    }

    return m_audioClock;
}
/*�ص���������SDL��������������,*/
void AudioDecoder::audioCallback(void *userdata, quint8 *stream, int SDL_AudioBufSize)
{
    AudioDecoder *decoder = (AudioDecoder *)userdata;

    int decodedSize;
    /* SDL_BufSize means audio play buffer left size
     * while it greater than 0, means counld fill data to it
     */
    while (SDL_AudioBufSize > 0) {
        if (decoder->m_isStop) {
            return ;
        }

        if (decoder->m_isPause) {
            SDL_Delay(10);
            continue;
        }

        /* no data in buffer */
        if (decoder->m_audioBufIndex >= decoder->m_audioBufSize) {

            decodedSize = decoder->decodeAudio();
            /* if error, just output silence */
            if (decodedSize < 0) {
                /* if not decoded data, just output silence */
                decoder->m_audioBufSize = 1024;
                decoder->m_audioBuf = nullptr;
            } else {
                decoder->m_audioBufSize = decodedSize;
            }
            decoder->m_audioBufIndex = 0;
        }

        /* calculate number of data that haven't play */
        int left = decoder->m_audioBufSize - decoder->m_audioBufIndex;
        if (left > SDL_AudioBufSize) {
            left = SDL_AudioBufSize;
        }

        if (decoder->m_audioBuf) {
            memset(stream, 0, left);
            SDL_MixAudio(stream, decoder->m_audioBuf + decoder->m_audioBufIndex, left, decoder->m_volume);
        }

        SDL_AudioBufSize -= left;
        stream += left;
        decoder->m_audioBufIndex += left;
    }
}

int AudioDecoder::decodeAudio()
{
    int ret;
    AVFrame *frame = av_frame_alloc();
    int resampledDataSize;

    if (!frame) {
        qDebug() << "Decode audio frame alloc failed.";
        return -1;
    }

    if (m_isStop) {
        return -1;
    }

    if (m_AudioPacketQueue.queueSize() <= 0) {
        if (m_isreadFinished) {
            m_isStop = true;
            SDL_Delay(100);
            emit playFinished();
        }
        return -1;
    }

    /* get new packet whiel last packet all has been resolved */
    if (m_sendReturn != AVERROR(EAGAIN)) {
        m_AudioPacketQueue.basDequeue(&m_AudioPacket, true);
    }

    if (!strcmp((char*)m_AudioPacket.data, "FLUSH")) {
        avcodec_flush_buffers(m_codecCtx);
        av_packet_unref(&m_AudioPacket);
        av_frame_free(&frame);
        m_sendReturn = 0;
        qDebug() << "seek audio";
        return -1;
    }

    /* while return -11 means packet have data not resolved,
     * this packet cannot be unref
     */
    m_sendReturn = avcodec_send_packet(m_codecCtx, &m_AudioPacket);
    if ((m_sendReturn < 0) && (m_sendReturn != AVERROR(EAGAIN)) && (m_sendReturn != AVERROR_EOF)) {
        av_packet_unref(&m_AudioPacket);
        av_frame_free(&frame);
        qDebug() << "Audio send to decoder failed, error code: " << m_sendReturn;
        return m_sendReturn;
    }

    ret = avcodec_receive_frame(m_codecCtx, frame);
    if ((ret < 0) && (ret != AVERROR(EAGAIN))) {
        av_packet_unref(&m_AudioPacket);
        av_frame_free(&frame);
        qDebug() << "Audio frame decode failed, error code: " << ret;
        return ret;
    }

    if (frame->pts != AV_NOPTS_VALUE) {
        m_audioClock = av_q2d(m_audioStream->time_base) * frame->pts;
//        qDebug() << "no pts";
    }

    /* get audio channels */
    qint64 inChannelLayout = (frame->channel_layout && frame->channels == av_get_channel_layout_nb_channels(frame->channel_layout)) ?
                frame->channel_layout : av_get_default_channel_layout(frame->channels);

    if (frame->format       != m_audioSrcFmt              ||
        inChannelLayout     != m_audioSrcChannelLayout    ||
        frame->sample_rate  != m_audioSrcFreq             ||
        !m_aCovertCtx) {
        if (m_aCovertCtx) {
            swr_free(&m_aCovertCtx);
        }

        /* init swr audio convert context */
        m_aCovertCtx = swr_alloc_set_opts(nullptr, m_audioDstChannelLayout, m_audioDstFmt, m_spec.freq,
                inChannelLayout, (AVSampleFormat)frame->format , frame->sample_rate, 0, NULL);
        if (!m_aCovertCtx || (swr_init(m_aCovertCtx) < 0)) {
            av_packet_unref(&m_AudioPacket);
            av_frame_free(&frame);
            return -1;
        }

        m_audioSrcFmt             = (AVSampleFormat)frame->format;
        m_audioSrcChannelLayout   = inChannelLayout;
        m_audioSrcFreq            = frame->sample_rate;
        m_audioSrcChannels        = frame->channels;
    }

    if (m_aCovertCtx) {
        const quint8 **in   = (const quint8 **)frame->extended_data;
        uint8_t *out[] = {audioBuf1};

        int outCount = sizeof(audioBuf1) / m_spec.channels / av_get_bytes_per_sample(m_audioDstFmt);

        int sampleSize = swr_convert(m_aCovertCtx, out, outCount, in, frame->nb_samples);
        if (sampleSize < 0) {
            qDebug() << "swr convert failed";
            av_packet_unref(&m_AudioPacket);
            av_frame_free(&frame);
            return -1;
        }

        if (sampleSize == outCount) {
            qDebug() << "audio buffer is probably too small";
            if (swr_init(m_aCovertCtx) < 0) {
                swr_free(&m_aCovertCtx);
            }
        }

        m_audioBuf = audioBuf1;
        resampledDataSize = sampleSize * m_spec.channels * av_get_bytes_per_sample(m_audioDstFmt);
    } else {
        m_audioBuf = frame->data[0];
        resampledDataSize = av_samples_get_buffer_size(NULL, frame->channels, frame->nb_samples, static_cast<AVSampleFormat>(frame->format), 1);
    }

    m_audioClock += static_cast<double>(resampledDataSize) / (m_audioDepth * m_codecCtx->channels * m_codecCtx->sample_rate);

    if (m_sendReturn != AVERROR(EAGAIN)) {
        av_packet_unref(&m_AudioPacket);
    }

    av_frame_free(&frame);

    return resampledDataSize;
}
