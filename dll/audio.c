#include "player.h"
#include "packet.h"
#include "frame.h"

static void sdl_audio_callback(void *opaque, Uint8 *stream, int len);

// ��packet_queue��ȡһ��packet����������frame
static int audio_decode_frame(AVCodecContext *p_codec_ctx, packet_queue_t *p_pkt_queue, AVFrame *frame)
{
    int ret;

    while (1)
    {
        AVPacket pkt;

        while (1)
        {
            //if (d->queue->abort_request)
            //    return -1;

            // 3.2 һ����Ƶpacket��һ�������Ƶframe��ÿ��avcodec_receive_frame()����һ��frame���˺������ء�
            // �´ν����˺�����������ȡһ��frame��ֱ��avcodec_receive_frame()����AVERROR(EAGAIN)��
            // ��ʾ��������Ҫ�����µ���Ƶpacket
            ret = avcodec_receive_frame(p_codec_ctx, frame);
            if (ret >= 0)
            {
                // ʱ��ת������d->avctx->pkt_timebaseʱ��ת����1/frame->sample_rateʱ��
                AVRational tb = (AVRational) { 1, frame->sample_rate };
                if (frame->pts != AV_NOPTS_VALUE)
                {
                    frame->pts = av_rescale_q(frame->pts, p_codec_ctx->pkt_timebase, tb);
                }
                else
                {
                    av_log(NULL, AV_LOG_WARNING, "frame->pts no\n");
                }

                return 1;
            }
            else if (ret == AVERROR_EOF)
            {
                av_log(NULL, AV_LOG_INFO, "audio avcodec_receive_frame(): the decoder has been flushed\n");
                avcodec_flush_buffers(p_codec_ctx);
                return 0;
            }
            else if (ret == AVERROR(EAGAIN))
            {
                av_log(NULL, AV_LOG_INFO, "audio avcodec_receive_frame(): input is not accepted in the current state\n");
                break;
            }
            else
            {
                av_log(NULL, AV_LOG_ERROR, "audio avcodec_receive_frame(): other errors\n");
                continue;
            }
        }

        // 1. ȡ��һ��packet��ʹ��pkt��Ӧ��serial��ֵ��d->pkt_serial
        if (packet_queue_get(p_pkt_queue, &pkt, true) < 0)
        {
            return -1;
        }

        // packet_queue�е�һ������flush_pkt��ÿ��seek���������flush_pkt������serial�������µĲ�������
        if (pkt.data == NULL)
        {
            // ��λ�������ڲ�״̬/ˢ���ڲ�����������seek�������л���ʱӦ���ô˺�����
            avcodec_flush_buffers(p_codec_ctx);
        }
        else
        {
            // 2. ��packet���͸�������
            //    ����packet��˳���ǰ�dts������˳����IPBBPBB
            //    pkt.pos�������Ա�ʶ��ǰpacket����Ƶ�ļ��еĵ�ַƫ��
            if (avcodec_send_packet(p_codec_ctx, &pkt) == AVERROR(EAGAIN))
            {
                av_log(NULL, AV_LOG_ERROR, "receive_frame and send_packet both returned EAGAIN, which is an API violation.\n");
            }

            av_packet_unref(&pkt);
        }
    }
}

// ��Ƶ�����̣߳�����Ƶpacket_queue��ȡ���ݣ�����������Ƶframe_queue
static int audio_decode_thread(void *arg)
{
    player_stat_t *is = (player_stat_t *)arg;
    AVFrame *p_frame = av_frame_alloc();
    frame_t *af;

    int got_frame = 0;
    AVRational tb;
    int ret = 0;

    if (p_frame == NULL)
    {
        return AVERROR(ENOMEM);
    }

    while (1)
    {
        got_frame = audio_decode_frame(is->p_acodec_ctx, &is->audio_pkt_queue, p_frame);
        if (got_frame < 0)
        {
            goto the_end;
        }

        if (got_frame)
        {
            tb = (AVRational) { 1, p_frame->sample_rate };

            if (!(af = frame_queue_peek_writable(&is->audio_frm_queue)))
                goto the_end;

            af->pts = (p_frame->pts == AV_NOPTS_VALUE) ? NAN : p_frame->pts * av_q2d(tb);
            af->pos = p_frame->pkt_pos;
            //-af->serial = is->auddec.pkt_serial;
            // ��ǰ֡������(��������)������/�����ʾ��ǵ�ǰ֡�Ĳ���ʱ��
            af->duration = av_q2d((AVRational) { p_frame->nb_samples, p_frame->sample_rate });

            // ��frame���ݿ���af->frame��af->frameָ����Ƶframe����β��
            av_frame_move_ref(af->frame, p_frame);
            // ������Ƶframe���д�С��дָ��
            frame_queue_push(&is->audio_frm_queue);
        }
    }

the_end:
    av_frame_free(&p_frame);
    return ret;
}

int open_audio_stream(player_stat_t *is)
{
    AVCodecContext *p_codec_ctx;
    AVCodecParameters *p_codec_par = NULL;
    AVCodec* p_codec = NULL;
    int ret;

    // 1. Ϊ��Ƶ������������AVCodecContext

    // 1.1 ��ȡ����������AVCodecParameters
    p_codec_par = is->p_audio_stream->codecpar;
    // 1.2 ��ȡ������
    p_codec = avcodec_find_decoder(p_codec_par->codec_id);
    if (p_codec == NULL)
    {
        av_log(NULL, AV_LOG_ERROR, "Cann't find codec!\n");
        return -1;
    }

    // 1.3 ����������AVCodecContext
    // 1.3.1 p_codec_ctx��ʼ��������ṹ�壬ʹ��p_codec��ʼ����Ӧ��ԱΪĬ��ֵ
    p_codec_ctx = avcodec_alloc_context3(p_codec);
    if (p_codec_ctx == NULL)
    {
        av_log(NULL, AV_LOG_ERROR, "avcodec_alloc_context3() failed\n");
        return -1;
    }
    // 1.3.2 p_codec_ctx��ʼ����p_codec_par ==> p_codec_ctx����ʼ����Ӧ��Ա
    ret = avcodec_parameters_to_context(p_codec_ctx, p_codec_par);
    if (ret < 0)
    {
        av_log(NULL, AV_LOG_ERROR, "avcodec_parameters_to_context() failed %d\n", ret);
        return -1;
    }
    // 1.3.3 p_codec_ctx��ʼ����ʹ��p_codec��ʼ��p_codec_ctx����ʼ�����
    ret = avcodec_open2(p_codec_ctx, p_codec, NULL);
    if (ret < 0)
    {
        av_log(NULL, AV_LOG_ERROR, "avcodec_open2() failed %d\n", ret);
        return -1;
    }

    p_codec_ctx->pkt_timebase = is->p_audio_stream->time_base;
    is->p_acodec_ctx = p_codec_ctx;

    // 2. ������Ƶ�����߳�
    SDL_CreateThread(audio_decode_thread, "audio decode thread", is);

    return 0;
}

static int audio_resample2(player_stat_t *is, int64_t audio_callback_time)
{
    int data_size, resampled_data_size;
    int64_t dec_channel_layout;
    av_unused double audio_clock0;
    int wanted_nb_samples;
    frame_t *af;

#if defined(_WIN32)
    while (frame_queue_nb_remaining(&is->audio_frm_queue) == 0)
    {
        if ((av_gettime_relative() - audio_callback_time) > 1000000LL * is->audio_hw_buf_size / is->audio_param_tgt.bytes_per_sec / 2)
            return -1;
        av_usleep(1000);
    }
#endif

    // ������ͷ���ɶ�������afָ��ɶ�֡
    if (!(af = frame_queue_peek_readable(&is->audio_frm_queue)))
        return -1;
    frame_queue_next(&is->audio_frm_queue);

    // ����frame��ָ������Ƶ������ȡ�������Ĵ�С
    data_size = av_samples_get_buffer_size(NULL, af->frame->channels,   // ������������linesize��������
        af->frame->nb_samples,       // ����һ��������֡�а����ĵ��������е�������
        af->frame->format, 1);       // ������������������ʽ��������

// ��ȡ��������
    dec_channel_layout =
        (af->frame->channel_layout && af->frame->channels == av_get_channel_layout_nb_channels(af->frame->channel_layout)) ?
        af->frame->channel_layout : av_get_default_channel_layout(af->frame->channels);
    wanted_nb_samples = af->frame->nb_samples;

    // is->audio_param_tgt��SDL�ɽ��ܵ���Ƶ֡������audio_open()��ȡ�õĲ���
    // ��audio_open()���������С�is->audio_src = is->audio_param_tgt��
    // �˴���ʾ�����frame�е���Ƶ���� == is->audio_src == is->audio_param_tgt������Ƶ�ز����Ĺ��̾�����(���ʱis->swr_ctr��NULL)
    // ��������������ʹ��frame(Դ)��is->audio_param_tgt(Ŀ��)�е���Ƶ����������is->swr_ctx����ʹ��frame�е���Ƶ��������ֵis->audio_src
    if (af->frame->format != is->audio_param_src.fmt ||
        dec_channel_layout != is->audio_param_src.channel_layout ||
        af->frame->sample_rate != is->audio_param_src.freq)
    {
        swr_free(&is->audio_swr_ctx);
        // ʹ��frame(Դ)��is->audio_param_tgt(Ŀ��)�е���Ƶ����������is->audio_swr_ctx
        is->audio_swr_ctx = swr_alloc_set_opts(NULL,
            is->audio_param_tgt.channel_layout, is->audio_param_tgt.fmt, is->audio_param_tgt.freq,
            dec_channel_layout, af->frame->format, af->frame->sample_rate,
            0, NULL);
        if (!is->audio_swr_ctx || swr_init(is->audio_swr_ctx) < 0)
        {
            av_log(NULL, AV_LOG_ERROR,
                "Cannot create sample rate converter for conversion of %d Hz %s %d channels to %d Hz %s %d channels!\n",
                af->frame->sample_rate, av_get_sample_fmt_name(af->frame->format), af->frame->channels,
                is->audio_param_tgt.freq, av_get_sample_fmt_name(is->audio_param_tgt.fmt), is->audio_param_tgt.channels);
            swr_free(&is->audio_swr_ctx);
            return -1;
        }
        // ʹ��frame�еĲ�������is->audio_src����һ�θ��º�����������ִ�д�if��֧�ˣ���Ϊһ����Ƶ���и�frameͨ�ò���һ��
        is->audio_param_src.channel_layout = dec_channel_layout;
        is->audio_param_src.channels = af->frame->channels;
        is->audio_param_src.freq = af->frame->sample_rate;
        is->audio_param_src.fmt = af->frame->format;
    }

    if (is->audio_swr_ctx)
    {
        // �ز����������1��������Ƶ��������af->frame->nb_samples
        // �ز����������2��������Ƶ������
        const uint8_t **in = (const uint8_t **)af->frame->extended_data;
        // �ز����������1�������Ƶ�������ߴ�
        // �ز����������2�������Ƶ������
        uint8_t **out = &is->audio_frm_rwr;
        // �ز�����������������Ƶ������(�����256������)
        int out_count = (int64_t)wanted_nb_samples * is->audio_param_tgt.freq / af->frame->sample_rate + 256;
        // �ز�����������������Ƶ�������ߴ�(���ֽ�Ϊ��λ)
        int out_size = av_samples_get_buffer_size(NULL, is->audio_param_tgt.channels, out_count, is->audio_param_tgt.fmt, 0);
        int len2;
        if (out_size < 0)
        {
            av_log(NULL, AV_LOG_ERROR, "av_samples_get_buffer_size() failed\n");
            return -1;
        }
        av_fast_malloc(&is->audio_frm_rwr, &is->audio_frm_rwr_size, out_size);
        if (!is->audio_frm_rwr)
            return AVERROR(ENOMEM);
        // ��Ƶ�ز���������ֵ���ز�����õ�����Ƶ�����е���������������
        len2 = swr_convert(is->audio_swr_ctx, out, out_count, in, af->frame->nb_samples);
        if (len2 < 0)
        {
            av_log(NULL, AV_LOG_ERROR, "swr_convert() failed\n");
            return -1;
        }
        if (len2 == out_count)
        {
            av_log(NULL, AV_LOG_WARNING, "audio buffer is probably too small\n");
            if (swr_init(is->audio_swr_ctx) < 0)
                swr_free(&is->audio_swr_ctx);
        }
        is->p_audio_frm = is->audio_frm_rwr;
        // �ز������ص�һ֡��Ƶ���ݴ�С(���ֽ�Ϊ��λ)
        resampled_data_size = len2 * is->audio_param_tgt.channels * av_get_bytes_per_sample(is->audio_param_tgt.fmt);
    }
    else
    {
        // δ���ز�������ָ��ָ��frame�е���Ƶ����
        is->p_audio_frm = af->frame->data[0];
        resampled_data_size = data_size;
    }

    audio_clock0 = is->audio_clock;
    /* update the audio clock with the pts */
    if (!isnan(af->pts))
    {
        is->audio_clock = af->pts + (double)af->frame->nb_samples / af->frame->sample_rate;
    }
    else
    {
        is->audio_clock = NAN;
    }
    is->audio_clock_serial = af->serial;
#ifdef DEBUG
    {
        static double last_clock;
        printf("audio: delay=%0.3f clock=%0.3f clock0=%0.3f\n",
            is->audio_clock - last_clock,
            is->audio_clock, audio_clock0);
        last_clock = is->audio_clock;
    }
#endif
    return resampled_data_size;
}

static int open_audio_playing(void *arg)
{
    player_stat_t *is = (player_stat_t *)arg;
    SDL_AudioSpec wanted_spec;
    SDL_AudioSpec actual_spec;

    // 2. ����Ƶ�豸��������Ƶ�����߳�
    // 2.1 ����Ƶ�豸����ȡSDL�豸֧�ֵ���Ƶ����actual_spec(�����Ĳ�����wanted_spec��ʵ�ʵõ�actual_spec)
    // 1) SDL�ṩ����ʹ��Ƶ�豸ȡ����Ƶ���ݷ�����
    //    a. push��SDL���ض���Ƶ�ʵ��ûص��������ڻص�������ȡ����Ƶ����
    //    b. pull���û��������ض���Ƶ�ʵ���SDL_QueueAudio()������Ƶ�豸�ṩ���ݡ��������wanted_spec.callback=NULL
    // 2) ��Ƶ�豸�򿪺󲥷ž������������ص�������SDL_PauseAudio(0)�������ص�����ʼ����������Ƶ
    wanted_spec.freq = is->p_acodec_ctx->sample_rate;   // ������
    wanted_spec.format = AUDIO_S16SYS;                  // S������ţ�16�ǲ�����ȣ�SYS�����ϵͳ�ֽ���
    wanted_spec.channels = is->p_acodec_ctx->channels;  // ����ͨ����
    wanted_spec.silence = 0;                            // ����ֵ
    // wanted_spec.samples = SDL_AUDIO_BUFFER_SIZE;     // SDL�����������ߴ磬��λ�ǵ�����������ߴ�xͨ����
    // SDL�����������ߴ磬��λ�ǵ�����������ߴ�x������
    wanted_spec.samples = FFMAX(SDL_AUDIO_MIN_BUFFER_SIZE, 2 << av_log2(wanted_spec.freq / SDL_AUDIO_MAX_CALLBACKS_PER_SEC));
    wanted_spec.callback = sdl_audio_callback;          // �ص���������ΪNULL����Ӧʹ��SDL_QueueAudio()����
    wanted_spec.userdata = is;                          // �ṩ���ص������Ĳ���
    if (SDL_OpenAudio(&wanted_spec, &actual_spec) < 0)
    {
        av_log(NULL, AV_LOG_ERROR, "SDL_OpenAudio() failed: %s\n", SDL_GetError());
        return -1;
    }

    // 2.2 ����SDL��Ƶ����������Ƶ�ز�������
    // wanted_spec�������Ĳ�����actual_spec��ʵ�ʵĲ�����wanted_spec��auctual_spec����SDL�еĲ�����
    // �˴�audio_param��FFmpeg�еĲ������˲���Ӧ��֤��SDL����֧�ֵĲ����������ز���Ҫ�õ��˲���
    // ��Ƶ֡�����õ���frame�е���Ƶ��ʽδ�ر�SDL֧�֣�����frame������planar��ʽ����SDL2.0����֧��planar��ʽ��
    // ����������frameֱ������SDL��Ƶ���������������޷��������š�������Ҫ�Ƚ�frame�ز���(ת����ʽ)ΪSDL֧�ֵ�ģʽ��
    // Ȼ������д��SDL��Ƶ������
    is->audio_param_tgt.fmt = AV_SAMPLE_FMT_S16;
    is->audio_param_tgt.freq = actual_spec.freq;
    is->audio_param_tgt.channel_layout = av_get_default_channel_layout(actual_spec.channels);;
    is->audio_param_tgt.channels = actual_spec.channels;
    is->audio_param_tgt.frame_size = av_samples_get_buffer_size(NULL, actual_spec.channels, 1, is->audio_param_tgt.fmt, 1);
    is->audio_param_tgt.bytes_per_sec = av_samples_get_buffer_size(NULL, actual_spec.channels, actual_spec.freq, is->audio_param_tgt.fmt, 1);
    if (is->audio_param_tgt.bytes_per_sec <= 0 || is->audio_param_tgt.frame_size <= 0)
    {
        av_log(NULL, AV_LOG_ERROR, "av_samples_get_buffer_size failed\n");
        return -1;
    }
    is->audio_param_src = is->audio_param_tgt;
    is->audio_hw_buf_size = actual_spec.size;   // SDL��Ƶ��������С
    is->audio_frm_size = 0;
    is->audio_cp_index = 0;

    // 3. ��ͣ/������Ƶ�ص���������1����ͣ��0�������
    //     ����Ƶ�豸��Ĭ��δ�����ص�����ͨ������SDL_PauseAudio(0)�������ص�����
    //     �����Ϳ����ڴ���Ƶ�豸����Ϊ�ص�������ȫ��ʼ�����ݣ�һ�о�������������Ƶ�ص���
    //     ����ͣ�ڼ䣬�Ὣ����ֵ����Ƶ�豸д��
    SDL_PauseAudio(0);
}

// ��Ƶ����ص������������л�ȡ��Ƶ�������룬����
// �˺�����SDL������ã��˺��������û����߳��У����������Ҫ����
// \param[in]  opaque �û���ע��ص�����ʱָ���Ĳ���
// \param[out] stream ��Ƶ���ݻ�������ַ������������Ƶ��������˻�����
// \param[out] len    ��Ƶ���ݻ�������С����λ�ֽ�
// �ص��������غ�streamָ�����Ƶ����������Ϊ��Ч
// ˫�����������˳��ΪLRLRLR
static void sdl_audio_callback(void *opaque, Uint8 *stream, int len)
{
    player_stat_t *is = (player_stat_t *)opaque;
    int audio_size, len1;

    int64_t audio_callback_time = av_gettime_relative();

    while (len > 0) // �������len����is->audio_hw_buf_size����audio_open()�����뵽��SDL��Ƶ��������С
    {
        if (is->audio_cp_index >= (int)is->audio_frm_size)
        {
            // 1. ����Ƶframe������ȡ��һ��frame��ת��Ϊ��Ƶ�豸֧�ֵĸ�ʽ������ֵ���ز�����Ƶ֡�Ĵ�С
            audio_size = audio_resample2(is, audio_callback_time);
            if (audio_size < 0)
            {
                /* if error, just output silence */
                is->p_audio_frm = NULL;
                is->audio_frm_size = SDL_AUDIO_MIN_BUFFER_SIZE / is->audio_param_tgt.frame_size * is->audio_param_tgt.frame_size;
            }
            else
            {
                is->audio_frm_size = audio_size;
            }
            is->audio_cp_index = 0;
        }
        // ����is->audio_cp_index�����ã���ֹһ֡��Ƶ���ݴ�С����SDL��Ƶ��������С������һ֡������Ҫ������ο���
        // ��is->audio_cp_index��ʶ�ز���֡���ѿ���SDL��Ƶ������������λ��������len1��ʾ���ο�����������
        len1 = is->audio_frm_size - is->audio_cp_index;
        if (len1 > len)
        {
            len1 = len;
        }
        // 2. ��ת�������Ƶ���ݿ�������Ƶ������stream�У�֮��Ĳ��ž�����Ƶ�豸��������Ĺ�����
        if (is->p_audio_frm != NULL)
        {
            memcpy(stream, (uint8_t *)is->p_audio_frm + is->audio_cp_index, len1);
        }
        else
        {
            memset(stream, 0, len1);
        }

        len -= len1;
        stream += len1;
        is->audio_cp_index += len1;
    }
    // is->audio_write_buf_size�Ǳ�֡����δ����SDL��Ƶ��������������
    is->audio_write_buf_size = is->audio_frm_size - is->audio_cp_index;
    /* Let's assume the audio driver that is used by SDL has two periods. */
    // 3. ����ʱ��
    if (!isnan(is->audio_clock))
    {
        // ������Ƶʱ�ӣ�����ʱ�̣�ÿ���������������������ݺ�
        // ǰ��audio_decode_frame�и��µ�is->audio_clock������Ƶ֡Ϊ��λ�����Դ˴��ڶ�������Ҫ��ȥδ����������ռ�õ�ʱ��
        set_clock_at(&is->audio_clk,
            is->audio_clock - (double)(2 * is->audio_hw_buf_size + is->audio_write_buf_size) / is->audio_param_tgt.bytes_per_sec,
            is->audio_clock_serial,
            audio_callback_time / 1000000.0);
    }
}

int open_audio(player_stat_t *is)
{
    open_audio_stream(is);
    open_audio_playing(is);

    return 0;
}
