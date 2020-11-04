#include <QDebug>

#include "decoder.h"

DecoderThread::DecoderThread() :
    m_timeTotal(0),
    m_playState(STOP),
    m_isStop(false),
    m_isPause(false),
    m_isSeek(false),
    m_isReadFinished(false),
    m_audioDecoder(new AudioDecoder),
    m_filterGraph(NULL)
{
    av_init_packet(&m_seekPacket);
    m_seekPacket.data = (uint8_t *)"FLUSH";

    connect(m_audioDecoder, SIGNAL(playFinished()), this, SLOT(audioFinished()));
    connect(this, SIGNAL(readFinished()), m_audioDecoder, SLOT(readFileFinished()));
}

DecoderThread::~DecoderThread()
{

}

void DecoderThread::displayVideo(QImage image)
{
    emit gotVideo(image);
}

void DecoderThread::clearData()
{
    m_videoIndex = -1,
    m_audioIndex = -1,
    m_subtitleIndex = -1,

    m_timeTotal = 0;

    m_isStop  = false;
    m_isPause = false;
    m_isSeek  = false;
    m_isReadFinished      = false;
    m_isDecodeFinished    = false;

    m_videoQueue.empty();

    m_audioDecoder->emptyAudioData();

    m_videoClk = 0;
}

void DecoderThread::setPlayState(DecoderThread::PlayState state)
{
//    qDebug() << "Set state: " << state;
    emit playStateChanged(state);
    m_playState = state;
}

bool DecoderThread::isRealtime(AVFormatContext *pFormatCtx)
{
    if (!strcmp(pFormatCtx->iformat->name, "rtp")
        || !strcmp(pFormatCtx->iformat->name, "rtsp")
        || !strcmp(pFormatCtx->iformat->name, "sdp")) {
         return true;
    }

    if(pFormatCtx->pb && (!strncmp(pFormatCtx->filename, "rtp:", 4)
        || !strncmp(pFormatCtx->filename, "udp:", 4)
        )) {
        return true;
    }

    return false;
}

int DecoderThread::initFilter()
{
    int ret;

    AVFilterInOut *out = avfilter_inout_alloc();
    AVFilterInOut *in = avfilter_inout_alloc();
    /* output format */
    enum AVPixelFormat pixFmts[] = {AV_PIX_FMT_RGB32, AV_PIX_FMT_NONE};

    /* free last graph */
    if (m_filterGraph) {
        avfilter_graph_free(&m_filterGraph);
    }

    m_filterGraph = avfilter_graph_alloc();

    /* just add filter ouptut format rgb32,
     * use for function avfilter_graph_parse_ptr()
     */
    QString filter("pp=hb/vb/dr/al");

    QString args = QString("video_size=%1x%2:pix_fmt=%3:time_base=%4/%5:pixel_aspect=%6/%7")
            .arg(m_pCodecCtx->width).arg(m_pCodecCtx->height).arg(m_pCodecCtx->pix_fmt)
            .arg(m_videoStream->time_base.num).arg(m_videoStream->time_base.den)
            .arg(m_pCodecCtx->sample_aspect_ratio.num).arg(m_pCodecCtx->sample_aspect_ratio.den);

    /* create source filter */
    ret = avfilter_graph_create_filter(&m_filterSrcCxt, avfilter_get_by_name("buffer"), "in", args.toLocal8Bit().data(), NULL, m_filterGraph);
    if (ret < 0) {
        qDebug() << "avfilter graph create filter failed, ret:" << ret;
        avfilter_graph_free(&m_filterGraph);
        goto out;
    }

    /* create sink filter */
    ret = avfilter_graph_create_filter(&m_filterSinkCxt, avfilter_get_by_name("buffersink"), "out", NULL, NULL, m_filterGraph);
    if (ret < 0) {
        qDebug() << "avfilter graph create filter failed, ret:" << ret;
        avfilter_graph_free(&m_filterGraph);
        goto out;
    }

    /* set sink filter ouput format */
    ret = av_opt_set_int_list(m_filterSinkCxt, "pix_fmts", pixFmts, AV_PIX_FMT_NONE, AV_OPT_SEARCH_CHILDREN);
    if (ret < 0) {
        qDebug() << "av opt set int list failed, ret:" << ret;
        avfilter_graph_free(&m_filterGraph);
        goto out;
    }

    out->name       = av_strdup("in");
    out->filter_ctx = m_filterSrcCxt;
    out->pad_idx    = 0;
    out->next       = NULL;

    in->name       = av_strdup("out");
    in->filter_ctx = m_filterSinkCxt;
    in->pad_idx    = 0;
    in->next       = NULL;

    if (filter.isEmpty() || filter.isNull()) {
        /* if no filter to add, just link source & sink */
        ret = avfilter_link(m_filterSrcCxt, 0, m_filterSinkCxt, 0);
        if (ret < 0) {
            qDebug() << "avfilter link failed, ret:" << ret;
            avfilter_graph_free(&m_filterGraph);
            goto out;
        }
    } else {
        /* add filter to graph */
        ret = avfilter_graph_parse_ptr(m_filterGraph, filter.toLatin1().data(), &in, &out, NULL);
        if (ret < 0) {
            qDebug() << "avfilter graph parse ptr failed, ret:" << ret;
            avfilter_graph_free(&m_filterGraph);
            goto out;
        }
    }

    /* check validity and configure all the links and formats in the graph */
    if ((ret = avfilter_graph_config(m_filterGraph, NULL)) < 0) {
        qDebug() << "avfilter graph config failed, ret:" << ret;
        avfilter_graph_free(&m_filterGraph);
    }

out:
    avfilter_inout_free(&out);
    avfilter_inout_free(&in);

    return ret;
}

void DecoderThread::decoderFile(QString file, QString type)
{
//    qDebug() << "Current state:" << playState;
    qDebug() << "File name:" << file << ", type:" << type;
    if (m_playState != STOP) {
        m_isStop = true;
        while (m_playState != STOP) {
            SDL_Delay(10);
        }
        SDL_Delay(100);
    }

    clearData();

    SDL_Delay(100);

    m_currentFile = file;
    m_currentType = type;

    this->start();
}

void DecoderThread::audioFinished()
{
    m_isStop = true;
    if (m_currentType == "music") {
        SDL_Delay(100);
        emit playStateChanged(DecoderThread::FINISH);
    }
}

void DecoderThread::stopVideo()
{
    if (m_playState == STOP) {
        setPlayState(DecoderThread::STOP);
        return;
    }

    m_gotStop = true;
    m_isStop  = true;
    m_audioDecoder->stopAudio();

    if (m_currentType == "video") {
        /* wait for decoding & reading stop */
        while (!m_isReadFinished || !m_isDecodeFinished) {
            SDL_Delay(10);
        }
    } else {
        while (!m_isReadFinished) {
            SDL_Delay(10);
        }
    }
}

void DecoderThread::pauseVideo()
{
    if (m_playState == STOP) {
        return;
    }

    m_isPause = !m_isPause;
    m_audioDecoder->pauseAudio(m_isPause);
    if (m_isPause) {
        av_read_pause(m_pFormatCtx);
        setPlayState(PAUSE);
    } else {
        av_read_play(m_pFormatCtx);
        setPlayState(PLAYING);
    }
}

int DecoderThread::getVolume()
{
    return m_audioDecoder->getVolume();
}

void DecoderThread::setVolume(int volume)
{
    m_audioDecoder->setVolume(volume);
}

double DecoderThread::getCurrentTime()
{
    if (m_audioIndex >= 0) {
        return m_audioDecoder->getAudioClock();
    }

    return 0;
}

void DecoderThread::decoderSeekProgress(qint64 pos)
{
    if (!m_isSeek) {
        m_seekPos = pos;
        m_isSeek = true;
    }
}

double DecoderThread::synchronize(AVFrame *frame, double pts)
{
    double delay;

    if (pts != 0) {
        m_videoClk = pts; // Get pts,then set video clock to it
    } else {
        pts = m_videoClk; // Don't get pts,set it to video clock
    }
	

   delay = av_q2d(m_pCodecCtx->time_base);
     //int fps = av_q2d(av_inv_q(m_pCodecCtx->time_base));
	//printf("DecoderThread::synchronize delay:%d\n", delay);
	//printf("codecCtx->time_base: %d\n", av_inv_q(m_pCodecCtx->time_base));
	//delay = repeat_pict / (2*fps)= repeat_pict *0.5*delay
	delay += frame->repeat_pict * (delay * 0.5);
    //delay = frame->repeat_pict/(2*fps);

    m_videoClk += delay;

    return pts;
}
//从队列中拿出视频来显示
int DecoderThread::videoThread(void *arg)
{
    int ret;
    double pts;
    AVPacket packet;
    DecoderThread *decoder = (DecoderThread *)arg;
    AVFrame *pFrame  = av_frame_alloc();

    while (true) {
        if (decoder->m_isStop) {
            break;
        }

        if (decoder->m_isPause) {
            SDL_Delay(10);
            continue;
        }

        if (decoder->m_videoQueue.queueSize() <= 0) {
            /* while video file read finished exit decode thread,
             * otherwise just delay for data input
             */
            if (decoder->m_isReadFinished) {
                break;
            }
            SDL_Delay(1);
            continue;
        }

        decoder->m_videoQueue.basDequeue(&packet, true);

        /* flush codec buffer while received flush packet */
        if (!strcmp((char *)packet.data, "FLUSH")) {//是flush 包
            qDebug() << "Seek video";
            avcodec_flush_buffers(decoder->m_pCodecCtx);
            av_packet_unref(&packet);
            continue;
        }

        ret = avcodec_send_packet(decoder->m_pCodecCtx, &packet);
        if ((ret < 0) && (ret != AVERROR(EAGAIN)) && (ret != AVERROR_EOF)) {
            qDebug() << "Video send to decoder failed, error code: " << ret;
            av_packet_unref(&packet);
            continue;
        }

        ret = avcodec_receive_frame(decoder->m_pCodecCtx, pFrame);
        if ((ret < 0) && (ret != AVERROR_EOF)) {
            qDebug() << "Video frame decode failed, error code: " << ret;
            av_packet_unref(&packet);
            continue;
        }

        if ((pts = pFrame->pts) == AV_NOPTS_VALUE) {
            pts = 0;
        }

        pts *= av_q2d(decoder->m_videoStream->time_base);
        pts =  decoder->synchronize(pFrame, pts);

        if (decoder->m_audioIndex >= 0) {
            while (1) {
                if (decoder->m_isStop) {
                    break;
                }

                double audioClk = decoder->m_audioDecoder->getAudioClock();
                pts = decoder->m_videoClk;

                if (pts <= audioClk) {
                     break;
                }
                int delayTime = (pts - audioClk) * 1000;

                delayTime = delayTime > 5 ? 5 : delayTime;

                SDL_Delay(delayTime);
            }
        }

        if (av_buffersrc_add_frame(decoder->m_filterSrcCxt, pFrame) < 0) {
            qDebug() << "av buffersrc add frame failed.";
            av_packet_unref(&packet);
            continue;
        }

        if (av_buffersink_get_frame(decoder->m_filterSinkCxt, pFrame) < 0) {
            qDebug() << "av buffersrc get frame failed.";
            av_packet_unref(&packet);
            continue;
        } else {
            QImage tmpImage(pFrame->data[0], decoder->m_pCodecCtx->width, decoder->m_pCodecCtx->height, QImage::Format_RGB32);
            /* deep copy, otherwise when tmpImage data change, this image cannot display */
            QImage image = tmpImage.copy();
            decoder->displayVideo(image);
        }

        av_frame_unref(pFrame);
        av_packet_unref(&packet);
    }

    av_frame_free(&pFrame);

    if (!decoder->m_isStop) {
        decoder->m_isStop = true;
    }

    qDebug() << "Video decoder finished.";

    SDL_Delay(100);

    decoder->m_isDecodeFinished = true;

    if (decoder->m_gotStop) {
        decoder->setPlayState(DecoderThread::STOP);
    } else {
        decoder->setPlayState(DecoderThread::FINISH);
    }

    return 0;
}





void DecoderThread::run()
{
    AVCodec *pCodec;

    AVPacket pkt, *packet = &pkt;        // packet use in decoding

    int seekIndex;  
    bool realTime;
	
    m_pFormatCtx = avformat_alloc_context();
	//Open an input stream and read the header. The codecs are not opened.
    if (avformat_open_input(&m_pFormatCtx, m_currentFile.toLocal8Bit().data(), NULL, NULL) != 0) {
        qDebug() << "Open file failed.";
        return ;
    }

    if (avformat_find_stream_info(m_pFormatCtx, NULL) < 0) {
        qDebug() << "Could't find stream infomation.";
        avformat_free_context(m_pFormatCtx);
        return;
    }

    realTime = isRealtime(m_pFormatCtx);

//    av_dump_format(pFormatCtx, 0, 0, 0);  // just use in debug output

    /* find video & audio stream index */
    for (unsigned int i = 0; i < m_pFormatCtx->nb_streams; i++) {
        if (m_pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            m_videoIndex = i;
            qDebug() << "Find video stream.";
        }

        if (m_pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            m_audioIndex = i;
            qDebug() << "Find audio stream.";
        }

        if (m_pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE) {
            m_subtitleIndex = i;
            qDebug() << "Find subtitle stream.";
        }
    }

    if (m_currentType == "video") {
        if (m_videoIndex < 0) {
            qDebug() << "Not support this video file, videoIndex: " << m_videoIndex << ", audioIndex: " << m_audioIndex;
            avformat_free_context(m_pFormatCtx);
            return;
        }
    } else {
        if (m_audioIndex < 0) {
            qDebug() << "Not support this audio file.";
            avformat_free_context(m_pFormatCtx);
            return;
        }
    }

    if (!realTime) {
        emit gotVideoTime(m_pFormatCtx->duration);
        m_timeTotal = m_pFormatCtx->duration;//Duration of the stream, in AV_TIME_BASE fractional
    } else {
        emit gotVideoTime(0);//更新进度条显示的时间
    }
	//音视频有不一样的解码器
	//open audio 
    if (m_audioIndex >= 0) {
        if (m_audioDecoder->openAudio(m_pFormatCtx, m_audioIndex) < 0) {
            avformat_free_context(m_pFormatCtx);
            return;
        }
    }

	 //opencv video 
    if (m_currentType == "video") {
        /* find video decoder */
        m_pCodecCtx = avcodec_alloc_context3(NULL);

		//Fill the codec context based on the values from the supplied codec parameters.
        avcodec_parameters_to_context(m_pCodecCtx, m_pFormatCtx->streams[m_videoIndex]->codecpar);

        /* find video decoder */
        if ((pCodec = avcodec_find_decoder(m_pCodecCtx->codec_id)) == NULL) {
            qDebug() << "Video decoder not found.";
            goto fail;
        }

        if (avcodec_open2(m_pCodecCtx, pCodec, NULL) < 0) {
            qDebug() << "Could not open video decoder.";
            goto fail;
        }

        m_videoStream = m_pFormatCtx->streams[m_videoIndex];

        if (initFilter() < 0) {
            goto fail;
        }

        SDL_CreateThread(&DecoderThread::videoThread, "video_thread", this);
    }

    setPlayState(DecoderThread::PLAYING);

    while (true) {
        if (m_isStop) {
            break;
        }

        /* do not read next frame & delay to release cpu utilization */
        if (m_isPause) {
            SDL_Delay(10);
            continue;
        }

/* this seek just use in playing music, while read finished
 * & have out of loop, then jump back to seek position
 */
seek:
        if (m_isSeek) {
            if (m_currentType == "video") {
                seekIndex = m_videoIndex;
            } else {
                seekIndex = m_audioIndex;
            }

            AVRational aVRational = av_get_time_base_q();
			int internal_time_base = av_q2d(av_inv_q(aVRational));
			printf("internal time base:%d\n", internal_time_base);
            m_seekPos = av_rescale_q(m_seekPos, aVRational, m_pFormatCtx->streams[seekIndex]->time_base);

            if (av_seek_frame(m_pFormatCtx, seekIndex, m_seekPos, AVSEEK_FLAG_BACKWARD) < 0) {
                qDebug() << "Seek failed.";

            } else {
                m_audioDecoder->emptyAudioData();
                m_audioDecoder->audioPacketEnqueue(&m_seekPacket);//插入查找包

                if (m_currentType == "video") {
                    m_videoQueue.empty();
                    m_videoQueue.basEnqueue(&m_seekPacket);//插入查找包
                    m_videoClk = 0;//时钟清零
                }
            }

            m_isSeek = false;
        }

        if (m_currentType == "video") {
            if (m_videoQueue.queueSize() > 512) {
                SDL_Delay(10);
                continue;
            }
        }

        /* judge haven't read all frame */
		//Return the next frame of a stream.
 // This function returns what is stored in the file, and does not validate
 // that what is there are valid frames for the decoder.
 //return 0 if OK, < 0 on error or end of file
        if (av_read_frame(m_pFormatCtx, packet) < 0){
            qDebug() << "Read file completed.";
            m_isReadFinished = true;
            emit readFinished();
            SDL_Delay(10);
            break;
        }

        if (packet->stream_index == m_videoIndex && m_currentType == "video") {
            m_videoQueue.basEnqueue(packet); // video stream
        } else if (packet->stream_index == m_audioIndex) {
            m_audioDecoder->audioPacketEnqueue(packet); // audio stream
        } else if (packet->stream_index == m_subtitleIndex) {
            //m_subtitleQueue.enqueue(packet);
            av_packet_unref(packet);    // subtitle stream
        } else {
            av_packet_unref(packet);
        }
    }

//    qDebug() << isStop;
    while (!m_isStop) {
        /* just use at audio playing */
        if (m_isSeek) {
            goto seek;
        }

        SDL_Delay(100);
    }

fail:
    /* close audio device */
    if (m_audioIndex >= 0) {
        m_audioDecoder->closeAudio();
    }

    if (m_currentType == "video") {
        avcodec_close(m_pCodecCtx);
        avcodec_free_context(&m_pCodecCtx);
    }

    avformat_close_input(&m_pFormatCtx);
    avformat_free_context(m_pFormatCtx);

    m_isReadFinished = true;

    if (m_currentType == "music") {
        setPlayState(DecoderThread::STOP);
    }

    qDebug() << "Main decoder finished.";
}
